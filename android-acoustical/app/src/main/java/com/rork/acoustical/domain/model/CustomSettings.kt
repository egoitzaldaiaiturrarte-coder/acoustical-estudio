package com.rork.acoustical.domain.model

import kotlinx.serialization.Serializable

/**
 * The user's custom settings layered on top of the active work mode (v1.9)
 * — the Android counterpart of the Windows app's `settings.json`:
 *
 *   - the Windows `loadActiveMode()` first applies the catalog mode profile
 *     (the base) and THEN re-applies the last saved `settings.json` (the
 *     personalization) over it, so every change made inside a mode survives
 *     a restart: "el catálogo da la base, el JSON la personalización";
 *
 *   - this class is the same record for Android: a COMPLETE snapshot of
 *     everything the user can tune on top of a mode (engine config, the
 *     three dynamic EQs — including the sweep direction and the free-
 *     frequency support bands — and the spatial position). It is saved on
 *     every change and, at startup, re-applied over the re-applied mode.
 *     Because it is complete, the overlay is trivially safe: fields the
 *     user never touched hold the mode's own values (the snapshot is also
 *     re-saved after a mode application, exactly like Windows'
 *     applyModeProfile() → saveSettings()).
 *
 * Plain values only (no Android/JUCE types): the JVM unit test
 * `CustomSettingsTest` pins the mode→snapshot mapping and the JSON round-
 * trip.
 */
@Serializable
data class CustomSettings(
    val maxGainDb: Float,
    val smoothingFactor: Float,
    val noiseSubtractionEnabled: Boolean,
    val correctionEnabled: Boolean,
    val targetSpl: Float,
    val audioDelayMs: Float,
    val eqs: List<EqSettings>,
    val spatialX: Float,
    val spatialY: Float,
    val spatialZ: Float,
    val spatialSize: Float
) {
    /**
     * One dynamic EQ inside the snapshot. [startFrom] is the ordinal of
     * `SweepDirection` (0 = where needed, 1 = bottom-up, 2 = top-down) —
     * the SAME order as the C index in the mode catalog, so the three
     * programs keep converging on the same values.
     */
    @Serializable
    data class EqSettings(
        val enabled: Boolean,
        val startFrom: Int,
        val decisionIntervalMs: Int,
        val maxGainDb: Float,
        val mixerLevel: Float,
        val speedMultiplier: Float,
        val extraSweeps: Int,
        val support: List<SupportBand>
    )
}

/**
 * Mode catalog → custom-settings record: the values a mode profile leaves
 * behind in the user's snapshot right after it is applied (Windows does the
 * same: applyModeProfile() ends with saveSettings()). [noiseFloorDb] is
 * intentionally absent: it is fixed by design (120 dB reference) and has
 * no manual control, in the three programs.
 */
fun ModeProfile.asCustomSettings(): CustomSettings = CustomSettings(
    maxGainDb = maxGainDb,
    smoothingFactor = smoothingFactor,
    noiseSubtractionEnabled = noiseSubtractionEnabled,
    correctionEnabled = correctionEnabled,
    targetSpl = targetSpl,
    audioDelayMs = audioDelayMs,
    eqs = eqs.map { e ->
        CustomSettings.EqSettings(
            enabled = e.enabled,
            startFrom = e.startFrom,
            decisionIntervalMs = e.intervalMs,
            maxGainDb = e.maxGainDb,
            mixerLevel = e.mixerLevel,
            speedMultiplier = e.speedMultiplier,
            extraSweeps = e.extraSweeps,
            support = e.support
        )
    },
    spatialX = spatial.x,
    spatialY = spatial.y,
    spatialZ = spatial.z,
    spatialSize = spatial.size
)
