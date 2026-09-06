package com.rork.acoustical.domain.audio

import kotlin.math.pow

/**
 * "Auto ayuda" — the fast sweeper processor with its own mixer.
 *
 * It sweeps the EQ bands going where they are most needed: the band with the
 * highest measured SPL gets its target cut and the quietest one boosted,
 * advancing round-robin through the SPL ranking. Sweep speed and smoothing
 * adapt automatically and progressively by frequency — in the treble it sweeps
 * in 0.10 ms steps with up to 4 ms of smoothing, slowing down smoothly toward
 * the bass. There are no manual controls for these curves.
 */
class SweeperProcessor(
    private val bandFrequencies: FloatArray,
    private val maxGainDb: Float
) {
    /** One sweep correction, published to the UI for the live status. */
    data class SweepStep(
        val bandIndex: Int,
        val centerFreqHz: Float,
        val gainDb: Float,
        val sweepIntervalMs: Float,
        val smoothingMs: Float
    )

    /** Its own mixer: 0..1 output level of the sweep corrections. */
    @Volatile
    var mixerLevel: Float = 0.8f

    private val bandCount: Int = bandFrequencies.size
    private val sweepGains = FloatArray(bandCount)
    private val targets = FloatArray(bandCount)
    private var rankCursor = 0
    private var lastAdvanceMs = 0L

    /** 0 = bass (20 Hz), 1 = treble (20 kHz), log-spaced. */
    private fun freqNorm(freqHz: Float): Float =
        (Math.log10((freqHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).coerceIn(0.0, 1.0).toFloat()

    /** Treble sweeps in 0.10 ms steps; bass up to 100x slower (100 ms). */
    fun sweepIntervalMs(freqHz: Float): Float = 100f * 10f.pow(-3f * freqNorm(freqHz))

    /** Treble smoothing caps at ~4 ms; bass relaxes up to 500 ms. */
    fun smoothingMs(freqHz: Float): Float = 500f * 10f.pow(-2.1f * freqNorm(freqHz))

    /**
     * One 10 ms tick. Smooths every band toward its target (attack time set
     * automatically by frequency), then advances the sweep to the next band in
     * the SPL ranking when its own interval allows.
     *
     * @return the step that was just applied, or null when there is nothing to
     *   correct (not enough signal) or the band's interval has not elapsed.
     */
    fun step(measuredLevels: FloatArray, nowMs: Long, dtMs: Float = TICK_MS.toFloat()): SweepStep? {
        if (bandCount == 0) return null

        // 1. Continuous smoothing toward targets, per-band auto attack time.
        for (i in 0 until bandCount) {
            val factor = (dtMs / smoothingMs(bandFrequencies[i])).coerceIn(0f, 1f)
            sweepGains[i] += (targets[i] - sweepGains[i]) * factor
        }

        // 2. Only bands with real signal participate.
        val active = (0 until bandCount).filter { i ->
            i < measuredLevels.size && measuredLevels[i] > NOISE_FLOOR_DB + ACTIVE_MARGIN_DB
        }
        if (active.size < 2) return null

        val rank = rankCursor % active.size
        val cutIdx = active.sortedByDescending { measuredLevels[it] }[rank]
        val freq = bandFrequencies[cutIdx]

        val interval = sweepIntervalMs(freq)
        if (nowMs - lastAdvanceMs < interval) return null
        lastAdvanceMs = nowMs

        val mean = active.map { measuredLevels[it] }.average().toFloat()
        val boostIdx = active.sortedByDescending { measuredLevels[it] }[active.size - 1 - rank]

        if (boostIdx != cutIdx) {
            val cutDev = (measuredLevels[cutIdx] - mean).coerceAtLeast(0f)
            if (cutDev > 0f) {
                targets[cutIdx] = (targets[cutIdx] - cutDev).coerceIn(-maxGainDb, maxGainDb)
            }
            val boostDev = (mean - measuredLevels[boostIdx]).coerceAtLeast(0f)
            if (boostDev > 0f) {
                targets[boostIdx] = (targets[boostIdx] + boostDev).coerceIn(-maxGainDb, maxGainDb)
            }
        }

        rankCursor++
        return SweepStep(
            bandIndex = cutIdx,
            centerFreqHz = freq,
            gainDb = sweepGains[cutIdx] * mixerLevel,
            sweepIntervalMs = interval,
            smoothingMs = smoothingMs(freq)
        )
    }

    /** Post-mixer gains per band (raw corrections scaled by the mixer level). */
    fun gains(): FloatArray = FloatArray(bandCount) { sweepGains[it] * mixerLevel }

    fun reset() {
        for (i in 0 until bandCount) {
            sweepGains[i] = 0f
            targets[i] = 0f
        }
        rankCursor = 0
        lastAdvanceMs = 0L
    }

    companion object {
        /** The sweeper ticks every 10 ms. */
        const val TICK_MS = 10L

        /** Same fixed threshold the engine uses (120 dB reference). */
        private const val NOISE_FLOOR_DB = -120f

        /** Margin over the threshold for a band to count as "real signal". */
        private const val ACTIVE_MARGIN_DB = 3f
    }
}
