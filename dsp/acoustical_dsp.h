/* acoustical_dsp.h — Núcleo DSP compartido (C puro, sin dependencias).
 *
 * Es la única implementación de los algoritmos de señal de Acoustical Estudio:
 *   - FFT radix-2 Cooley-Tukey con ventana Hamming y magnitudes en dB
 *     (tabla de twiddles precomputada: sin cos/sin en el camino caliente).
 *   - Agregación de bins del FFT en bandas logarítmicas (dos punteros,
 *     O(bandas + bins) en vez de O(bandas * bins)).
 *   - Biquad peaking (cookbook RBJ) para el banco de EQ.
 *
 * Lo enlazan la app Windows (C++/JUCE) y la app Android (Kotlin vía JNI),
 * así las dos plataformas comparten exactamente el mismo código probado.
 *
 * C linkage para que tanto C++ como el glue JNI lo llamen sin mangling.
 */
#ifndef ACOUSTICAL_DSP_H
#define ACOUSTICAL_DSP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * FFT
 * ========================================================================= */

/* Opaque handle. Create once per FFT size, reuse for every frame. */
typedef struct acoustical_fft acoustical_fft;

/*
 * Create an FFT processor of the given size (must be a power of two, 512-8192).
 * Precomputes the Hamming window, the bit-reversal table and the twiddle
 * table. Returns NULL on invalid size. Free with acoustical_fft_free().
 */
acoustical_fft *acoustical_fft_create(int size);

void acoustical_fft_free(acoustical_fft *fft);

int acoustical_fft_bin_count(const acoustical_fft *fft);

/*
 * Run the FFT on `input` (>= size samples, mono, range [-1, 1]) and write the
 * magnitude in dB of each of the size/2 bins into `out_magnitudes_db`.
 * Identical to the original per-frame computation (Hamming window, bit
 * reversal, Cooley-Tukey, 2*|X|/n normalization, -120 dB floor).
 */
void acoustical_fft_compute_magnitudes_db(acoustical_fft *fft,
                                          const float *input,
                                          int sample_rate,
                                          float *out_magnitudes_db);

/*
 * Write the frequency (Hz) of each of the size/2 bins into `out_bin_freqs`.
 */
void acoustical_fft_bin_frequencies(const acoustical_fft *fft,
                                    int sample_rate,
                                    float *out_bin_freqs);

/* =========================================================================
 * Band aggregation
 * ========================================================================= */

/*
 * Aggregate raw FFT bins into perceptual bands.
 *
 * `band_frequencies` (band_count entries, ascending center frequencies),
 * `magnitudes_db` (mag_count entries) and `bin_frequencies` (bin_count
 * entries, ascending) describe the data. For each band the bins whose
 * frequency lies in [center/ratio, center*ratio] and whose magnitude is above
 * `noise_floor_db` are averaged; a band with no qualifying bin gets
 * `noise_floor_db`. `ratio` is 2^(1/12) when band_count > 40 else 2^(1/6),
 * matching the platform implementations.
 *
 * Two-pointer scan: O(band_count + bin_count) total.
 */
void acoustical_aggregate_bands(const float *band_frequencies, int band_count,
                                const float *magnitudes_db, int mag_count,
                                const float *bin_frequencies, int bin_count,
                                float noise_floor_db,
                                float *out_band_levels);

/* =========================================================================
 * Biquad peaking EQ (RBJ cookbook)
 * ========================================================================= */

/* Coefficients: b0, b1, b2, a1, a2 (5 floats). */
typedef struct {
    float b0, b1, b2, a1, a2;
} acoustical_biquad_coeffs;

/* Compute the coefficients of a peaking EQ. Writes 5 floats into `out`. */
void acoustical_make_peaking(float center_freq_hz, float sample_rate,
                             float gain_db, float q,
                             acoustical_biquad_coeffs *out);

/*
 * Process one sample through a biquad. `state` is a 2-float array (z1, z2)
 * owned by the caller (zero it for a fresh filter). Returns the output sample.
 */
float acoustical_biquad_process(const acoustical_biquad_coeffs *c,
                                float state[2], float x);

#ifdef __cplusplus
}
#endif

#endif /* ACOUSTICAL_DSP_H */
