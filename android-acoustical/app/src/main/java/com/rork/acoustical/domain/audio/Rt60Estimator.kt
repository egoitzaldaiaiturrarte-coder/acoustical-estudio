package com.rork.acoustical.domain.audio

import kotlin.math.log10
import kotlin.math.pow

/**
 * Estimates reverberation time (RT60) from a sequence of band-level frames.
 *
 * RT60 is the time it takes for the sound pressure level to decrease by 60 dB
 * after the sound source stops. This implementation uses the energy decay
 * curve (EDC) via Schroeder backward integration:
 *
 *  - [feedFrame] converts each band's dB level to LINEAR ENERGY
 *    (10^(dB/10) — it is energy, not amplitude) and stores it in a ring
 *    buffer. IMPORTANT: the frame must hold AGGREGATED BAND levels (10/16/31/124
 *    bands depending on the active config), not raw FFT bins: an estimator
 *    built for N bands interprets the first N values of the frame as its N
 *    bands, so feeding raw bins made the "bands" be 0-18 Hz.
 *  - [computeRt60] builds the Schroeder EDC from the oldest frame:
 *    EDC[j] = sum of energies of frames j..n-1. It starts at the total
 *    energy and decays toward the latest frame; the dB curve is then linearly
 *    regressed against time and the slope (dB/ms) yields RT60 = 60/|slope|.
 */
class Rt60Estimator(
    val bandCount: Int = 8,
    private val historySize: Int = 64,
    private val sampleIntervalMs: Long = 50L
) {

    /** Ring buffer of linear band energy per frame */
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
     * For each band, build the EDC from the oldest frame (EDC[j] = sum of
     * energies of frames j..n-1: total energy at the start, decaying toward
     * the latest frame), then fit a line to the log of that curve to find
     * the decay rate.
     */
    private fun computeRt60() {
        var sumRt60 = 0f
        var validBands = 0

        for (b in 0 until bandCount) {
            val history = energyHistory[b]
            val n = framesCollected

            if (n < 4) continue

            // Schroeder EDC: edc[j] = sum of the energies of frames j..n-1.
            // The oldest frame (ring slot writeIndex-n) is edc's anchor: it
            // holds the TOTAL energy, so the dB curve starts high and decays —
            // the direction a real EDC has. (The previous code accumulated
            // from the LATEST frame backwards, producing a curve that GREW
            // with time, whose slope was always positive and therefore always
            // rejected — the estimator could never output a decay.)
            val edc = FloatArray(n)
            var cumulative = 0f
            for (j in n - 1 downTo 0) {
                val idx = (writeIndex - n + j + historySize) % historySize
                cumulative += history[idx]
                edc[j] = cumulative
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

    /**
     * dB -> linear ENERGY. The Schroeder integral integrates energy, so the
     * correct conversion is 10^(dB/10) (a 10 dB drop = 10x less energy).
     * The previous 20·log10 (amplitude) version under-weighted the decay and
     * made the fitted slope half as steep as it really was.
     */
    private fun dbToLinear(db: Float): Float = 10.0.pow(db / 10.0).toFloat()

    /** Linear energy -> dB (inverse of [dbToLinear]). */
    private fun linearToDb(linear: Float): Float =
        if (linear > 0f) (10.0 * log10(linear.toDouble())).toFloat() else -120f
}
