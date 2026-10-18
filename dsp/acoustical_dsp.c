/* acoustical_dsp.c — Implementación del núcleo DSP compartido.
 *
 * La FFT es una portación literal del algoritmo original de Acoustical
 * Estudio (ventana Hamming, bit-reversal, Cooley-Tukey radix-2, piso
 * -120 dB), con la única mejora de que la tabla de twiddles, la tabla de
 * bit-reversal y el scratch real/imaginario se precalculan/preasignan una vez
 * en create() en vez de hacer cos/sin y malloc/free por (stage, i) y frame.
 *
 * La normalización es 2·|X|/(n·0.54): compensa la ganancia coherente de la
 * ventana Hamming (0.54) para que un tono a 0 dBFS lea 0 dB. El frente
 * Android aplica el mismo factor en su FFT Kotlin (ver test_golden /
 * golden_bands.json como fixture compartido).
 *
 * aggregate_bands usa dos punteros sobre los arrays ordenados para ser
 * O(bandas + bins) en vez de O(bandas * bins).
 */
#include "acoustical_dsp.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ACOUSTICAL_PI 3.14159265358979323846
#define ACOUSTICAL_DB_FLOOR (-120.0f)

/* =========================================================================
 * FFT
 * ========================================================================= */

struct acoustical_fft {
    int size;       /* total number of samples (power of two, >= 2) */
    int bin_count;  /* size / 2 */
    float *window;  /* Hamming window, size entries */
    int *bitrev;    /* bit-reversal permutation, size entries */
    float *twirl;   /* twiddle cos table, size/2 entries (cos(2*pi*k/size)) */
    float *twirm;   /* twiddle sin table, size/2 entries (sin(2*pi*k/size)) */
    /* Scratch de la FFT (parte real/imaginaria), preasignado en create():
     * antes se hacía malloc/free EN CADA frame dentro de compute_magnitudes_db,
     * lo que añadía jitter al path de audio y, si la alloc fallaba en caliente,
     * dejaba el buffer de salida con magnitudes stale del frame anterior sin
     * que el caller pudiera distinguirlo. */
    float *re;
    float *im;
};

static int is_power_of_two(int n) { return n > 0 && (n & (n - 1)) == 0; }

acoustical_fft *acoustical_fft_create(int size) {
    /* size == 1 es UB: la ventana divide por (size - 1) más abajo. */
    if (size < 2 || !is_power_of_two(size))
        return NULL;

    acoustical_fft *fft = (acoustical_fft *)calloc(1, sizeof(acoustical_fft));
    if (!fft) return NULL;
    fft->size = size;
    fft->bin_count = size / 2;

    fft->window = (float *)malloc(sizeof(float) * size);
    fft->bitrev = (int *)malloc(sizeof(int) * size);
    fft->twirl = (float *)malloc(sizeof(float) * (size / 2));
    fft->twirm = (float *)malloc(sizeof(float) * (size / 2));
    fft->re = (float *)malloc(sizeof(float) * size);
    fft->im = (float *)malloc(sizeof(float) * size);
    if (!fft->window || !fft->bitrev || !fft->twirl || !fft->twirm ||
        !fft->re || !fft->im) {
        acoustical_fft_free(fft);
        return NULL;
    }

    /* Hamming window: 0.54 - 0.46*cos(2*pi*i/(N-1)).
     * Computed in double and cast to float, exactly like the original. */
    for (int i = 0; i < size; ++i) {
        fft->window[i] = (float)(0.54 - 0.46 * cos(2.0 * ACOUSTICAL_PI * i / (size - 1)));
    }

    /* Bit-reversal table (log2(size) bits). */
    const int bits = (int)(log2f((float)size) + 0.5f);
    for (int i = 0; i < size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r = (r << 1) | ((i >> b) & 1);
        fft->bitrev[i] = r;
    }

    /* Twiddle table: exp(-2*pi*i*k/size) for k in [0, size/2).
     * Computed in double and cast to float, exactly like the original
     * (std::cos/std::sin in double, then static_cast<float>). We store cos and
     * sin separately; the FFT body uses them directly. */
    for (int k = 0; k < size / 2; ++k) {
        const double ang = (2.0 * ACOUSTICAL_PI * k) / size;
        fft->twirl[k] = (float)cos(ang);
        fft->twirm[k] = (float)sin(ang);
    }
    return fft;
}

