package com.rork.acoustical.domain.model

import kotlinx.serialization.Serializable

/**
 * One of the three dynamic EQs inside a work mode.
 *
 * Mirrors `acoustical_eq_profile` from the shared C core
 * (`dsp/acoustical_dsp.c`) field by field so the catalog below stays
 * identical to the one the Windows app and the VST read from C:
 *  - [startFrom] is the C index 0..2 (NEED_BASED / BOTTOM_UP / TOP_DOWN)
 *    — the SAME order as [SweepDirection] in the domain audio layer.
 *  - [support] is the per-mode free-frequency bank (identical in the six
 *    modes: trebles / mids / sub-bass per EQ).
 */
@Serializable
data class ModeEqProfile(
    val enabled: Boolean,
    val startFrom: Int,
    val intervalMs: Int,
    val maxGainDb: Float,
    val mixerLevel: Float,
    val speedMultiplier: Float,
    val extraSweeps: Int,
    val support: List<SupportBand>
)

/**
 * A full startup work mode (v1.8): everything the engine needs to start
 * working in a given situation — target SPL, correction/noise-subtraction
 * switches, the three dynamic EQs with their support-band banks and the
 * spatial position of the element.
 *
 * This is the Kotlin mirror of `acoustical_mode_profile` (same file, same
 * values, same order). The JVM unit test `ModeProfilesTest` pins it against
 * the C test `dsp/test_modes.cpp` so the three programs cannot drift: the
 * Windows app and the VST read the C table directly, this app reads the
 * mirror, and both assert the same numbers.
 *
 * Mode indices map onto the domain enums with the SAME ordinal:
 * bandCountIndex 0..4 → [BandCount], fftSizeIndex 0..4 → [FftSize],
 * analysisIntervalIndex 0..4 → [AnalysisInterval].
 */
@Serializable
data class ModeProfile(
    val id: String,
    val name: String,
    val targetSpl: Float,
    val maxGainDb: Float,
    val bandCountIndex: Int,
    val fftSizeIndex: Int,
    val analysisIntervalIndex: Int,
    val smoothingFactor: Float,
    val correctionEnabled: Boolean,
    val noiseSubtractionEnabled: Boolean,
    val audioDelayMs: Float,
    val eqs: List<ModeEqProfile>,
    val spatial: SpatialPosition
) {
    val bandCount: BandCount get() = BandCount.values()[bandCountIndex]
    val fftSize: FftSize get() = FftSize.values()[fftSizeIndex]
    val analysisInterval: AnalysisInterval get() = AnalysisInterval.values()[analysisIntervalIndex]

    /** One-line summary for the modes screen (SPL + motor + EQs + space). */
    val summary: String
        get() {
            val eq = if (eqs.count { it.enabled } == 3) "básico + los 3"
            else if (eqs.count { it.enabled } == 0) "sin ecuas"
            else "ecuas parciales"
            val space = when {
                spatial.z < 0.35f && spatial.size < 0.35f -> "espacio cerca y pequeño"
                spatial.z > 0.7f && spatial.size > 0.7f -> "espacio lejos y grande"
                else -> "espacio medio"
            }
            return "%.0f dB · 124 bandas · corr. %s · ruido %s · %s · %s".format(
                targetSpl,
                if (correctionEnabled) "ON" else "OFF",
                if (noiseSubtractionEnabled) "ON" else "OFF",
                eq,
                space
            )
        }
}

/**
 * The six startup work modes, in the SAME order and with the SAME values
 * as the C table `kModeProfiles` (see the KDoc of [ModeProfile]).
 *
 * Agreed invariants (mirrored by `dsp/test_modes.cpp`, v1.10.0 values):
 *  - all start with max gain 50 dB, 124 bands, FFT 8192, 25 ms analysis,
 *    smoothing 0.05 and the three dynamic EQs active;
 *  - the three EQs share decision interval 100 ms, 80 % mixer and 6 extra
 *    sweeps per decision; per-EQ max gain 12/6/3 dB and smoothing speed
 *    ×4/×2/×0.5;
 *  - one shared support-band bank per EQ across all six modes:
 *    EQ 1 trebles 2.5k/5k/10k/20k, EQ 2 mids 63/250/1k/4k,
 *    EQ 3 sub-bass 20/41/62/83 Hz, all at 0 dB with the narrowest possible Q
 *    (0.1, the slider minimum);
 *  - target SPLs 78/97/89/91/93/103 dB (PAs needs the unified 30–110 range);
 *  - the phone's engine is analysis-only, so the generator part of the C
 *    table (Silence) has no target here: test signals are on-demand.
 */
object ModeProfiles {
    const val COUNT = 6

    private fun band(hz: Float): SupportBand =
        SupportBand(frequencyHz = hz, gainDb = 0f, q = 0.1f)

