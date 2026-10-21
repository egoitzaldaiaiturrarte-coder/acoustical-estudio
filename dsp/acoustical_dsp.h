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

/* =========================================================================
 * Signal generator (sine, log sweep, pink/white noise, silence)
 *
 * Shared by the Windows app (app/plugin, via the C++ SignalGenerator wrapper)
 * and Android (Kotlin via JNI): the EXACT same generation code — phase
 * accumulation of the sine/log-sweep, Paul Kellet's pink filter and the white
 * noise source — runs on both platforms, and both are validated against this
 * core by the tests (dsp/test_siggen.cpp, dsp/test_jni.cpp).
 *
 * Waveforms (canonical values; every platform's menu maps to these):
 *   0 = Silence — the FIRST entry of every generator menu: the first
 *     option of every program and the only one that never sounds.
 *     Picking any other waveform must make the program sound immediately.
 *   1 = Sine, 2 = BandSine (a per-band sine, same generation as Sine),
 *   3 = LogSweep, 4 = Pink, 5 = White.
 *
 * Thread contract: a handle is owned by ONE consumer (the audio thread in
 * the Windows app, a worker coroutine on Android). The parameters are passed
 * to every fill() call — the caller takes its own snapshot of its controls
 * (atomics, config…) — so no parameter storage is shared between threads.
 * The internal state (phases, pink filter, RNG) is private to the handle.
 * Changing the waveform between two calls resets the internal state, so
 * returning to Pink never leaves a level transient (same rule the C++
 * wrapper applied before the unification).
 * ========================================================================= */

/* Values canonicos de forma de onda. En los tres programas el 0 (Silencio)
 * es siempre la PRIMERA entrada de su menu generador; cada plataforma mapea
 * su propio menu a estos valores (en el header de cada generador). */
enum {
    ACOUSTICAL_WAVE_SILENCE   = 0,
    ACOUSTICAL_WAVE_SINE      = 1,
    ACOUSTICAL_WAVE_BAND_SINE = 2,
    ACOUSTICAL_WAVE_LOG_SWEEP = 3,
    ACOUSTICAL_WAVE_PINK      = 4,
    ACOUSTICAL_WAVE_WHITE     = 5
};

typedef struct acoustical_siggen acoustical_siggen;

/* Create/free a generator. create returns NULL on OOM; free(NULL) is safe. */
acoustical_siggen *acoustical_siggen_create(void);
void acoustical_siggen_free(acoustical_siggen *g);

/*
 * Fill `out` with `count` mono samples in [-1, 1] * amplitude.
 *
 *   waveform        one of the ACOUSTICAL_WAVE_* values above.
 *   frequencyHz     frequency of Sine/BandSine (ignored by the rest).
 *   amplitude       linear amplitude (1.0 = full scale); the caller converts
 *                   dB (10^(db/20)) and pre-scaled values — the core keeps
 *                   one way to express level so every platform agrees.
 *   sweep*          LogSweep range in Hz and duration in seconds; a
 *                   non-positive duration falls back to 5 s.
 *   sampleRate      must be > 0 and finite; a corrupted rate silences the
 *                   block instead of producing Inf/NaN phase increments.
 *
 * Sine/BandS integrate the phase sample by sample (exact instantaneous
 * frequency, no aliasing); LogSweep integrates f(t) = f0·(f1/f0)^(t/T) the
 * same way; Pink is Paul Kellet's 7-tap -3 dB/octave filter; White is a
 * portable PRNG in [-1, 1).
 */
void acoustical_siggen_fill(acoustical_siggen *g,
                            int waveform,
                            float frequencyHz,
                            float amplitude,
                            float sweepStartHz,
                            float sweepEndHz,
                            float sweepSeconds,
                            float sampleRate,
                            float *out,
                            int count);

/* =========================================================================
 * Spatial position → SPL compensation (gain + delay)
 *
 * Same law in every program (Windows app, VST, Android): the position is a
 * normalized point (x/y lateral-frontal, z distance, size element) and the
 * compensation is the inverse-square-law gain plus the propagation delay at
 * the speed of sound — so a "far and big" element and a "close and small"
 * one sound at the same level with aligned timing in whichever program plays
 * them. One implementation here means the three versions cannot drift.
 * ========================================================================= */

