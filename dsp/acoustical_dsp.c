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