void acoustical_fft_free(acoustical_fft *fft) {
    if (!fft) return;
    free(fft->window);
    free(fft->bitrev);
    free(fft->twirl);
    free(fft->twirm);
    free(fft->re);
    free(fft->im);
    free(fft);
}

int acoustical_fft_bin_count(const acoustical_fft *fft) { return fft ? fft->bin_count : 0; }

void acoustical_fft_bin_frequencies(const acoustical_fft *fft, int sample_rate,
                                    float *out_bin_freqs) {
    if (!fft || !out_bin_freqs) return;
    const float hz_per_bin = (float)sample_rate / (float)fft->size;
    for (int i = 0; i < fft->bin_count; ++i)
        out_bin_freqs[i] = (float)i * hz_per_bin;
}

void acoustical_fft_compute_magnitudes_db(acoustical_fft *fft,
                                          const float *input,
                                          int sample_rate,
                                          float *out_magnitudes_db) {
    if (!fft || !input || !out_magnitudes_db) return;
    (void)sample_rate;

    /* El scratch ya vive dentro del handle (ver create): sin malloc por frame.
     * Si por alguna razón no está (no debería: create falla antes), no se
     * escribe nada en out — mismo estado observable que el fallo de alloc del
     * código anterior, pero ya no se producirán OOMs en caliente. */
    if (!fft->re || !fft->im) return;

    const int n = fft->size;
    const int half = fft->bin_count;
    float *re = fft->re;
    float *im = fft->im;

    /* Aplica la ventana Hamming y el bit-reversal.
     *
     * Sanitización: una muestra de entrada no finita (NaN/Inf — por ejemplo,
     * un pico de conversión A/D o un buffer sin inicializar) envenena TODOS
     * los bins del frame y, aguas abajo, al corrector y a los sweepers, que
     * suavizan hacia ese valor frame tras frame. Se trata como 0.0 en la
     * ventana (muestra silenciada), que es lo más defensivo y barato. */
    for (int i = 0; i < n; ++i) {
        const int r = fft->bitrev[i];
        const float x = isfinite(input[i]) ? input[i] : 0.0f;
        re[r] = x * fft->window[i];
        im[r] = 0.0f;
    }

    /* Iterative Cooley-Tukey radix-2. Twiddles come from the precomputed
     * table: exp(-2*pi*i*k/n) = twirl[k] - i*twirm[k]. */
    for (int len = 2; len <= n; len <<= 1) {
        const int half_len = len >> 1;
        const int table_step = n / len; /* twiddle index increment */
        for (int start = 0; start < n; start += len) {
            for (int i = 0; i < half_len; ++i) {
                const int k = i * table_step;
                const float wr = fft->twirl[k];
                const float wi = -fft->twirm[k]; /* exp(-i*ang) */
                const int e = start + i;
                const int o = e + half_len;
                const float tr = wr * re[o] - wi * im[o];
                const float ti = wr * im[o] + wi * re[o];
                re[o] = re[e] - tr;
                im[o] = im[e] - ti;
                re[e] += tr;
                im[e] += ti;
            }
        }
    }

    /* Magnitudes en dB.
     *
     * Normalización: 2·|X|/(n · 0.54). El factor 0.54 es la ganancia
     * coherente de la ventana Hamming (la suma de la ventana ≈ 0.54·n): con
     * la normalización de ventana rectangular (2/n) un seno a escala 1.0 se
     * leía a ~-5.4 dB, porque la ventana "roba" esa ganancia. Dividir también
     * por 0.54 la compensa, de modo que un tono a 0 dBFS lea 0 dB (el pico
     * exacto depende de si la frecuencia cae en el centro de un bin y de la
     * dispersión de energía entre bins vecinas, pero el orden es el correcto).
     * El frente Android aplica el mismo factor en su FFT Kotlin para no
     * divergir de esta referencia.
     */
    const float normFactor = 2.0f / ((float)n * 0.54f);
    for (int i = 0; i < half; ++i) {
        const float mag = (float)(sqrt((double)re[i] * re[i] + (double)im[i] * im[i]) * normFactor);
        out_magnitudes_db[i] = mag > 1e-10f ? 20.0f * log10f(mag) : -120.0f;
    }
}

/* =========================================================================
 * Band aggregation (two-pointer)
 * ========================================================================= */

