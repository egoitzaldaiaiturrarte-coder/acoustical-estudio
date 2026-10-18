package com.rork.acoustical.domain.audio

/**
 * JNI bridge to the shared C DSP core (`dsp/acoustical_dsp.c`) — the exact
 * same FFT / band-aggregation code the Windows app links.
 *
 * The native library is loaded lazily on first use. When it is present (on
 * device) the pure-Kotlin [FftProcessor] and [RoomCorrector.aggregateBands]
 * delegate here, so both platforms run the identical, battle-tested C code.
 * When it is absent (e.g. pure-JVM unit tests, or a build without the native
 * lib) [isAvailable] is false and the pure-Kotlin implementations are used
 * instead — both paths are correct.
 *
 * FFT handles are opaque C pointers surfaced as [Long]; a failed create
 * returns 0L, which callers treat as "fall back to pure Kotlin".
 */
object NativeDsp {
    private const val LIB_NAME = "acoustical_dsp"

    @Volatile
    private var loaded: Boolean? = null

    /** True once the native DSP library has been loaded successfully. */
    val isAvailable: Boolean
        get() = loaded ?: try {
            System.loadLibrary(LIB_NAME)
            true
        } catch (t: Throwable) {
            false
        }.also { loaded = it }

    // --- FFT ---------------------------------------------------------------
    external fun nativeFftCreate(size: Int): Long
    external fun nativeFftFree(handle: Long)
    external fun nativeFftCompute(handle: Long, input: FloatArray, sampleRate: Int, out: FloatArray)
    external fun nativeFftBinFrequencies(handle: Long, sampleRate: Int, out: FloatArray)

    // --- Band aggregation --------------------------------------------------
    external fun nativeAggregateBands(
        bandFrequencies: FloatArray,
        magnitudesDb: FloatArray,
        binFrequencies: FloatArray,
        noiseFloorDb: Float,
        out: FloatArray
    )

    // --- Signal generator (shared stateful core, same as the Windows app) --
    //
    // `handle` is an opaque C pointer (0 = create failed -> use the pure
    // Kotlin fallback). Like the C API, every fill() takes the full
    // parameter snapshot: waveform (ACOUSTICAL_WAVE_*: 0=Silence, 1=Sine,
    // 2=BandSine, 3=LogSweep, 4=Pink, 5=White), frequency, amplitude, sweep
    // range/duration and the sample rate; the handle only keeps the phase /
    // pink filter / PRNG state, so the caller owns thread-safety.
    external fun nativeSiggenCreate(): Long
    external fun nativeSiggenFree(handle: Long)
    external fun nativeSiggenFill(
        handle: Long,
        waveform: Int,
        frequencyHz: Float,
        amplitude: Float,
        sweepStartHz: Float,
        sweepEndHz: Float,
        sweepSeconds: Float,
        sampleRate: Int,
        out: FloatArray
    )

    // --- Spatial position -> compensation (single C law, all 3 programs) --
    //
    // Fills [out] (length >= 2) with [0]=gain dB, [1]=delay ms, computed
    // from the normalized position (x/y in [-1,1], z distance 0..1 -> 0..15 m,
    // size 0..1). Identical to what the Windows app and the VST apply.
    external fun nativeSpatialCompensate(
        x: Float,
        y: Float,
        z: Float,
        size: Float,
        out: FloatArray
    )
}
