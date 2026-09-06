package com.rork.acoustical.domain.audio

import kotlin.math.pow

/** How the processor picks the next band to correct. */
enum class SweepDirection {
    /** Always goes where it is most needed (SPL ranking, round-robin). */
    NEED_BASED,
    /** Traverses the spectrum from the bass upward. */
    BOTTOM_UP,
    /** Traverses the spectrum from the treble downward. */
    TOP_DOWN
}

/** Identifies one of the three automated processors. */
enum class SweepProcess { AUTO_HELP, EQ_NORMAL, AUTO_CHECK }

/**
 * Automated free-frequency corrector. The three processors (Auto ayuda,
 * EQ normal, Auto-chequeo) are instances of this class, differing only in
 * their [direction].
 *
 * Every [DECISION_INTERVAL_MS] the processor makes a decision — which band to
 * cut and which to boost, chosen by its [direction] — while the gain values
 * keep adjusting every [TICK_MS] toward their targets, with a smoothing time
 * that adapts automatically by frequency (fast in the treble, relaxed toward
 * the bass). There are no manual controls for these curves.
 */
class SweeperProcessor(
    private val bandFrequencies: FloatArray,
    private val maxGainDb: Float,
    val direction: SweepDirection = SweepDirection.NEED_BASED
) {
    /** One correction decision, published to the UI for the live status. */
    data class SweepStep(
        val bandIndex: Int,
        val centerFreqHz: Float,
        val gainDb: Float,
        val decisionIntervalMs: Float,
        val smoothingMs: Float
    )

    /** Its own mixer: 0..1 output level of the corrections. */
    @Volatile
    var mixerLevel: Float = 0.8f

    private val bandCount: Int = bandFrequencies.size
    private val sweepGains = FloatArray(bandCount)
    private val targets = FloatArray(bandCount)
    private var cursor = 0
    private var lastDecisionMs = 0L

    /** 0 = bass (20 Hz), 1 = treble (20 kHz), log-spaced. */
    private fun freqNorm(freqHz: Float): Float =
        (Math.log10((freqHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).coerceIn(0.0, 1.0).toFloat()

    /** Smoothing adapts automatically: ~4 ms in the treble, relaxed toward the bass. */
    fun smoothingMs(freqHz: Float): Float = 500f * 10f.pow(-2.1f * freqNorm(freqHz))

    /**
     * One 10 ms tick. Every band's gain keeps moving toward its target with its
     * automatic smoothing; every [DECISION_INTERVAL_MS] a new decision picks
     * the band to cut (above the average) and the one to boost (below it)
     * according to this processor's direction.
     *
     * @return the decision just applied, or null when the decision window has
     *   not elapsed or there is not enough signal.
     */
    fun step(measuredLevels: FloatArray, nowMs: Long, dtMs: Float = TICK_MS.toFloat()): SweepStep? {
        if (bandCount == 0) return null

        // 1. Continuous 10 ms adjustment toward targets, per-band auto smoothing.
        for (i in 0 until bandCount) {
            val factor = (dtMs / smoothingMs(bandFrequencies[i])).coerceIn(0f, 1f)
            sweepGains[i] += (targets[i] - sweepGains[i]) * factor
        }

        // 2. One decision every 800 ms.
        if (nowMs - lastDecisionMs < DECISION_INTERVAL_MS) return null
        lastDecisionMs = nowMs

        // 3. Only bands with real signal participate.
        val active = (0 until bandCount).filter { i ->
            i < measuredLevels.size && measuredLevels[i] > NOISE_FLOOR_DB + ACTIVE_MARGIN_DB
        }
        if (active.size < 2) return null

        val mean = active.map { measuredLevels[it] }.average().toFloat()

        val ordered = when (direction) {
            SweepDirection.NEED_BASED -> active.sortedByDescending { measuredLevels[it] }
            SweepDirection.BOTTOM_UP -> active.sortedBy { bandFrequencies[it] }
            SweepDirection.TOP_DOWN -> active.sortedByDescending { bandFrequencies[it] }
        }

        // Walk the ordered list round-robin: cut above the mean, boost below.
        val rank = cursor % active.size
        val cutIdx = ordered[rank]
        val boostIdx = ordered[active.size - 1 - rank]

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

        cursor++
        return SweepStep(
            bandIndex = cutIdx,
            centerFreqHz = bandFrequencies[cutIdx],
            gainDb = sweepGains[cutIdx] * mixerLevel,
            decisionIntervalMs = DECISION_INTERVAL_MS.toFloat(),
            smoothingMs = smoothingMs(bandFrequencies[cutIdx])
        )
    }

    /** Post-mixer gains per band (corrections scaled by the mixer level). */
    fun gains(): FloatArray = FloatArray(bandCount) { sweepGains[it] * mixerLevel }

    fun reset() {
        for (i in 0 until bandCount) {
            sweepGains[i] = 0f
            targets[i] = 0f
        }
        cursor = 0
        lastDecisionMs = 0L
    }

    companion object {
        /** Gain values adjust every 10 ms. */
        const val TICK_MS = 10L

        /** Each processor makes a decision every 800 ms. */
        const val DECISION_INTERVAL_MS = 800L

        /** Same fixed threshold the engine uses (120 dB reference). */
        private const val NOISE_FLOOR_DB = -120f

        /** Margin over the threshold for a band to count as "real signal". */
        private const val ACTIVE_MARGIN_DB = 3f
    }
}