void acoustical_aggregate_bands(const float *band_frequencies, int band_count,
                                const float *magnitudes_db, int mag_count,
                                const float *bin_frequencies, int bin_count,
                                float noise_floor_db,
                                float *out_band_levels) {
    /* Guard: un array magnitudes_db NULL con mag_count > 0 dereferenciaba
     * NULL en el bucle de agregación (los bins [lo, hi) se leían sin checar). */
    if (!band_frequencies || !bin_frequencies || !magnitudes_db || !out_band_levels) return;

    /* Ratio matching the platform implementations. */
    const float ratio = (band_count > 40)
        ? expf(logf(2.0f) / 12.0f)
        : expf(logf(2.0f) / 6.0f);

    /* Both bin arrays are ascending in frequency. For each band (also
     * ascending in center frequency) the qualifying bin range
     * [lower, upper] moves monotonically forward, so two cursors give a
     * linear total scan. */
    int lo = 0;   /* first bin with freq >= current lower */
    int hi = 0;   /* first bin with freq >  current upper (exclusive end) */

    for (int b = 0; b < band_count; ++b) {
        const float center = band_frequencies[b];
        const float lower = center / ratio;
        const float upper = center * ratio;

        /* Advance lo to the first bin with freq >= lower. */
        while (lo < bin_count && bin_frequencies[lo] < lower) ++lo;
        /* Advance hi to the first bin with freq > upper. */
        if (hi < lo) hi = lo;
        while (hi < bin_count && bin_frequencies[hi] <= upper) ++hi;

        double sum = 0.0;
        int count = 0;
        for (int i = lo; i < hi; ++i) {
            /* Respect the original guard: only bins that have a magnitude
             * entry participate. */
            if (i >= mag_count) break;
            if (magnitudes_db[i] > noise_floor_db) {
                sum += magnitudes_db[i];
                ++count;
            }
        }
        out_band_levels[b] = (count > 0) ? (float)(sum / count) : noise_floor_db;
    }
}

/* =========================================================================
 * Biquad peaking EQ (RBJ cookbook)
 * ========================================================================= */

void acoustical_make_peaking(float center_freq_hz, float sample_rate,
                             float gain_db, float q,
                             acoustical_biquad_coeffs *out) {
    /* Validación de entrada: sin chequeos, f0<=0 / q<=0 / sample_rate<=0 o
     * cualquier NaN producen coeficientes Inf/NaN que envenenan el banco de
     * EQ EN VIVO (el corrector re-computa los peaking en cada frame y un
     * único frame corrupto queda en el estado de los biquads hasta el
     * próximo re-prepare). Con parámetros inválidos se escribe un BYPASS
     * (paso directo: b0=1, resto 0) y se regresa: la banda no corrige. */
    if (out == NULL) return;
    if (!(sample_rate > 0.0f && center_freq_hz > 0.0f && q > 0.0f &&
          isfinite(sample_rate) && isfinite(center_freq_hz) &&
          isfinite(gain_db) && isfinite(q))) {
        out->b0 = 1.0f;
        out->b1 = 0.0f;
        out->b2 = 0.0f;
        out->a1 = 0.0f;
        out->a2 = 0.0f;
        return;
    }
    /* Clamps: fuera de [10 Hz, 0.99·fs/2] el w0 sale del rango del cookbook
     * (polos fuera de la unidad, respuesta degenerada); q fuera de [0.1, 10]
     * amplifica el ruido de los coeficientes. Se calcula siempre dentro. */
    if (center_freq_hz < 10.0f) center_freq_hz = 10.0f;
    const float fmax = 0.99f * sample_rate * 0.5f;
    if (center_freq_hz > fmax) center_freq_hz = fmax;
    if (q < 0.1f) q = 0.1f;
    if (q > 10.0f) q = 10.0f;

    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * (float)ACOUSTICAL_PI * center_freq_hz / sample_rate;
    const float cw = cosf(w0);
    const float sw = sinf(w0);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha / A;
    out->b0 = (1.0f + alpha * A) / a0;
    out->b1 = (-2.0f * cw) / a0;
    out->b2 = (1.0f - alpha * A) / a0;
    out->a1 = (-2.0f * cw) / a0;
    out->a2 = (1.0f - alpha / A) / a0;
}

float acoustical_biquad_process(const acoustical_biquad_coeffs *c,
                                float state[2], float x) {
    const float y = c->b0 * x + state[0];
    state[0] = c->b1 * x - c->a1 * y + state[1];
    state[1] = c->b2 * x - c->a2 * y;
    return y;
}

