package com.rork.acoustical.domain.audio

import kotlin.math.max
import kotlin.math.pow

/**
 * Captures and stores a background noise profile.
 *
 * During a "noise capture" session (user is silent, only ambient noise is present),
 * the profiler accumulates FFT magnitude frames and computes a time-averaged
 * noise spectrum. This profile is then subtracted from subsequent measurements
 * so the room corrector only reacts to the actual source signal, not to
 * persistent background noise (HVAC, fans, traffic, etc.).
 *
 * The subtraction uses a spectral gate: for each frequency bin, if the measured
 * level is close to the noise floor (within [gateRange] dB), the bin is attenuated
 * proportionally. This avoids amplifying noise in quiet bands.
 */
class NoiseProfiler(
    private val binCount: Int,
    private val maxCaptureFrames: Int = 50,
    private val gateRange: Float = 6f
) {

    private val accumulatedMagnitudes = FloatArray(binCount)
    private var capturedFrameCount: Int = 0
    private var noiseProfile: FloatArray? = null
    private var isCapturing: Boolean = false

    /**
     * The final averaged noise floor spectrum in dB per FFT bin.
     * Null until capture is complete.
     */
    fun getNoiseProfile(): FloatArray? = noiseProfile?.copyOf()

    /**
     * Whether a noise profile has been captured and is available for subtraction.
     */
    fun hasProfile(): Boolean = noiseProfile != null

    /**
     * Begin a noise capture session. Resets accumulated data.
     */
    fun startCapture() {
        for (i in accumulatedMagnitudes.indices) {
            accumulatedMagnitudes[i] = 0f
        }
        capturedFrameCount = 0
        noiseProfile = null
        isCapturing = true
    }

    /**
     * Feed a frame of FFT magnitudes (in dB) during capture.
     * Returns true when capture is complete (reached [maxCaptureFrames]).
     */
    fun feedFrame(magnitudesDb: FloatArray): Boolean {
        if (!isCapturing) return false
        if (magnitudesDb.size != binCount) return false

        for (i in 0 until binCount) {
            accumulatedMagnitudes[i] += magnitudesDb[i]
        }
        capturedFrameCount++

        if (capturedFrameCount >= maxCaptureFrames) {
            finishCapture()
            return true
        }
        return false
    }

    private fun finishCapture() {
        val profile = FloatArray(binCount)
        for (i in 0 until binCount) {
            profile[i] = accumulatedMagnitudes[i] / capturedFrameCount
        }
        noiseProfile = profile
        isCapturing = false
    }

    fun cancelCapture() {
        isCapturing = false
        for (i in accumulatedMagnitudes.indices) {
            accumulatedMagnitudes[i] = 0f
        }
        capturedFrameCount = 0
    }

    fun isCapturing(): Boolean = isCapturing

    fun getCaptureProgress(): Float {
        if (maxCaptureFrames == 0) return 0f
        return (capturedFrameCount.toFloat() / maxCaptureFrames).coerceIn(0f, 1f)
    }

    /**
     * Subtract the noise profile from measured magnitudes using a spectral gate.
     *
     * For each bin:
     * - If measured >> noise (signal well above floor): keep as-is
     * - If measured ≈ noise (within gateRange dB): attenuate proportionally
     * - If measured < noise: set to noise floor (don't go below)
     *
     * @param magnitudesDb measured FFT magnitudes in dB
     * @return noise-subtracted magnitudes, or original if no profile
     */
    fun subtractNoise(magnitudesDb: FloatArray): FloatArray {
        val profile = noiseProfile ?: return magnitudesDb
        if (profile.size != magnitudesDb.size) return magnitudesDb

        val result = FloatArray(magnitudesDb.size)
        for (i in magnitudesDb.indices) {
            val measured = magnitudesDb[i]
            val noise = profile[i]

            if (measured <= noise) {
                // Signal at or below noise floor — gate it down
                result[i] = noise
            } else if (measured < noise + gateRange) {
                // Transition zone: proportional subtraction
                val ratio = (measured - noise) / gateRange
                val subtraction = noise * (1f - ratio)
                result[i] = max(noise, measured - subtraction)
            } else {
                // Signal well above noise — no subtraction needed
                result[i] = measured
            }
        }
        return result
    }

    /**
     * Get the noise profile as band levels (for display).
     */
    fun getNoiseBandLevels(
        bandFrequencies: FloatArray,
        binFrequencies: FloatArray
    ): FloatArray {
        val profile = noiseProfile ?: return FloatArray(bandFrequencies.size)
        val bandLevels = FloatArray(bandFrequencies.size)

        for (b in bandFrequencies.indices) {
            val center = bandFrequencies[b]
            val ratio = 2.0.pow(1.0 / 6.0)
            val lower = (center / ratio).toFloat()
            val upper = (center * ratio).toFloat()

            var sum = 0.0
            var count = 0
            for (i in binFrequencies.indices) {
                val freq = binFrequencies[i]
                if (freq >= lower && freq <= upper) {
                    if (i < profile.size) {
                        sum += profile[i]
                        count++
                    }
                }
                if (freq > upper) break
            }
            bandLevels[b] = if (count > 0) (sum / count).toFloat() else -120f
        }

        return bandLevels
    }

    fun clearProfile() {
        noiseProfile = null
    }
}
