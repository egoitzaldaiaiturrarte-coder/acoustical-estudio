package com.rork.acoustical.domain.audio

import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Sound Pressure Level (SPL) meter.
 * Converts digital audio levels (dBFS) to real-world SPL (dB) using a calibration
 * offset that can be changed at runtime ([calibrationOffset], e.g. after a
 * calibration — it does NOT require rebuilding the meter, so peak/history state
 * is preserved).
 *
 * Two measurements are available:
 *  - [computeSpl]: flat (Z) level of a time-domain sample buffer (RMS).
 *  - [computeWeightedSpl]: A-weighted level of a SPECTRUM (per-bin energy
 *    weighting with the IEC 61672 A curve, approximated by the 1/3-octave
 *    [aWeightingDb] table with log-frequency interpolation — a band-table
 *    approximation of the continuous A filter). The engine feeds this one, so
 *    the displayed SPL is what the ear actually perceives.
 */
class SplMeter(var calibrationOffset: Float = 120f) {

    private var peakSpl: Float = 0f
    private var splHistory: FloatArray = FloatArray(HISTORY_SIZE)
    private var historyIndex: Int = 0
    private var historyCount: Int = 0

    /**
     * Compute SPL from a buffer of audio samples.
     *
     * Flat (Z) measurement: time-domain RMS. For A-weighting use
     * [computeWeightedSpl] instead — weighting in the frequency domain is
     * what the ear sensitivity curve actually is; there is no equivalent
     * time-domain scalar for it.
     *
     * @param samples mono audio samples in range [-1, 1]
     * @param weighting historical API: only Z is meaningful here
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

        // A-weighting is a FREQUENCY-domain response: in the time domain the
        // level is flat (Z). The engine therefore reports A through
        // [computeWeightedSpl] over the spectrum.
        val spl = (dbfs + calibrationOffset).toFloat().coerceIn(0f, 200f)

        record(spl)
        return spl
    }

    /**
     * A-weighted SPL from a SPECTRUM (the perceptually correct measurement).
     *
     * The A curve is applied as a per-bin ENERGY weight: the weighted energy
     * is the sum over bins of 10^((mag_i + A(f_i))/10) and the level is its
     * 10·log10. A(f) is the IEC 61672 A-weighting at 1/3-octave centers
     * ([aWeightingDb], 20 Hz – 20 kHz) interpolated in log-frequency — a
     * band-table approximation of the continuous A filter (a true A would be
     * a biquad cascade; this is the standard published 1/3-oct table).
     *
     * @param magnitudesDb dB magnitude per FFT bin
     * @param binFrequencies Hz of each FFT bin (same length as magnitudesDb;
     *   bins at 0 Hz carry no energy and are skipped)
     * @return A-weighted SPL in dB
     */
    fun computeWeightedSpl(magnitudesDb: FloatArray, binFrequencies: FloatArray): Float {
        if (magnitudesDb.isEmpty()) return 0f

        var energy = 0.0
        for (i in magnitudesDb.indices) {
            val f = if (i < binFrequencies.size) binFrequencies[i] else 0f
            if (f <= 0f) continue // bin DC: no energy, no weighting
            val aWeightDb = aWeightingAt(f)
            energy += 10.0.pow((magnitudesDb[i] + aWeightDb) / 10.0)
        }

        val weightedDbfs = if (energy > 1e-12) 10.0 * log10(energy) else -120.0
        val spl = (weightedDbfs + calibrationOffset).toFloat().coerceIn(0f, 200f)

        record(spl)
        return spl
    }

    /**
     * A-weighting (dB) at [freqHz]: log-frequency interpolation between the
     * 1/3-octave centers of [aWeightingFrequencies] (which lock-step with
     * [aWeightingDb]). Frequencies outside 20 Hz – 20 kHz clamp to the
     * table edges (no published data beyond them).
     */
    private fun aWeightingAt(freqHz: Float): Double {
        val freqs = aWeightingFrequencies
        val weights = aWeightingDb
        if (freqHz <= freqs[0]) return weights[0].toDouble()
        if (freqHz >= freqs.last()) return weights.last().toDouble()

        // Binary search of the bracketing pair [freqs[lo], freqs[hi]].
        var lo = 0
        var hi = freqs.size - 1
        while (hi - lo > 1) {
            val mid = (lo + hi) ushr 1
            if (freqs[mid] <= freqHz) lo = mid else hi = mid
        }
        val f0 = freqs[lo].toDouble()
        val f1 = freqs[hi].toDouble()
        val frac = (ln(freqHz.toDouble()) - ln(f0)) / (ln(f1) - ln(f0))
        return weights[lo] + (weights[hi] - weights[lo]) * frac
    }

    /** Updates peak and running history (shared by both measurements). */
    private fun record(spl: Float) {
        if (spl > peakSpl) peakSpl = spl
        splHistory[historyIndex] = spl
        historyIndex = (historyIndex + 1) % HISTORY_SIZE
        if (historyCount < HISTORY_SIZE) historyCount++
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

        /**
         * Center frequencies (Hz) of the 31 1/3-octave bands of [aWeightingDb]
         * — the standard published table from 20 Hz to 20 kHz. Both arrays
         * are lock-step: aWeightingDb[i] is the A-weighting at
         * aWeightingFrequencies[i].
         */
        val aWeightingFrequencies: FloatArray = floatArrayOf(
            20f, 25f, 31.5f, 40f, 50f, 63f, 80f, 100f, 125f, 160f,
            200f, 250f, 315f, 400f, 500f, 630f, 800f, 1000f, 1250f, 1600f,
            2000f, 2500f, 3150f, 4000f, 5000f, 6300f, 8000f, 10000f, 12500f, 16000f, 20000f
        )
    }
}
