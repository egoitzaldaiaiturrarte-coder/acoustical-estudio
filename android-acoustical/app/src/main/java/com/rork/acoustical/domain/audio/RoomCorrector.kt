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

    /** Long-lived target gain per band; smoothed toward on every frame. */
    private var targetGains = FloatArray(bandCount)
    private var currentBandLevels = FloatArray(bandCount)

    /** Fast two-band correction cadence: 2 corrections per second (500 ms). */
    private var lastCorrectionMs = 0L

    /**
     * Round-robin cursor over the SPL ranking. Cycle 0 corrects the loudest
     * and quietest band, cycle 1 the next pair, and so on. Resets once every
     * band has been touched so the sweep starts over.
     */
    private var rankCursor = 0

    /**
     * Band edge ratio for aggregation. With ultra band counts (124) the
     * spacing is ~1/12 octave, so narrower edges avoid heavy overlap.
     */
    private val bandEdgeRatio: Double =
        if (bandCount > 40) 2.0.pow(1.0 / 12.0) else 2.0.pow(1.0 / 6.0)

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
            // Band edges: half the band spacing (1/3 or 1/6 octave depending on density)
            val ratio = bandEdgeRatio
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
     * Fast two-band mode: every 500 ms the band with the highest measured SPL
     * among the bands with real signal gets its target cut and the lowest gets
     * its target boosted. The next cycle moves to the NEXT pair in the SPL
     * ranking (round-robin), so every band gets corrected before the sweep
     * restarts from the top.
     *
     * On every analysis frame ALL bands smooth continuously toward their
     * targets, so the fader motion is visible instead of a rare jump once per
     * sweep.
     *
     * @param referenceLevels dB per band from the source signal (all-zero when
     *   no reference was captured — the mean of active bands is used instead)
     * @param measuredLevels dB per band from the microphone
     * @param currentBands existing EQ bands (for smooth transitions)
     * @return updated list of EqBand with new target gains
     */
    fun computeCorrections(
        referenceLevels: FloatArray,
        measuredLevels: FloatArray,
        currentBands: List<EqBand>
    ): List<EqBand> {
        if (currentBands.isEmpty()) return currentBands
        if (targetGains.size != bandCount) {
            targetGains = FloatArray(bandCount)
        }

        val now = System.currentTimeMillis()
        if (now - lastCorrectionMs >= TWO_BAND_PERIOD_MS) {
            lastCorrectionMs = now

            // Only bands with real signal above the noise floor participate;
            // noise-floor bands would drag the neutral point down and make
            // every real band look like a peak.
            val active = currentBands.indices.filter { i ->
                i < measuredLevels.size && measuredLevels[i] > noiseFloorDb + ACTIVE_MARGIN_DB
            }
            if (active.size >= 2) {
                // A flat 0 dB reference would turn every negative dBFS level
                // into a max boost, so without a captured reference fall back
                // to the mean of the active bands as the neutral point.
                val hasRef = referenceLevels.size == bandCount && referenceLevels.any { it != 0f }
                val mean = active.map { measuredLevels[it] }.average().toFloat()

                val order = active.sortedByDescending { measuredLevels[it] }
                val rank = rankCursor.coerceAtMost(order.size - 1)
                val cutIdx = order[rank]
                val boostIdx = order[order.size - 1 - rank]

                if (cutIdx != boostIdx) {
                    val cutDeviation = if (hasRef) measuredLevels[cutIdx] - referenceLevels[cutIdx]
                    else measuredLevels[cutIdx] - mean
                    val boostDeviation = if (hasRef) measuredLevels[boostIdx] - referenceLevels[boostIdx]
                    else measuredLevels[boostIdx] - mean

                    // Cut only what actually exceeds the neutral point, boost
                    // only what sits below it; otherwise leave the target put.
                    if (cutDeviation > 0f) {
                        targetGains[cutIdx] = (targetGains[cutIdx] - cutDeviation)
                            .coerceIn(-maxGainDb, maxGainDb)
                    }
                    if (boostDeviation < 0f) {
                        targetGains[boostIdx] = (targetGains[boostIdx] - boostDeviation)
                            .coerceIn(-maxGainDb, maxGainDb)
                    }
                }

                rankCursor++
                if (rankCursor >= order.size) rankCursor = 0
            }
        }

        // Continuous smoothing toward targets on every frame.
        return currentBands.mapIndexed { i, band ->
            val target = targetGains[i]
            val smoothed = band.gainDb + (target - band.gainDb) * smoothingFactor
            band.copy(targetGainDb = target, gainDb = smoothed)
        }
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
     * Calculate the overall correction intensity (0-1) — how much the EQ is
     * actively changing. Normalized against the two bands the fast corrector
     * can move at once, so the percentage stays meaningful with 124 bands.
     */
    fun correctionIntensity(bands: List<EqBand>): Float {
        if (bands.isEmpty()) return 0f
        val totalDeviation = bands.sumOf { abs(it.gainDb.toDouble()) }
        val maxPossible = (2f * maxGainDb).coerceAtLeast(0.5f)
        return (totalDeviation / maxPossible).toFloat().coerceIn(0f, 1f)
    }

    fun getCurrentBandLevels(): FloatArray = currentBandLevels.copyOf()

    fun reset() {
        targetGains = FloatArray(bandCount)
        lastCorrectionMs = 0L
        rankCursor = 0
    }

    companion object {
        /** Fast two-band correction runs twice per second. */
        private const val TWO_BAND_PERIOD_MS = 500L

        /** Margin over the noise floor for a band to count as "real signal". */
        private const val ACTIVE_MARGIN_DB = 3f

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
