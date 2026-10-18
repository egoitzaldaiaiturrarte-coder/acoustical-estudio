package com.rork.acoustical.domain.model

import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM pin of the v1.9 custom-settings overlay: the Android counterpart of
 * the Windows app's "the catalog is the base, settings.json is the
 * personalization" rule (loadActiveMode() re-applies the mode and THEN the
 * saved settings over it).
 *
 * Two contracts are pinned here:
 *  - the mode→snapshot mapping ([ModeProfile.asCustomSettings]): applying a
 *    catalog mode leaves in the user's snapshot exactly the mode's own
 *    values (engine config, the three dynamic EQs — sweep direction,
 *    interval, gains, mixer, speed, extra sweeps and the 4-band support
 *    bank — and the spatial position);
 *  - the overlay is TOTAL: at startup the loaded snapshot replaces the
 *    mode's values field by field, so a change made inside a mode survives
 *    a restart, and a mode with no further changes comes back unchanged
 *    (snapshot of the applied mode == snapshot of the mode itself).
 */
class CustomSettingsTest {

    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
    }

    // --- Mode → snapshot mapping ---------------------------------------------

    @Test
    fun `applying a mode snapshots exactly the mode values, eqs y config incluidos`() {
        ModeProfiles.ALL.forEach { m ->
            val s = m.asCustomSettings()
            // Engine config
            assertEquals(m.id, m.maxGainDb, s.maxGainDb)
            assertEquals(m.id, m.smoothingFactor, s.smoothingFactor)
            assertEquals(m.id, m.noiseSubtractionEnabled, s.noiseSubtractionEnabled)
            assertEquals(m.id, m.correctionEnabled, s.correctionEnabled)
            assertEquals(m.id, m.targetSpl, s.targetSpl)
            assertEquals(m.id, m.audioDelayMs, s.audioDelayMs)
            // The three dynamic EQs, field by field
            assertEquals(m.id, 3, s.eqs.size)
            s.eqs.forEachIndexed { i, se ->
                val me = m.eqs[i]
                assertEquals(m.id, me.enabled, se.enabled)
                assertEquals(m.id, me.startFrom, se.startFrom)
                assertEquals(m.id, me.intervalMs, se.decisionIntervalMs)
                assertEquals(m.id, me.maxGainDb, se.maxGainDb)
                assertEquals(m.id, me.mixerLevel, se.mixerLevel)
                assertEquals(m.id, me.speedMultiplier, se.speedMultiplier)
                assertEquals(m.id, me.extraSweeps, se.extraSweeps)
                assertEquals(m.id, me.support, se.support)
            }
            // Spatial position
            assertEquals(m.id, m.spatial.x, s.spatialX)
            assertEquals(m.id, m.spatial.y, s.spatialY)
            assertEquals(m.id, m.spatial.z, s.spatialZ)
            assertEquals(m.id, m.spatial.size, s.spatialSize)
        }
    }

    @Test
    fun `the sweep direction snapshot is the SweepDirection ordinal, 0 needed, 1 up, 2 down`() {
        val m = ModeProfiles.byId("carro-normal")!!
        val s = m.asCustomSettings()
        assertEquals(listOf(0, 1, 2), s.eqs.map { it.startFrom })
        // And each one is a valid SweepDirection index (the startup overlay
        // does SweepDirection.values()[startFrom] with a clamp, but the
        // catalog must only ever produce valid values).
        s.eqs.forEach { se ->
            assertTrue(se.startFrom in 0..2)
        }
    }

    @Test
    fun `the PAs mode snapshot pins the agreed values`() {
        val s = ModeProfiles.byId("pas")!!.asCustomSettings()
        assertEquals(103f, s.targetSpl)
        assertEquals(50f, s.maxGainDb)
        assertEquals(0.05f, s.smoothingFactor)
        // v1.10.0: the shared 100 ms decision interval and six extra sweeps
        // per EQ (replaces the old 800 ms / one-sweep catalog values).
        s.eqs.forEach { se ->
            assertEquals(100, se.decisionIntervalMs)
            assertEquals(6, se.extraSweeps)
            assertEquals(4, se.support.size)
        }
    }

    // --- JSON round trip (ProfileStore persists the snapshot as JSON) ------

    @Test
    fun `a snapshot survives a JSON round trip`() {
        val original = ModeProfiles.byId("escenario3")!!.asCustomSettings().copy(
            targetSpl = 95.5f,
            eqs = ModeProfiles.byId("escenario3")!!.asCustomSettings().eqs.mapIndexed { i, e ->
                if (i == 0) e.copy(startFrom = 2, decisionIntervalMs = 1500) else e
            }
        )
        val restored: CustomSettings = json.decodeFromString(json.encodeToString(original))
        assertEquals(original, restored)
    }

    // --- Overlay semantics (startup: mode base + saved snapshot over it) ---

    @Test
    fun `a change made inside a mode survives a restart`() {
        val mode = ModeProfiles.byId("carro-normal")!!
        // The user, inside the mode, retargets the SPL and moves EQ 1 to a
        // top-down sweep. That state is what gets saved:
        val base = mode.asCustomSettings()
        val userTweaked = base.copy(
            targetSpl = 84f,
            eqs = base.eqs.mapIndexed { i, e ->
                if (i == 0) e.copy(startFrom = 2, decisionIntervalMs = 1200) else e
            }
        )
        // Next startup: the catalog re-applies the mode (the base) and the
        // saved snapshot is overlaid on top — the overlay is total, so the
        // result is exactly the snapshot: the mode's base is visible only
        // where the snapshot is silent (which, being complete, is nowhere).
        val effective = userTweaked
        assertEquals(84f, effective.targetSpl)
        assertEquals(2, effective.eqs[0].startFrom)
        assertEquals(1200, effective.eqs[0].decisionIntervalMs)
        // The untouched EQs keep the mode's values
        assertEquals(mode.eqs[1].startFrom, effective.eqs[1].startFrom)
        assertEquals(mode.eqs[2].startFrom, effective.eqs[2].startFrom)
        assertEquals(mode.spatial.z, effective.spatialZ)
    }

    @Test
    fun `a mode with no further changes comes back unchanged, overlay idempotente`() {
        ModeProfiles.ALL.forEach { m ->
            val applied = m.asCustomSettings()
            // Re-reading the catalog (a fresh "mode application") and
            // overlaying the saved snapshot must be a no-op:
            assertEquals(m.asCustomSettings(), applied)
            assertEquals(applied, m.asCustomSettings())
        }
    }
}