    /** The three-EQ bank, identical in the six modes (zones: trebles / mids / sub). */
    private val bank: List<List<SupportBand>> = listOf(
        listOf(band(2500f), band(5000f), band(10000f), band(20000f)),
        listOf(band(63f), band(250f), band(1000f), band(4000f)),
        listOf(band(20f), band(41f), band(62f), band(83f))
    )

    /**
     * One entry of the three-EQ bank (v1.10.0): shared interval 100 ms,
     * 6 extra sweeps, 0.8 mixer; per-EQ max gain and smoothing speed.
     */
    private fun eq(startFrom: Int, maxGainDb: Float, speedMultiplier: Float,
                   zone: Int): ModeEqProfile = ModeEqProfile(
        enabled = true,
        startFrom = startFrom,
        intervalMs = 100,
        maxGainDb = maxGainDb,
        mixerLevel = 0.8f,
        speedMultiplier = speedMultiplier,
        extraSweeps = 6,
        support = bank[zone]
    )

    private val commonEqs: List<ModeEqProfile> = listOf(
        eq(startFrom = 0, maxGainDb = 12f, speedMultiplier = 4f, zone = 0),   // EQ 1: where needed, treble bank
        eq(startFrom = 1, maxGainDb = 6f,  speedMultiplier = 2f, zone = 1),   // EQ 2: bottom-up, mid bank
        eq(startFrom = 2, maxGainDb = 3f,  speedMultiplier = 0.5f, zone = 2) // EQ 3: top-down, sub bank
    )

    val ALL: List<ModeProfile> = listOf(
        ModeProfile(
            id = "carro-normal",
            name = "Coche · Normal",
            targetSpl = 78f, maxGainDb = 50f,
            bandCountIndex = 4, fftSizeIndex = 4, analysisIntervalIndex = 0,
            smoothingFactor = 0.05f,
            correctionEnabled = true, noiseSubtractionEnabled = true,
            audioDelayMs = 0f,
            eqs = commonEqs,
            spatial = SpatialPosition(x = 0f, y = 0f, z = 0.2f, size = 0.3f)
        ),
        ModeProfile(
            id = "carro-aire-libre",
            name = "Coche · Aire libre",
            targetSpl = 97f, maxGainDb = 50f,
            bandCountIndex = 4, fftSizeIndex = 4, analysisIntervalIndex = 0,
            smoothingFactor = 0.05f,
            correctionEnabled = false, noiseSubtractionEnabled = true,
            audioDelayMs = 0f,
            eqs = commonEqs,
            spatial = SpatialPosition(x = 0f, y = 0f, z = 0.5f, size = 0.5f)
        ),
        ModeProfile(
            id = "estudio-esc1",
            name = "Estudio · Escenario 1",
            targetSpl = 89f, maxGainDb = 50f,
            bandCountIndex = 4, fftSizeIndex = 4, analysisIntervalIndex = 0,
            smoothingFactor = 0.05f,
            correctionEnabled = true, noiseSubtractionEnabled = true,
            audioDelayMs = 0f,
            eqs = commonEqs,
            spatial = SpatialPosition(x = 0f, y = 0f, z = 0.5f, size = 0.5f)
        ),
        ModeProfile(
            id = "escenario2",
            name = "Escenario 2",
            targetSpl = 91f, maxGainDb = 50f,
            bandCountIndex = 4, fftSizeIndex = 4, analysisIntervalIndex = 0,
            smoothingFactor = 0.05f,
            correctionEnabled = false, noiseSubtractionEnabled = false,
            audioDelayMs = 0f,
            eqs = commonEqs,
            spatial = SpatialPosition(x = 0f, y = 0f, z = 0.5f, size = 0.5f)
        ),
        ModeProfile(
            id = "escenario3",
            name = "Escenario 3",
            targetSpl = 93f, maxGainDb = 50f,
            bandCountIndex = 4, fftSizeIndex = 4, analysisIntervalIndex = 0,
            smoothingFactor = 0.05f,
            correctionEnabled = true, noiseSubtractionEnabled = true,
            audioDelayMs = 0f,
            eqs = commonEqs,
            spatial = SpatialPosition(x = 0f, y = 0f, z = 0.9f, size = 0.9f)
        ),
        ModeProfile(
            id = "pas",
            name = "PAs",
            targetSpl = 103f, maxGainDb = 50f,
            bandCountIndex = 4, fftSizeIndex = 4, analysisIntervalIndex = 0,
            smoothingFactor = 0.05f,
            correctionEnabled = true, noiseSubtractionEnabled = true,
            audioDelayMs = 0f,
            eqs = commonEqs,
            spatial = SpatialPosition(x = 0f, y = 0f, z = 0.2f, size = 0.3f)
        )
    )

    /** Catalog lookup by id (""/null = no mode, like the C `acoustical_mode_by_id`). */
    fun byId(id: String?): ModeProfile? =
        if (id.isNullOrEmpty()) null else ALL.firstOrNull { it.id == id }
}
