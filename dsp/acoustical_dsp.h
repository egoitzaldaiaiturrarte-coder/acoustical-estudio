/* acoustical_dsp.h — Núcleo DSP compartido (C puro, sin dependencias).
 *
 * Es la única implementación de los algoritmos de señal de Acoustical Estudio:
 *   - FFT radix-2 Cooley-Tukey con ventana Hamming y magnitudes en dB
 *     (tablas de twiddles y bit-reversal, y el scratch real/imaginario,
 *     preasignados en create(): sin cos/sin ni malloc en el camino caliente).
 *   - Agregación de bins del FFT en bandas logarítmicas (dos punteros,
 *     O(bandas + bins) en vez de O(bandas * bins)).
 *   - Biquad peaking (cookbook RBJ) para el banco de EQ.
 *
 * Lo enlazan la app Windows (C++/JUCE) y la app Android (Kotlin vía JNI),
 * así las dos plataformas comparten exactamente el mismo código probado.
 *
 * C linkage para que tanto C++ como el glue JNI lo llamen sin mangling.
 *
 * Contrato de threads:
 *   - Un handle acoustical_fft es INMUTABLE tras create() (ventana,
 *     bit-reversal, twiddles y scratch fijos): compartir el handle entre
 *     hilos para leer (bin_count, bin_frequencies) es seguro.
 *   - compute_magnitudes_db escribe en el scratch INTERNO del handle, así que
 *     dos llamadas CONCURRENTES sobre el MISMO handle se enredarían; cada
 *     consumidor concurrente debe tener su propio handle (así se usa en la
 *     app: un handle por motor). Los punteros de entrada/salida los da el
 *     caller y no se tocan entre llamadas.
 *   - acoustical_biquad_process requiere estado EXCLUSIVO por caller: el
 *     array float state[2] lo provee y lo posee el caller (en el EQ vivo,
 *     solo el hilo de audio lo toca); compartir un state entre hilos es una
 *     carrera.
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
 * Create an FFT processor of the given size.
 *
 * Requirements: power of two and size >= 2 (size 1 would divide by (size-1)
 * in the window). Returns NULL on invalid size or OOM. Free with
 * acoustical_fft_free().
 */
acoustical_fft *acoustical_fft_create(int size);

void acoustical_fft_free(acoustical_fft *fft);

int acoustical_fft_bin_count(const acoustical_fft *fft);

/*
 * Run the FFT on `input` (>= size samples, mono, range [-1, 1]) and write the
 * magnitude in dB of each of the size/2 bins into `out_magnitudes_db`.
 *
 * Magnitudes are normalized 2*|X|/(n * 0.54): the 0.54 factor is the Hamming
 * window's coherent gain (sum of the window ≈ 0.54*n), so a full-scale (0
 * dBFS) sine reads 0 dB instead of ~-5.4 dB. If any input sample is not
 * finite (NaN/Inf) it is treated as 0.0 (one poisoned sample would otherwise
 * corrupt every bin of the frame and, downstream, the corrector and
 * sweepers). -120 dB floor for bins below 1e-10 linear.
 *
 * Thread contract: the handle's scratch is internal and shared, so calls
 * from different threads must use different handles (see the file header).
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

/*
 * Compute the coefficients of a peaking EQ. Writes 5 floats into `out`.
 *
 * Validación: si algún parámetro es inválido (sample_rate <= 0,
 * center_freq_hz <= 0, q <= 0, o cualquier NaN/Inf) se escribe un BYPASS
 * (b0 = 1, b1 = b2 = a1 = a2 = 0) y la llamada termina: coeficientes
 * corruptos envenenarían el banco de EQ en vivo. Con parámetros válidos se
 * clamp center_freq_hz a [10 Hz, 0.99*fs/2] y q a [0.1, 10] antes de
 * calcular. `out` puede ser NULL (no-op).
 */
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
