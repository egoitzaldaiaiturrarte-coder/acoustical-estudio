/* acoustical_dsp.c — Implementación del núcleo DSP compartido.
 *
 * La FFT es una portación literal del algoritmo original de Acoustical
 * Estudio (ventana Hamming, bit-reversal, Cooley-Tukey radix-2, normalización
 * 2*|X|/n, piso -120 dB), con la única mejora de que la tabla de twiddles y
 * la tabla de bit-reversal se precomputan una vez en create() en vez de
 * hacer cos/sin por (stage, i) en cada frame.
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
    int size;       /* total number of samples (power of two) */
    int bin_count;  /* size / 2 */
    float *window;  /* Hamming window, size entries */
    int *bitrev;    /* bit-reversal permutation, size entries */
    float *twirl;   /* twiddle cos table, size/2 entries (cos(2*pi*k/size)) */
    float *twirm;   /* twiddle sin table, size/2 entries (sin(2*pi*k/size)) */
};

static int is_power_of_two(int n) { return n > 0 && (n & (n - 1)) == 0; }

acoustical_fft *acoustical_fft_create(int size) {
    if (!is_power_of_two(size))
        return NULL;

    acoustical_fft *fft = (acoustical_fft *)calloc(1, sizeof(acoustical_fft));
    if (!fft) return NULL;
    fft->size = size;
    fft->bin_count = size / 2;

    fft->window = (float *)malloc(sizeof(float) * size);
    fft->bitrev = (int *)malloc(sizeof(int) * size);
    fft->twirl = (float *)malloc(sizeof(float) * (size / 2));
    fft->twirm = (float *)malloc(sizeof(float) * (size / 2));
    if (!fft->window || !fft->bitrev || !fft->twirl || !fft->twirm) {
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

    const int n = fft->size;
    const int half = fft->bin_count;
    float *re = (float *)malloc(sizeof(float) * n);
    float *im = (float *)malloc(sizeof(float) * n);
    if (!re || !im) {
        free(re);
        free(im);
        return;
    }

    /* Apply Hamming window and bit-reverse. */
    for (int i = 0; i < n; ++i) {
        const int r = fft->bitrev[i];
        re[r] = input[i] * fft->window[i];
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

    /* Magnitude in dB, exactly like the original:
     *   mag = (float)(sqrt(re*re + im*im) * (2/n))
     *   db  = mag > 1e-10f ? 20*log10(mag) : -120.0f
     */
    const float normFactor = 2.0f / (float)n;
    for (int i = 0; i < half; ++i) {
        const float mag = (float)(sqrt((double)re[i] * re[i] + (double)im[i] * im[i]) * normFactor);
        out_magnitudes_db[i] = mag > 1e-10f ? 20.0f * log10f(mag) : -120.0f;
    }

    free(re);
    free(im);
}

/* =========================================================================
 * Band aggregation (two-pointer)
 * ========================================================================= */

void acoustical_aggregate_bands(const float *band_frequencies, int band_count,
                                const float *magnitudes_db, int mag_count,
                                const float *bin_frequencies, int bin_count,
                                float noise_floor_db,
                                float *out_band_levels) {
    if (!band_frequencies || !bin_frequencies || !out_band_levels) return;

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