/* =========================================================================
 * Signal generator
 * ========================================================================= */

struct acoustical_siggen {
    int lastWaveform;              /* del bloque anterior (para el reset) */
    double phase;                  /* seno: radianes */
    double sweepTime;              /* barrido: t (s) dentro del barrido */
    double sweepIntPhase;          /* barrido: fase integrada (radianes) */
    float pinkB0, pinkB1, pinkB2, pinkB3, pinkB4, pinkB5, pinkB6;
    /* PRNG portables (xoshiro128**): el blanco no depende de la plataforma
     * ni del thread; la semilla fija hace las corridas reproducibles. */
    uint32_t rng[4];
};

static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

/* xoshiro128**: sucesor portable (sin libc) del mt19937 de la app Windows
 * y del Random de Android para el ruido blanco; uniforme en [-1, 1). */
static float whiteSample(acoustical_siggen *g) {
    uint32_t const result32 = rotl(g->rng[1] * 5, 7) * 9;
    const uint32_t t = g->rng[1] << 9;
    g->rng[2] ^= g->rng[0];
    g->rng[3] ^= g->rng[1];
    g->rng[1] ^= g->rng[2];
    g->rng[0] ^= g->rng[3];
    g->rng[2] ^= t;
    g->rng[3] = rotl(g->rng[3], 11);
    /* 32 bits de rango → [-1, 1) */
    return (float)((double)result32 / 2147483648.0 - 1.0);
}

