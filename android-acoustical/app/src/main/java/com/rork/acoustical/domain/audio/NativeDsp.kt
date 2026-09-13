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
}
