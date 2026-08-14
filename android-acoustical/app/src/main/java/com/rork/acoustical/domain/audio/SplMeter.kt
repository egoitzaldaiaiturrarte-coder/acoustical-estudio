package com.rork.acoustical.domain.audio

import kotlin.math.log10
import kotlin.math.sqrt

/**
 * Sound Pressure Level (SPL) meter.
 * Converts digital audio samples (dBFS) to real-world SPL (dB) using a calibration offset.
 *
 * Supports A-weighting (perceptual) and Z-weighting (flat/linear).
 */
class SplMeter(private val calibrationOffset: Float = 120f) {

    private var peakSpl: Float = 0f
    private var splHistory: FloatArray = FloatArray(HISTORY_SIZE)
    private var historyIndex: Int = 0
    private var historyCount: Int = 0

    /**
     * Compute SPL from a buffer of audio samples.
     *
     * @param samples mono audio samples in range [-1, 1]
     * @param weighting "A" for A-weighting, "Z" for flat
     * @return SPL in dB
     */
    fun computeSpl(samples: FloatArray, weighting: Weighting = Weighting.A): Float {
        if (samples.isEmpty()) return 0f

        // Compute RMS
        var sumSquares = 0.0
        for (sample in samples) {
            sumSquares += sample.toDouble() * sample.toDouble()
        }
        val rms = sqrt(sumSquares / samples.size)

        // Convert to dBFS
        val dbfs = if (rms > 1e-10) 20.0 * log10(rms) else -120.0

        // Apply weighting
        val weightedDbfs = when (weighting) {
            Weighting.A -> dbfs + 0.0 // Simplified: full A-weighting would require frequency-domain filtering
            Weighting.Z -> dbfs
        }

        // Convert to SPL
        val spl = (weightedDbfs + calibrationOffset).toFloat().coerceIn(0f, 200f)

        // Update peak
        if (spl > peakSpl) peakSpl = spl

        // Update history for averaging
        splHistory[historyIndex] = spl
        historyIndex = (historyIndex + 1) % HISTORY_SIZE
        if (historyCount < HISTORY_SIZE) historyCount++

        return spl
    }

    fun getPeakSpl(): Float = peakSpl

    fun getAverageSpl(): Float {
        if (historyCount == 0) return 0f
        var sum = 0f
        for (i in 0 until historyCount) {
            sum += splHistory[i]
        }
        return sum / historyCount
    }

    fun resetPeak() {
        peakSpl = 0f
    }

    fun resetHistory() {
        historyIndex = 0
        historyCount = 0
        peakSpl = 0f
    }

    enum class Weighting { A, Z }

    companion object {
        private const val HISTORY_SIZE = 100

        /**
         * A-weighting correction factors for 1/3 octave bands.
         * Used when computing perceptual SPL from band data.
         */
        val aWeightingDb: FloatArray = floatArrayOf(
            -50.5f, -44.7f, -39.4f, -34.6f, -30.2f, -26.2f, -22.5f, -19.1f, -16.1f, -13.4f,
            -10.9f, -8.6f, -6.6f, -4.8f, -3.2f, -1.9f, -0.8f, 0.0f, 0.6f, 1.0f,
            1.2f, 1.3f, 1.2f, 1.0f, 0.5f, -0.1f, -1.1f, -2.5f, -4.3f, -6.6f, -9.3f
        )
    }
}