acoustical_siggen *acoustical_siggen_create(void) {
    acoustical_siggen *g = (acoustical_siggen *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->lastWaveform = -1;  /* la primera fill() cuenta como cambio de forma */
    /* Semilla fija (reproducible entre plataformas y tests). */
    g->rng[0] = 0x12345678u;
    g->rng[1] = 0x9ABCDEF0u;
    g->rng[2] = 0x11111111u;
    g->rng[3] = 0x22222222u;
    return g;
}

void acoustical_siggen_free(acoustical_siggen *g) { free(g); }

void acoustical_siggen_fill(acoustical_siggen *g,
                            int waveform,
                            float frequencyHz,
                            float amplitude,
                            float sweepStartHz,
                            float sweepEndHz,
                            float sweepSeconds,
                            float sampleRate,
                            float *out,
                            int count) {
    if (!g || !out || count <= 0) return;

    /* Cambio de forma de onda (vs la llamada anterior): se resetean las
     * fases y el filtro rosa EN el hilo que consume el handle (aquí), NUNCA
     * en un setter de la UI: escribir estado compartido con el audio en
     * marcha era el bug original (transientes de nivel al volver a rosa). */
    if (waveform != g->lastWaveform) {
        g->lastWaveform = waveform;
        g->phase = 0.0;
        g->sweepTime = 0.0;
        g->sweepIntPhase = 0.0;
        g->pinkB0 = g->pinkB1 = g->pinkB2 = g->pinkB3 =
            g->pinkB4 = g->pinkB5 = g->pinkB6 = 0.0f;
    }

    if (waveform == ACOUSTICAL_WAVE_SILENCE) {
        for (int i = 0; i < count; ++i) out[i] = 0.0f;
        return;
    }
    /* Tasa corrupta (<= 0 / no finita) daría incrementos de fase Inf/NaN que
     * envenenarían el buffer de salida: se silencia el bloque. */
    if (!(sampleRate > 0.0f) || !isfinite((double)sampleRate)) {
        for (int i = 0; i < count; ++i) out[i] = 0.0f;
        return;
    }

    const double twoPi = 2.0 * ACOUSTICAL_PI;
    for (int i = 0; i < count; ++i) {
        float sample = 0.0f;
        switch (waveform) {
            case ACOUSTICAL_WAVE_SINE:
            case ACOUSTICAL_WAVE_BAND_SINE:
                sample = (float)sin(g->phase);
                g->phase += twoPi * (double)frequencyHz / (double)sampleRate;
                if (g->phase >= twoPi) g->phase -= twoPi;
                break;
            case ACOUSTICAL_WAVE_LOG_SWEEP: {
                /* f(t) = f0·(f1/f0)^(t/T) con fase integrada muestra a muestra
                 * (frecuencia instantánea EXACTA, sin aliasing). */
                const double T = (sweepSeconds > 0.0f) ? (double)sweepSeconds : 5.0;
                const double ratio =
                    log((double)sweepEndHz / (double)sweepStartHz) / T;
                const double instFreq =
                    (double)sweepStartHz * exp(ratio * g->sweepTime);
                g->sweepIntPhase += twoPi * instFreq / (double)sampleRate;
                sample = (float)sin(g->sweepIntPhase);
                g->sweepTime += 1.0 / (double)sampleRate;
                if (g->sweepTime >= T) {
                    g->sweepTime = 0.0;
                    g->sweepIntPhase = 0.0;
                }
                break;
            }
            case ACOUSTICAL_WAVE_WHITE:
                sample = whiteSample(g);
                break;
            case ACOUSTICAL_WAVE_PINK: {
                /* Paul Kellet (economical) — -3 dB/octava. */
                const float white = whiteSample(g);
                g->pinkB0 = 0.99886f * g->pinkB0 + white * 0.0555179f;
                g->pinkB1 = 0.99332f * g->pinkB1 + white * 0.0750759f;
                g->pinkB2 = 0.96900f * g->pinkB2 + white * 0.1538520f;
                g->pinkB3 = 0.86650f * g->pinkB3 + white * 0.3104856f;
                g->pinkB4 = 0.55000f * g->pinkB4 + white * 0.5329522f;
                g->pinkB5 = -0.7616f * g->pinkB5 - white * 0.0168980f;
                sample = (g->pinkB0 + g->pinkB1 + g->pinkB2 + g->pinkB3 +
                          g->pinkB4 + g->pinkB5 + g->pinkB6 + white * 0.5362f) *
                         0.11f;
                g->pinkB6 = white * 0.115926f;
                break;
            }
            default:
                break;
        }
        out[i] = sample * amplitude;
    }
}

/* =========================================================================
 * Spatial position → SPL compensation
 * ========================================================================= */

static float clampF(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void acoustical_spatial_compensate(float x, float y, float z, float size,
                                    float *out_gain_db, float *out_delay_ms) {
    /* Entradas no finitas (un fader corrupto) no pueden envenenar la cadena
     * de salida en vivo: se trata todo como posición NEUTRA (centrada, cerca,
     * tamaño medio) → 0 dB / 0 ms, nunca +3 dB de "tamaño 0". */
    if (!isfinite((double)x) || !isfinite((double)z) ||
        !isfinite((double)size)) {
        x = 0.0f; z = 0.0f; size = 0.5f;
    }
    (void)y;  /* la ley de compensación no usa y (solo la posición la guarda) */
    x = clampF(x, -1.0f, 1.0f);
    z = clampF(z, 0.0f, 1.0f);
    size = clampF(size, 0.0f, 1.0f);

    const float distanceMeters = z * 15.0f;
    const float distanceGain =
        distanceMeters > 0.5f ? 20.0f * log10f(distanceMeters / 1.0f) : 0.0f;
    const float sizeGain = (0.5f - size) * 6.0f;
    const float lateralGain = fabsf(x) * 1.5f;

    const float gain = distanceGain + sizeGain + lateralGain;
    const float delayMs = (distanceMeters / 343.0f) * 1000.0f;
    if (out_gain_db) *out_gain_db = gain;
    if (out_delay_ms) *out_delay_ms = delayMs;
}

/* =========================================================================
 * Work modes (catalog of startup profiles)
 * =========================================================================
 *
 * Los SEIS perfiles de arranque del sistema de modos, con los
 * valores acordados (v1.10.0; todos: ganancia máx 50 dB, 124 bandas ·
 * FFT 8192 · análisis 25 ms, suavizado 0.05, generador en Silencio y el
 * banco de bandas de apoyo por ecu asignado por zonas de frecuencia):
 *
 *   eq1 (donde más se necesita): 2.5k · 5k · 10k · 20 kHz
 *   eq2 (empieza por los graves): 63 · 250 · 1k · 4 kHz
 *   eq3 (empieza por los agudos): 20 · 41 · 62 · 83 Hz
 *
 *   Por ecu (v1.10.0): intervalo de decisión 100 ms · mezclador 80 % ·
 *   barridos extra 6 · banda de apoyo a la Q más estrecha (0.1):
 *   eq1 ganancia máx 12 dB, velocidad ×4 · eq2 6 dB, ×2 · eq3 3 dB, ×0.5
 *
 *   id                SPL   corr  sub  espacio (z, size)
 *   carro-normal      78    ON    ON   cerca, pequeño (0.2, 0.3)
 *   carro-aire-libre  97    OFF   ON   medio (0.5, 0.5)
 *   estudio-esc1      89    ON    ON   medio (0.5, 0.5)
 *   escenario2        91    OFF   OFF  medio (0.5, 0.5)
 *   escenario3        93    ON    ON   lejos, grande (0.9, 0.9)
 *   pas               103   ON    ON   cerca, pequeño (0.2, 0.3)
 *
 * "corr" = el corrector principal (el "básico"/super-EQ 124); en los modos
 * con OFF los tres ecuas dinámicos siguen activos (las "3 pasadas extra").
 */

#define ACOUSTICAL_B124 4
#define ACOUSTICAL_FFT_8192 4
#define ACOUSTICAL_AN_25MS 0
#define ACOUSTICAL_GAIN50 50.0f
#define ACOUSTICAL_SMOOTH 0.05f
/* v1.10.0: valores comunes a los tres ecuas dinámicos del catálogo:
 * intervalo de decisión 100 ms, 6 barridos extra por decisión y la Q de
 * las bandas de apoyo a lo más estrecho posible (0.1, mínimo del slider
 * de las tres versiones). */
#define ACOUSTICAL_EQ_INTERVAL 100
#define ACOUSTICAL_EQ_SWEEPS 6
#define ACOUSTICAL_EQ_Q 0.1f

static acoustical_support_band bandHz(float hz) {
    acoustical_support_band b;
    b.frequencyHz = hz;
    b.gainDb = 0.0f;
    b.q = ACOUSTICAL_EQ_Q;
    return b;
}

/* v1.10.0: cada ecu lleva su ganancia máxima y su velocidad de suavizado
 * (los tres comparten intervalo, mezclador 0.8 y barridos extra). */
static acoustical_eq_profile makeEq(int enabled, int startFrom,
                                     float maxGainDb, float speedMultiplier,
                                     float f0, float f1, float f2, float f3) {
    acoustical_eq_profile e;
    e.enabled = enabled;
    e.intervalMs = ACOUSTICAL_EQ_INTERVAL;
    e.maxGainDb = maxGainDb;
    e.mixerLevel = 0.8f;
    e.speedMultiplier = speedMultiplier;
    e.extraSweeps = ACOUSTICAL_EQ_SWEEPS;
    e.startFrom = startFrom;
    /* Banco por zona (las 4 frecuencias se pasan por valor: sin compound
     * literals, que el .c se compila también como C++ en el gate). */
    const float freqs[4] = { f0, f1, f2, f3 };
    for (int k = 0; k < 4; ++k) e.support[k] = bandHz(freqs[k]);
    return e;
}

static acoustical_mode_profile kModeProfiles[ACOUSTICAL_MODE_COUNT];

/* v1.9: la tabla se rellena en runtime (primera consulta) en vez de con un
 * initializer estático: en C estricto no se permiten llamadas a funciones
 * (makeEq/bandHz) dentro de un initializer de objeto estático — la forma
 * anterior (v1.8, `= { ... }`) solo compilaba cuando el .c se trataba como
 * C++ (así compila el gate), y el NDK de Android (clang, C puro) la
 * rechazaba. Mismos valores, mismo orden: el fichero compila igual como C
 * que como C++. */
static void ensureModeProfiles(void) {
    static int ready = 0;
    if (ready) return;

    kModeProfiles[0] = (acoustical_mode_profile){
        "carro-normal", "Coche · Normal",
        78.0f, ACOUSTICAL_GAIN50, ACOUSTICAL_B124, ACOUSTICAL_FFT_8192,
        ACOUSTICAL_AN_25MS, ACOUSTICAL_SMOOTH, 1, 1, 0.0f,
        { makeEq(1, 0, 12.0f, 4.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f),
          makeEq(1, 1, 6.0f, 2.0f, 63.0f, 250.0f, 1000.0f, 4000.0f),
          makeEq(1, 2, 3.0f, 0.5f, 20.0f, 41.0f, 62.0f, 83.0f) },
        0.0f, 0.0f, 0.2f, 0.3f,
        ACOUSTICAL_WAVE_SILENCE, -12.0f
    };

    kModeProfiles[1] = (acoustical_mode_profile){
        "carro-aire-libre", "Coche · Aire libre",
        97.0f, ACOUSTICAL_GAIN50, ACOUSTICAL_B124, ACOUSTICAL_FFT_8192,
        ACOUSTICAL_AN_25MS, ACOUSTICAL_SMOOTH, 0, 1, 0.0f,
        { makeEq(1, 0, 12.0f, 4.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f),
          makeEq(1, 1, 6.0f, 2.0f, 63.0f, 250.0f, 1000.0f, 4000.0f),
          makeEq(1, 2, 3.0f, 0.5f, 20.0f, 41.0f, 62.0f, 83.0f) },
        0.0f, 0.0f, 0.5f, 0.5f,
        ACOUSTICAL_WAVE_SILENCE, -12.0f
    };

    kModeProfiles[2] = (acoustical_mode_profile){
        "estudio-esc1", "Estudio · Escenario 1",
        89.0f, ACOUSTICAL_GAIN50, ACOUSTICAL_B124, ACOUSTICAL_FFT_8192,
        ACOUSTICAL_AN_25MS, ACOUSTICAL_SMOOTH, 1, 1, 0.0f,
        { makeEq(1, 0, 12.0f, 4.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f),
          makeEq(1, 1, 6.0f, 2.0f, 63.0f, 250.0f, 1000.0f, 4000.0f),
          makeEq(1, 2, 3.0f, 0.5f, 20.0f, 41.0f, 62.0f, 83.0f) },
        0.0f, 0.0f, 0.5f, 0.5f,
        ACOUSTICAL_WAVE_SILENCE, -12.0f
    };

    kModeProfiles[3] = (acoustical_mode_profile){
        "escenario2", "Escenario 2",
        91.0f, ACOUSTICAL_GAIN50, ACOUSTICAL_B124, ACOUSTICAL_FFT_8192,
        ACOUSTICAL_AN_25MS, ACOUSTICAL_SMOOTH, 0, 0, 0.0f,
        { makeEq(1, 0, 12.0f, 4.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f),
          makeEq(1, 1, 6.0f, 2.0f, 63.0f, 250.0f, 1000.0f, 4000.0f),
          makeEq(1, 2, 3.0f, 0.5f, 20.0f, 41.0f, 62.0f, 83.0f) },
        0.0f, 0.0f, 0.5f, 0.5f,
        ACOUSTICAL_WAVE_SILENCE, -12.0f
    };

    kModeProfiles[4] = (acoustical_mode_profile){
        "escenario3", "Escenario 3",
        93.0f, ACOUSTICAL_GAIN50, ACOUSTICAL_B124, ACOUSTICAL_FFT_8192,
        ACOUSTICAL_AN_25MS, ACOUSTICAL_SMOOTH, 1, 1, 0.0f,
        { makeEq(1, 0, 12.0f, 4.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f),
          makeEq(1, 1, 6.0f, 2.0f, 63.0f, 250.0f, 1000.0f, 4000.0f),
          makeEq(1, 2, 3.0f, 0.5f, 20.0f, 41.0f, 62.0f, 83.0f) },
        0.0f, 0.0f, 0.9f, 0.9f,
        ACOUSTICAL_WAVE_SILENCE, -12.0f
    };

    kModeProfiles[5] = (acoustical_mode_profile){
        "pas", "PAs",
        103.0f, ACOUSTICAL_GAIN50, ACOUSTICAL_B124, ACOUSTICAL_FFT_8192,
        ACOUSTICAL_AN_25MS, ACOUSTICAL_SMOOTH, 1, 1, 0.0f,
        { makeEq(1, 0, 12.0f, 4.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f),
          makeEq(1, 1, 6.0f, 2.0f, 63.0f, 250.0f, 1000.0f, 4000.0f),
          makeEq(1, 2, 3.0f, 0.5f, 20.0f, 41.0f, 62.0f, 83.0f) },
        0.0f, 0.0f, 0.2f, 0.3f,
        ACOUSTICAL_WAVE_SILENCE, -12.0f
    };

    ready = 1;
}

const acoustical_mode_profile *acoustical_mode_profiles(void) {
    ensureModeProfiles();
    return kModeProfiles;
}

const acoustical_mode_profile *acoustical_mode_by_id(const char *id) {
    ensureModeProfiles();
    if (!id) return NULL;
    for (int i = 0; i < ACOUSTICAL_MODE_COUNT; ++i)
        if (strcmp(kModeProfiles[i].id, id) == 0) return &kModeProfiles[i];
    return NULL;
}
