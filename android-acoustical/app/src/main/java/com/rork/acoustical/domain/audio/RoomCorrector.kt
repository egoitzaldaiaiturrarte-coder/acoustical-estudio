package com.rork.acoustical.domain.audio

import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.domain.model.SpectrumFrame
import com.rork.acoustical.domain.model.StandardFrequencies
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.pow

/**
 * Analyzes the difference between a reference spectrum (what the source sends)
 * and the measured spectrum (what the microphone receives in the room),
 * then computes EQ band gains to compensate for room-induced coloration.
 *
 * The algorithm:
 * 1. Aggregate FFT bins into 1/N-octave bands centered on standard frequencies.
 * 2. Compute the deviation: measured - reference for each band.
 * 3. Invert the deviation to produce correction gains (negative gain for peaks,
 *    positive gain for dips).
 * 4. Apply smoothing and clamping to avoid oscillation and extreme boosts.
 */
class RoomCorrector(
    private val bandFrequencies: FloatArray,
    private val sampleRate: Int,
    private val fftBinCount: Int,
    private val maxGainDb: Float,
    private val smoothingFactor: Float,
    private val noiseFloorDb: Float
) {

    private val bandCount: Int = bandFrequencies.size
    private val previousGains = FloatArray(bandCount)
    private var currentBandLevels = FloatArray(bandCount)

    /**
     * Aggregate raw FFT bins into perceptual bands using log-spaced center frequencies.
     *
     * @param magnitudesDb dB magnitudes from the FFT (size = fftBinCount)
     * @param binFrequencies frequency in Hz for each FFT bin
     * @return array of averaged dB levels per band
     */
    fun aggregateBands(
        magnitudesDb: FloatArray,
        binFrequencies: FloatArray
    ): FloatArray {
        val bandLevels = FloatArray(bandCount)

        for (b in 0 until bandCount) {
            val center = bandFrequencies[b]
            // Band edges for 1/3 octave (factor 2^(1/6) ≈ 1.122)
            val ratio = 2.0.pow(1.0 / 6.0)
            val lower = (center / ratio).toFloat()
            val upper = (center * ratio).toFloat()

            var sum = 0.0
            var count = 0
            for (i in 0 until binFrequencies.size) {
                val freq = binFrequencies[i]
                if (freq >= lower && freq <= upper) {
                    if (magnitudesDb[i] > noiseFloorDb) {
                        sum += magnitudesDb[i]
                        count++
                    }
                }
                if (freq > upper) break
            }
            bandLevels[b] = if (count > 0) (sum / count).toFloat() else noiseFloorDb
        }

        currentBandLevels = bandLevels
        return bandLevels
    }

    /**
     * Compute correction gains by comparing measured bands to reference bands.
     *
     * @param referenceLevels dB per band from the source signal
     * @param measuredLevels dB per band from the microphone
     * @param currentBands existing EQ bands (for smooth transitions)
     * @return updated list of EqBand with new target gains
     */
    fun computeCorrections(
        referenceLevels: FloatArray,
        measuredLevels: FloatArray,
        currentBands: List<EqBand>
    ): List<EqBand> {
        val correctedBands = mutableListOf<EqBand>()

        for (i in currentBands.indices) {
            val refLevel = if (i < referenceLevels.size) referenceLevels[i] else 0f
            val measLevel = if (i < measuredLevels.size) measuredLevels[i] else 0f

            // Deviation: how much the room boosts or cuts this band
            val deviation = measLevel - refLevel

            // Invert the deviation (negative feedback)
            val rawCorrection = -deviation

            // Clamp to max gain
            val clamped = rawCorrection.coerceIn(-maxGainDb, maxGainDb)

            // Smooth: blend with previous gain to avoid jumps
            val smoothed = previousGains[i] * (1f - smoothingFactor) + clamped * smoothingFactor
            previousGains[i] = smoothed

            correctedBands.add(
                currentBands[i].copy(
                    targetGainDb = clamped,
                    gainDb = smoothed
                )
            )
        }

        return correctedBands
    }

    /**
     * Apply EQ gains to a spectrum (simulating the corrected output).
     */
    fun applyGainsToSpectrum(
        spectrum: SpectrumFrame,
        bands: List<EqBand>
    ): SpectrumFrame {
        val corrected = spectrum.magnitudesDb.copyOf()

        for (band in bands) {
            val center = band.centerFreq
            val gain = band.gainDb
            val q = band.q

            // Simple peak filter approximation: apply gain tapered by distance from center
            val bandwidth = center / q
            for (i in spectrum.frequencies.indices) {
                val freq = spectrum.frequencies[i]
                val distance = abs(log10(freq / center.toDouble()))
                if (distance < log10(bandwidth / center + 1.0)) {
                    val taper = (1.0 - distance / log10(bandwidth / center + 1.0)).toFloat()
                    corrected[i] += gain * taper
                }
            }
        }

        return spectrum.copy(magnitudesDb = corrected)
    }

    /**
     * Calculate the overall correction intensity (0-1) — how much the EQ is actively changing.
     */
    fun correctionIntensity(bands: List<EqBand>): Float {
        if (bands.isEmpty()) return 0f
        val totalDeviation = bands.sumOf { abs(it.gainDb.toDouble()) }
        val maxPossible = bands.size * maxGainDb
        return (totalDeviation / maxPossible).toFloat().coerceIn(0f, 1f)
    }

    fun getCurrentBandLevels(): FloatArray = currentBandLevels.copyOf()

    fun reset() {
        for (i in previousGains.indices) {
            previousGains[i] = 0f
        }
    }

    companion object {
        fun create(
            bandFrequencies: FloatArray,
            sampleRate: Int,
            fftSize: Int,
            maxGainDb: Float,
            smoothingFactor: Float,
            noiseFloorDb: Float
        ): RoomCorrector = RoomCorrector(
            bandFrequencies = bandFrequencies,
            sampleRate = sampleRate,
            fftBinCount = fftSize / 2,
            maxGainDb = maxGainDb,
            smoothingFactor = smoothingFactor,
            noiseFloorDb = noiseFloorDb
        )
    }
}