/* z = 1 is this many meters; the NEUTRAL position (x = 0, z = 0.5, size =
 * 0.5 — the default of the three programs) is the reference of the law. */
#define ACOUSTICAL_SPATIAL_MAX_METERS 15.0f
#define ACOUSTICAL_SPATIAL_NEUTRAL_METERS 7.5f

/*
 * Relative to the neutral position (which yields exactly 0 dB / 0 ms):
 *
 * out_gain_db = 20·log10(max(z·15 m, 1 m) / 7.5 m)
 *               + (0.5 − size)·6 dB (larger element = more efficient)
 *               + |x|·1.5 dB (off-axis);
 * out_delay_ms = max(z·15 m − 7.5 m, 0)/343 m/s · 1000.
 *
 * Range: −20.5 dB (z ≈ 0, size 1) … +10.5 dB (z = 1, size 0, |x| = 1);
 * 0 … 21.9 ms.
 *
 * Inputs are clamped (x,y ∈ [-1,1]; z,size ∈ [0,1]); non-finite inputs
 * return 0 dB / 0 ms instead of poisoning a live output chain.
 */
void acoustical_spatial_compensate(float x, float y, float z, float size,
                                    float *out_gain_db, float *out_delay_ms);

/* =========================================================================
 * Work modes (catalog of startup profiles)
 *
 * The six work modes (Coche Normal / Coche Aire libre / Estudio-Escenario
 * 1 / Escenario 2 / Escenario 3 / PAs) and their FULL startup settings:
 * motor (SPL, gain limit, bands, FFT, analysis, smoothing, correction,
 * noise subtraction, delay), the three dynamic EQs (on/off, interval,
 * gain, mixer, speed, extra sweeps, start direction and the four
 * free-frequency support bands of each) and the spatial position.
 *
 * One C table here; the Windows app and the VST read it directly, Android
 * keeps the same values in its Kotlin catalog (port, like the rest of the
 * shared models) and both sides are pinned by tests (dsp/test_modes.cpp and
 * the Android unit tests) so the three versions cannot drift.
 * ========================================================================= */

/* One free-frequency support band of a dynamic EQ. */
typedef struct {
    float frequencyHz;
    float gainDb;
    float q;
} acoustical_support_band;

/* The full settings of one dynamic EQ within a mode. */
typedef struct {
    int enabled;
    int intervalMs;
    float maxGainDb;
    float mixerLevel;
    float speedMultiplier;
    int extraSweeps;
    int startFrom;  /* 0 = where needed, 1 = bass-up, 2 = treble-down */
    acoustical_support_band support[4];
} acoustical_eq_profile;

/* The complete, editable, persistent profile of one work mode. */
typedef struct {
    const char *id;
    const char *name;
    /* Motor */
    float targetSpl;
    float maxGainDb;
    int bandCountIndex;        /* 0=8, 1=10, 2=16, 3=31, 4=124 */
    int fftSizeIndex;          /* 0=512, 1=1024, 2=2048, 3=4096, 4=8192 */
    int analysisIntervalIndex; /* 0=25 ms … 4=500 ms */
    float smoothingFactor;
    int correctionEnabled;
    int noiseSubtractionEnabled;
    float audioDelayMs;
    /* The three dynamic EQs (same corrector, different settings) */
    acoustical_eq_profile eqs[3];
    /* Spatial position (normalized; see acoustical_spatial_compensate) */
    float spatialX;
    float spatialY;
    float spatialZ;
    float spatialSize;
    /* Reference generator: waveform + level (Silence = the default, the
     * generator stays off and the input passes through) */
    int generatorWaveform;
    float generatorLevelDb;
} acoustical_mode_profile;

#define ACOUSTICAL_MODE_COUNT 6

/* The catalog (static, valid for the life of the process). */
const acoustical_mode_profile *acoustical_mode_profiles(void);
/* Look up one mode by id ("carro-normal", "pas", …); NULL when unknown. */
const acoustical_mode_profile *acoustical_mode_by_id(const char *id);

#ifdef __cplusplus
}
#endif

#endif /* ACOUSTICAL_DSP_H */
