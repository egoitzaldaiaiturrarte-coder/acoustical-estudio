package com.rork.acoustical.domain.audio

import kotlin.math.exp
import kotlin.math.ln

/**
 * Estimates reverberation time (RT60) from a sequence of spectrum frames.
 *
 * RT60 is the time it takes for the sound pressure level to decrease by 60 dB
 * after the sound source stops. This implementation uses the energy decay
 * curve (EDC) method via Schroeder backward integration.
 *
 * The estimator maintains a history of band energy levels and computes
 * the decay slope to estimate RT60 in milliseconds.
 */
class Rt60Estimator(
    private val bandCount: Int = 8,
    private val historySize: Int = 64,
    private val sampleIntervalMs: Long = 50L
) {

    /** Ring buffer of total energy per frame */
    private val energyHistory = Array(bandCount) { FloatArray(historySize) }
    private var writeIndex = 0
    private var framesCollected = 0

    /** Estimated RT60 in ms per band */
    private val rt60PerBand = FloatArray(bandCount)

    /** Overall estimated RT60 in ms */
    var currentRt60Ms: Float = 0f
        private set

    /**
     * Feed a new frame of band magnitudes (in dB).
     * The estimator converts dB to linear energy, stores it,
     * and recomputes the RT60 estimate.
     */
    fun feedFrame(bandMagnitudesDb: FloatArray) {
        val bands = minOf(bandMagnitudesDb.size, bandCount)

        for (b in 0 until bands) {
            val linearEnergy = dbToLinear(bandMagnitudesDb[b])
            energyHistory[b][writeIndex] = linearEnergy
        }

        writeIndex = (writeIndex + 1) % historySize
        if (framesCollected < historySize) framesCollected++

        if (framesCollected >= 8) {
            computeRt60()
        }
    }

    /**
     * Compute RT60 via Schroeder backward integration.
     * For each band, integrate energy backwards from the latest frame,
     * then fit a line to the log of the integrated curve to find the decay rate.
     */
    private fun computeRt60() {
        var sumRt60 = 0f
        var validBands = 0

        for (b in 0 until bandCount) {
            val history = energyHistory[b]
            val n = framesCollected

            if (n < 4) continue

            // Build Schroeder backward integration (cumulative sum from latest to oldest)
            val edc = FloatArray(n)
            var cumulative = 0f

            for (i in 0 until n) {
                val idx = (writeIndex - 1 - i + historySize) % historySize
                cumulative += history[idx]
                edc[i] = cumulative
            }

            // Normalize and convert to dB
            if (edc[0] <= 0f) continue
            val maxEdc = edc[0]
            for (i in edc.indices) {
                edc[i] = if (edc[i] > 0f) linearToDb(edc[i] / maxEdc) else -120f
            }

            // Fit linear regression on EDC: time vs dB
            // Time axis: i * sampleIntervalMs, from 0 to (n-1) * sampleIntervalMs
            // We only use the first portion of the curve (first 2/3) to avoid noise floor
            val useCount = maxOf(4, (n * 2) / 3)

            var sumX = 0.0
            var sumY = 0.0
            var sumXY = 0.0
            var sumXX = 0.0

            for (i in 0 until useCount) {
                val t = i * sampleIntervalMs.toDouble()
                val y = edc[i].toDouble()
                sumX += t
                sumY += y
                sumXY += t * y
                sumXX += t * t
            }

            val denom = useCount.toDouble() * sumXX - sumX * sumX
            if (denom == 0.0) continue

            // Slope in dB per ms
            val slope = (useCount.toDouble() * sumXY - sumX * sumY) / denom

            if (slope >= 0f) continue // Not decaying

            // RT60 = time to decay 60 dB = -60 / slope
            val rt60 = (-60.0 / slope).toFloat()

            if (rt60 in 10f..10000f) {
                rt60PerBand[b] = rt60
                sumRt60 += rt60
                validBands++
            }
        }

        currentRt60Ms = if (validBands > 0) sumRt60 / validBands else currentRt60Ms
    }

    /**
     * Get RT60 per band for display.
     */
    fun getRt60PerBand(): FloatArray = rt60PerBand.copyOf()

    fun reset() {
        for (b in 0 until bandCount) {
            energyHistory[b].fill(0f)
            rt60PerBand[b] = 0f
        }
        writeIndex = 0
        framesCollected = 0
        currentRt60Ms = 0f
    }

    private fun dbToLinear(db: Float): Float = exp((db / 8.6858896f).toDouble()).toFloat()

    private fun linearToDb(linear: Float): Float = if (linear > 0f) 8.6858896f * ln(linear.toDouble()).toFloat() else -120f
}
