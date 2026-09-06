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

/** Identifies one of the three dynamic EQs. */
enum class SweepProcess { EQ_1, EQ_2, EQ_3 }

/**
 * Settings of one dynamic EQ. The three dynamic EQs are the same automatic
 * corrector with different parameters: each decides every [decisionIntervalMs]
 * which band to correct while the gain values keep adjusting every 10 ms,
 * applies [extraSweeps] extra band pairs per decision (accelerating the
 * process), and its smoothing adapts automatically by frequency — faster in
 * the treble — scaled by [speedMultiplier].
 */
data class DynamicEqConfig(
    val startFrom: SweepDirection = SweepDirection.NEED_BASED,
    val decisionIntervalMs: Int = 800,
    val maxGainDb: Float = 12f,
    val mixerLevel: Float = 0.8f,
    /** Multiplier over the automatic frequency-adaptive smoothing (higher = faster). */
    val speedMultiplier: Float = 1f,
    /** Extra band pairs corrected on every decision (accelerates the process). */
    val extraSweeps: Int = 1
) {
    companion object {
        const val MIN_INTERVAL_MS = 100
        const val MAX_INTERVAL_MS = 2000
        const val MIN_GAIN_DB = 1f
        const val MAX_GAIN_DB = 50f
        const val MIN_SPEED = 0.5f
        const val MAX_SPEED = 4f
        const val MAX_EXTRA_SWEEPS = 6
    }
}

/**
 * Automated free-frequency corrector. The three dynamic EQs are instances of
 * this class, differing only in their [DynamicEqConfig].
 *
 * Every `decisionIntervalMs` the processor makes a decision — which band to
 * cut and which to boost, chosen by its direction, plus the configured extra
 * sweeps — while the gain values keep adjusting every [TICK_MS] toward their
 * targets with automatic per-frequency smoothing.
 *
 * Gains are kept per channel (L/R): linked, both channels are corrected
 * together; unlinked, the channels alternate decision by decision so each one
 * gets its own correction.
 */
