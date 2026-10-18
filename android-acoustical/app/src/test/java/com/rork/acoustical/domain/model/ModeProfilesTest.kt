package com.rork.acoustical.domain.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM pin of the Kotlin work-mode catalog: the SAME values the C core table
 * `kModeProfiles` holds (checked by `dsp/test_modes.cpp` on the CI gate).
 * If any number here or in C changes, both tests must be updated together —
 * that is the contract that keeps the three programs (Windows app, VST,
 * phone) starting with identical settings.
 */
class ModeProfilesTest {

    private fun nearF(a: Float, b: Float, eps: Float = 0.01f) =
        kotlin.math.abs(a - b) <= eps

    // --- Structure ----------------------------------------------------------

    @Test
    fun `the catalog has exactly the six agreed modes`() {
        assertEquals(6, ModeProfiles.COUNT)
        assertEquals(6, ModeProfiles.ALL.size)
    }

    @Test
    fun `ids and names are present and unique, in the C order`() {
        val expected = listOf(
            "carro-normal", "carro-aire-libre", "estudio-esc1",
            "escenario2", "escenario3", "pas"
        )
        assertEquals(expected, ModeProfiles.ALL.map { it.id })
        ModeProfiles.ALL.forEach { m ->
            assertTrue(m.name.isNotBlank())
        }
        assertEquals(expected.toSet(), ModeProfiles.ALL.map { it.id }.toSet())
    }

    // --- Common invariants (the six all start the same way) -----------------

    @Test
    fun `every mode starts with max gain 50 dB, 124 bands, FFT 2048, 25 ms, smoothing 0.05`() {
        ModeProfiles.ALL.forEach { m ->
            assertTrue(m.id, nearF(m.maxGainDb, 50f))
            assertEquals(m.id, 4, m.bandCountIndex)  // = BandCount.BANDS_124
            assertEquals(m.id, 2, m.fftSizeIndex)    // = FftSize.SIZE_2048
            assertEquals(m.id, 0, m.analysisIntervalIndex) // = AnalysisInterval.FAST (25 ms)
            assertTrue(m.id, nearF(m.smoothingFactor, 0.05f))
            // The indices must map onto the domain enums with the same ordinal.
            assertEquals(m.id, BandCount.BANDS_124, m.bandCount)
            assertEquals(m.id, FftSize.SIZE_2048, m.fftSize)
            assertEquals(m.id, AnalysisInterval.FAST, m.analysisInterval)
        }
    }

    @Test
    fun `all three dynamic EQs are active in every mode with their per-zone bank`() {
        val banks = listOf(
            listOf(2500f, 5000f, 10000f, 20000f),  // EQ 1: trebles
            listOf(63f, 250f, 1000f, 4000f),        // EQ 2: mids
            listOf(20f, 41f, 62f, 83f)              // EQ 3: sub-bass
        )
        val starts = listOf(0, 1, 2) // NEED_BASED / BOTTOM_UP / TOP_DOWN
        ModeProfiles.ALL.forEach { m ->
            assertEquals(m.id, 3, m.eqs.size)
            m.eqs.forEachIndexed { e, eq ->
                assertTrue("$m EQ${e + 1} enabled", eq.enabled)
                assertEquals("$m EQ${e + 1} startFrom", starts[e], eq.startFrom)
                assertEquals("$m EQ${e + 1} bank", 4, eq.support.size)
                eq.support.forEachIndexed { k, band ->
                    assertTrue("$m EQ${e + 1} band ${k + 1} in range",
                        band.frequencyHz > 0f && band.frequencyHz <= 20000f)
                    assertEquals("$m EQ${e + 1} band ${k + 1} at 0 dB", 0f, band.gainDb, 0.001f)
                    assertTrue("$m EQ${e + 1} band ${k + 1} Q2", nearF(band.q, 2f))
                    assertEquals("$m EQ${e + 1} band ${k + 1} frequency",
                        banks[e][k], band.frequencyHz, 0.5f)
                }
            }
        }
    }

    // --- The six agreed values -----------------------------------------------

    @Test
    fun `the six SPLs and their correction/noise-subtraction flags`() {
        // id -> (targetSpl, correction, noiseSubtraction, z, size)
        val expect = mapOf(
            "carro-normal" to listOf(78f, 1, 1, 0.2f, 0.3f),
            "carro-aire-libre" to listOf(97f, 0, 1, 0.5f, 0.5f),
            "estudio-esc1" to listOf(89f, 1, 1, 0.5f, 0.5f),
            "escenario2" to listOf(91f, 0, 0, 0.5f, 0.5f),
            "escenario3" to listOf(93f, 1, 1, 0.9f, 0.9f),
            "pas" to listOf(103f, 1, 1, 0.2f, 0.3f)
        )
        expect.forEach { (id, v) ->
            val m = ModeProfiles.byId(id)
            assertNotNull("missing $id", m)
            if (m == null) return@forEach
            assertTrue(id, nearF(m.targetSpl, v[0]))
            assertEquals("$id correction", v[1] == 1, m.correctionEnabled)
            assertEquals("$id noiseSub", v[2] == 1, m.noiseSubtractionEnabled)
            assertTrue("$id spatial", nearF(m.spatial.z, v[3]) && nearF(m.spatial.size, v[4]))
            // PAs (103) requires the unified 30-110 SPL range in all versions.
            assertTrue("$id SPL in 30-110", m.targetSpl >= 30f && m.targetSpl <= 110f)
        }
    }

    @Test
    fun `spatial positions stay inside their normalized ranges`() {
        ModeProfiles.ALL.forEach { m ->
            assertTrue(m.id, m.spatial.x in -1f..1f)
            assertTrue(m.id, m.spatial.y in -1f..1f)
            assertTrue(m.id, m.spatial.z in 0f..1f)
            assertTrue(m.id, m.spatial.size in 0f..1f)
        }
    }

    // --- Lookup ----------------------------------------------------------------

    @Test
    fun `byId finds each mode and rejects unknown ids`() {
        ModeProfiles.ALL.forEach { m ->
            assertEquals(m.id, m, ModeProfiles.byId(m.id))
        }
        assertNull(ModeProfiles.byId("no-existe"))
        assertNull(ModeProfiles.byId(""))
        assertNull(ModeProfiles.byId(null))
    }
}
