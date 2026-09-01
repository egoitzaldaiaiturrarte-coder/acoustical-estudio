package com.rork.acoustical.domain.model

import kotlin.math.pow
import kotlinx.serialization.Serializable

/**
 * Supported sample rates for audio analysis.
 * Higher rates capture more frequency detail at the cost of CPU and battery.
 */
@Serializable
enum class SampleRate(val hz: Int, val label: String) {
    SR_44100(44100, "44.1 kHz"),
    SR_48000(48000, "48 kHz"),
    SR_88200(88200, "88.2 kHz"),
    SR_96000(96000, "96 kHz");

    val nyquist: Int get() = hz / 2
}

/**
 * FFT window size — determines frequency resolution.
 * Larger windows give finer frequency bins but slower time response.
 */
@Serializable
enum class FftSize(val samples: Int, val label: String) {
    SIZE_512(512, "512"),
    SIZE_1024(1024, "1K"),
    SIZE_2048(2048, "2K"),
    SIZE_4096(4096, "4K"),
    SIZE_8192(8192, "8K");

    val binCount: Int get() = samples / 2
}

/**
 * Analysis interval in milliseconds.
 * Controls how often the engine runs FFT + correction, balancing resource usage.
 */
@Serializable
enum class AnalysisInterval(val ms: Long, val label: String) {
    FAST(25, "Fast (25 ms)"),
    NORMAL(50, "Normal (50 ms)"),
    BALANCED(100, "Balanced (100 ms)"),
    ECO(200, "Eco (200 ms)"),
    POWER_SAVER(500, "Power Saver (500 ms)")
}

/**
 * Number of EQ bands for the dynamic equalizer.
 */
@Serializable
enum class BandCount(val count: Int, val label: String) {
    BANDS_8(8, "8"),
    BANDS_10(10, "10"),
    BANDS_16(16, "16"),
    BANDS_31(31, "31 · 1/3 oct"),
    BANDS_124(124, "124 · Ultra")
}

/**
 * Central configuration for the audio correction engine.
 */
@Serializable
data class AudioConfig(
    val sampleRate: SampleRate = SampleRate.SR_48000,
    val fftSize: FftSize = FftSize.SIZE_2048,
    val analysisInterval: AnalysisInterval = AnalysisInterval.NORMAL,
    val bandCount: BandCount = BandCount.BANDS_10,
    val correctionEnabled: Boolean = true,
    val maxGainDb: Float = 12f,
    val targetSpl: Float = 75f,
    val smoothingFactor: Float = 0.3f,
    val noiseFloorDb: Float = -80f,
    val noiseSubtractionEnabled: Boolean = true,
    val audioDelayMs: Float = 25f,
    val geoAutoAdjust: Boolean = false,
    val scenarioPreset: String = "CUSTOM"
) {
    /**
     * Effective smoothing for the corrector. With high correction limits the
     * adaptation is automatically relaxed to avoid oscillation: above 24 dB
     * of limit the factor scales down proportionally.
     */
    val effectiveSmoothingFactor: Float
        get() = if (maxGainDb > 24f) {
            (smoothingFactor * 24f / maxGainDb).coerceAtLeast(0.05f)
        } else {
            smoothingFactor
        }

    val needsExtendedSmoothing: Boolean get() = maxGainDb > 24f

    companion object {
        val Default = AudioConfig()
    }
}

/**
 * A single equalizer band with its center frequency and current gain.
 */
@Serializable
data class EqBand(
    val index: Int,
    val centerFreq: Float,
    val gainDb: Float = 0f,
    val targetGainDb: Float = 0f,
    val q: Float = 1.41f
) {
    /** True when the band sits at the configured correction limit. */
    fun isClampedAt(maxGainDb: Float): Boolean =
        maxGainDb > 0f && kotlin.math.abs(gainDb) >= maxGainDb - 0.5f
}

/**
 * Standard 1/3 octave center frequencies from 20 Hz to 20 kHz.
 */
object StandardFrequencies {
    val thirdOctave: FloatArray = floatArrayOf(
        20f, 25f, 31.5f, 40f, 50f, 63f, 80f, 100f, 125f, 160f,
        200f, 250f, 315f, 400f, 500f, 630f, 800f, 1000f, 1250f, 1600f,
        2000f, 2500f, 3150f, 4000f, 5000f, 6300f, 8000f, 10000f, 12500f, 16000f, 20000f
    )

    val tenBand: FloatArray = floatArrayOf(
        31.25f, 62.5f, 125f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 16000f
    )

    val eightBand: FloatArray = floatArrayOf(
        60f, 170f, 310f, 600f, 1000f, 3000f, 6000f, 12000f
    )

    val sixteenBand: FloatArray = floatArrayOf(
        40f, 63f, 100f, 160f, 250f, 400f, 630f, 1000f,
        1600f, 2500f, 4000f, 6300f, 8000f, 10000f, 14000f, 16000f
    )

    /**
     * 124 log-spaced bands from 20 Hz to 20 kHz (ultra resolution).
     * Generated so adjacent bands keep a constant ratio of 1000^(1/123).
     * Recompute eagerly once — cheap (124 pow calls) and thread-safe.
     */
    val ultra124: FloatArray = FloatArray(124) { i ->
        (20.0 * 1000.0.pow(i / 123.0)).toFloat()
    }

    fun forCount(count: BandCount): FloatArray = when (count) {
        BandCount.BANDS_8 -> eightBand
        BandCount.BANDS_10 -> tenBand
        BandCount.BANDS_16 -> sixteenBand
        BandCount.BANDS_31 -> thirdOctave
        BandCount.BANDS_124 -> ultra124
    }
}

/**
 * A snapshot of the frequency spectrum at a point in time.
 */
data class SpectrumFrame(
    val magnitudesDb: FloatArray,
    val frequencies: FloatArray,
    val timestampMs: Long
) {
    val size: Int get() = magnitudesDb.size
}

/**
 * Room calibration profile — the measured acoustic signature of a space.
 */
@Serializable
data class RoomProfile(
    val name: String,
    val description: String = "",
    val measuredGains: List<Float> = emptyList(),
    val centerFrequencies: List<Float> = emptyList(),
    val calibratedSpl: Float = 0f,
    val createdAt: Long = 0L
) {
    companion object {
        val Empty = RoomProfile(name = "", description = "")
    }
}

/**
 * SPL calibration data for converting digital levels to real-world dB SPL.
 */
@Serializable
data class SplCalibration(
    val referenceSpl: Float = 94f,
    val measuredDbfs: Float = -26f,
    val offsetDb: Float = 0f,
    val isCalibrated: Boolean = false
) {
    val conversionOffset: Float get() = referenceSpl - measuredDbfs + offsetDb
}

/**
 * Overall engine state surfaced to the UI.
 */
data class EngineState(
    val isRunning: Boolean = false,
    val isCorrecting: Boolean = false,
    val currentSpl: Float = 0f,
    val peakSpl: Float = 0f,
    val averageSpl: Float = 0f,
    val referenceSpectrum: SpectrumFrame? = null,
    val measuredSpectrum: SpectrumFrame? = null,
    val correctedSpectrum: SpectrumFrame? = null,
    val bands: List<EqBand> = emptyList(),
    val correctionActive: Float = 0f,
    val cpuLoadPercent: Float = 0f,
    val framesAnalyzed: Long = 0L,
    val rt60Ms: Float = 0f,
    val geoLocationLabel: String = "",
    val geoAdjustmentApplied: Float = 0f
)