class SweeperProcessor(
    private val bandFrequencies: FloatArray,
    config: DynamicEqConfig
) {
    /** One correction decision, published to the UI for the live status. */
    data class SweepStep(
        val bandIndex: Int,
        val centerFreqHz: Float,
        val gainDb: Float,
        val decisionIntervalMs: Float,
        val smoothingMs: Float,
        /** "L+R" when linked, or the channel corrected by this decision. */
        val channel: String
    )

    @Volatile
    var decisionIntervalMs = config.decisionIntervalMs
        .coerceIn(DynamicEqConfig.MIN_INTERVAL_MS, DynamicEqConfig.MAX_INTERVAL_MS).toLong()

    @Volatile
    var maxGainDb = config.maxGainDb
        .coerceIn(DynamicEqConfig.MIN_GAIN_DB, DynamicEqConfig.MAX_GAIN_DB)

    @Volatile
    var mixerLevel = config.mixerLevel.coerceIn(0f, 1f)

    @Volatile
    var speedMultiplier = config.speedMultiplier
        .coerceIn(DynamicEqConfig.MIN_SPEED, DynamicEqConfig.MAX_SPEED)

    @Volatile
    var extraSweeps = config.extraSweeps.coerceIn(0, DynamicEqConfig.MAX_EXTRA_SWEEPS)

    @Volatile
    var direction: SweepDirection = config.startFrom

    /** Linked = L and R corrected together; unlinked = channels alternate. */
    @Volatile
    var channelLinked = true

    private val bandCount: Int = bandFrequencies.size
    private val autoGainsL = FloatArray(bandCount)
    private val autoTargetsL = FloatArray(bandCount)
    private val autoGainsR = FloatArray(bandCount)
    private val autoTargetsR = FloatArray(bandCount)
    private var cursor = 0
    private var lastDecisionMs = 0L
    private var nextChannelIsR = false

    /** Live-update this processor's parameters without losing its progress. */
    fun applyConfig(cfg: DynamicEqConfig) {
        decisionIntervalMs = cfg.decisionIntervalMs
            .coerceIn(DynamicEqConfig.MIN_INTERVAL_MS, DynamicEqConfig.MAX_INTERVAL_MS).toLong()
        maxGainDb = cfg.maxGainDb.coerceIn(DynamicEqConfig.MIN_GAIN_DB, DynamicEqConfig.MAX_GAIN_DB)
        mixerLevel = cfg.mixerLevel.coerceIn(0f, 1f)
        speedMultiplier = cfg.speedMultiplier
            .coerceIn(DynamicEqConfig.MIN_SPEED, DynamicEqConfig.MAX_SPEED)
        extraSweeps = cfg.extraSweeps.coerceIn(0, DynamicEqConfig.MAX_EXTRA_SWEEPS)
        direction = cfg.startFrom
    }

    /** 0 = bass (20 Hz), 1 = treble (20 kHz), log-spaced. */
    private fun freqNorm(freqHz: Float): Float =
        (Math.log10((freqHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).coerceIn(0.0, 1.0).toFloat()

    /** Automatic smoothing by frequency: ~4 ms in the treble, relaxed toward the bass. */
    fun smoothingMs(freqHz: Float): Float =
        (500f * 10f.pow(-2.1f * freqNorm(freqHz)) / speedMultiplier).coerceAtLeast(2f)

    /**
     * One 10 ms tick. Every band's gain keeps moving toward its target with its
     * automatic smoothing; every [decisionIntervalMs] a new decision picks the
     * band to cut (above the average) and the one to boost (below it) according
     * to this EQ's direction, plus the configured extra sweeps.
     *
     * @return the decision just applied, or null when the decision window has
     *   not elapsed or there is not enough signal.
     */
    fun step(measuredLevels: FloatArray, nowMs: Long, dtMs: Float = TICK_MS.toFloat()): SweepStep? {
        if (bandCount == 0) return null

        // 1. Continuous 10 ms adjustment toward targets, per-band auto smoothing.
        for (i in 0 until bandCount) {
            val factor = (dtMs / smoothingMs(bandFrequencies[i])).coerceIn(0f, 1f)
            autoGainsL[i] += (autoTargetsL[i] - autoGainsL[i]) * factor
            autoGainsR[i] += (autoTargetsR[i] - autoGainsR[i]) * factor
        }

        // 2. One decision every decisionIntervalMs.
        if (nowMs - lastDecisionMs < decisionIntervalMs) return null
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

        // Main pair + extra sweeps: consecutive positions in the ordered list.
        var firstCutIdx = -1
        val channel: String
        if (channelLinked) {
            channel = "L+R"
            for (k in 0..extraSweeps) {
                val rank = (cursor + k).mod(active.size)
                val cutIdx = ordered[rank]
                val boostIdx = ordered[(active.size - 1 - rank).mod(active.size)]
                applyDecision(measuredLevels, cutIdx, boostIdx, mean, toL = true, toR = true)
                if (firstCutIdx < 0) firstCutIdx = cutIdx
            }
        } else {
            val toR = nextChannelIsR
            nextChannelIsR = !nextChannelIsR
            channel = if (toR) "R" else "L"
            for (k in 0..extraSweeps) {
                val rank = (cursor + k).mod(active.size)
                val cutIdx = ordered[rank]
                val boostIdx = ordered[(active.size - 1 - rank).mod(active.size)]
                applyDecision(measuredLevels, cutIdx, boostIdx, mean, toL = !toR, toR = toR)
                if (firstCutIdx < 0) firstCutIdx = cutIdx
            }
        }

        cursor += extraSweeps + 1

        return SweepStep(
            bandIndex = firstCutIdx,
            centerFreqHz = bandFrequencies[firstCutIdx],
            gainDb = (if (channel == "R") autoGainsR[firstCutIdx] else autoGainsL[firstCutIdx]) * mixerLevel,
            decisionIntervalMs = decisionIntervalMs.toFloat(),
            smoothingMs = smoothingMs(bandFrequencies[firstCutIdx]),
            channel = channel
        )
    }

    private fun applyDecision(
        measuredLevels: FloatArray,
        cutIdx: Int,
        boostIdx: Int,
        mean: Float,
        toL: Boolean,
        toR: Boolean
    ) {
        if (cutIdx == boostIdx) return
        val cutDev = (measuredLevels[cutIdx] - mean).coerceAtLeast(0f)
        val boostDev = (mean - measuredLevels[boostIdx]).coerceAtLeast(0f)
        if (toL) {
            if (cutDev > 0f) {
                autoTargetsL[cutIdx] = (autoTargetsL[cutIdx] - cutDev).coerceIn(-maxGainDb, maxGainDb)
            }
            if (boostDev > 0f) {
                autoTargetsL[boostIdx] = (autoTargetsL[boostIdx] + boostDev).coerceIn(-maxGainDb, maxGainDb)
            }
        }
        if (toR) {
            if (cutDev > 0f) {
                autoTargetsR[cutIdx] = (autoTargetsR[cutIdx] - cutDev).coerceIn(-maxGainDb, maxGainDb)
            }
            if (boostDev > 0f) {
                autoTargetsR[boostIdx] = (autoTargetsR[boostIdx] + boostDev).coerceIn(-maxGainDb, maxGainDb)
            }
        }
    }

    /** Post-mixer gain curves per channel (corrections scaled by the mixer level). */
    fun gainsL(): FloatArray = FloatArray(bandCount) { autoGainsL[it] * mixerLevel }

    fun gainsR(): FloatArray = FloatArray(bandCount) { autoGainsR[it] * mixerLevel }

    fun reset() {
        for (i in 0 until bandCount) {
            autoGainsL[i] = 0f
            autoTargetsL[i] = 0f
            autoGainsR[i] = 0f
            autoTargetsR[i] = 0f
        }
        cursor = 0
        lastDecisionMs = 0L
        nextChannelIsR = false
    }

    companion object {
        /** Gain values adjust every 10 ms. */
        const val TICK_MS = 10L

        /** Same fixed threshold the engine uses (120 dB reference). */
        private const val NOISE_FLOOR_DB = -120f

        /** Margin over the threshold for a band to count as "real signal". */
        private const val ACTIVE_MARGIN_DB = 3f
    }
}
