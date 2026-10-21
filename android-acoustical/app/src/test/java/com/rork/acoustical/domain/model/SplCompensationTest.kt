package com.rork.acoustical.domain.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM pin of the spatial compensation law: the Kotlin mirror of
 * `acoustical_spatial_compensate()` (checked by `dsp/test_spatial.cpp` on the
 * CI gate). Both must change together — the Windows app and the VST apply the
 * C law to their real output, the phone applies this one to its test signal.
 */
class SplCompensationTest {

    private fun nearF(a: Float, b: Float, eps: Float = 0.05f) =
        kotlin.math.abs(a - b) <= eps

    @Test
    fun `the default position is neutral with exactly 0 dB and 0 ms`() {
        val c = SplCompensation.calculate(SpatialPosition(), 75f, WorkEnvironmentType.values()[0])
        assertEquals(0f, c.gainAdjustDb, 0f)
        assertEquals(0f, c.delayAdjustMs, 0f)
    }

    @Test
    fun `twice the neutral distance adds 6 dB and the extra propagation delay`() {
        val c = SplCompensation.calculate(0f, 1f, 0.5f)
        assertTrue(nearF(c.gainAdjustDb, 6.02f))
        assertTrue(nearF(c.delayAdjustMs, 7.5f / 343f * 1000f, 0.1f))
    }

    @Test
    fun `closer than the neutral attenuates and never delays`() {
        val c = SplCompensation.calculate(0f, 0f, 0.5f)
        assertTrue(nearF(c.gainAdjustDb, 20f * kotlin.math.log10(1f / 7.5f)))
        assertEquals(0f, c.delayAdjustMs, 0f)
    }

    @Test
    fun `the gain is continuous along z with no 6 dB jump`() {
        var prev = SplCompensation.calculate(0f, 0f, 0.5f).gainAdjustDb
        for (i in 1..1000) {
            val g = SplCompensation.calculate(0f, i / 1000f, 0.5f).gainAdjustDb
            assertTrue(kotlin.math.abs(g - prev) <= 1f)
            prev = g
        }
    }

    @Test
    fun `size and off-axis terms match the C law`() {
        assertTrue(nearF(SplCompensation.calculate(0f, 0.5f, 0f).gainAdjustDb, 3f))
        assertTrue(nearF(SplCompensation.calculate(0f, 0.5f, 1f).gainAdjustDb, -3f))
        assertTrue(nearF(SplCompensation.calculate(1f, 0.5f, 0.5f).gainAdjustDb, 1.5f))
        assertTrue(nearF(SplCompensation.calculate(-1f, 0.5f, 0.5f).gainAdjustDb, 1.5f))
    }

    @Test
    fun `no catalog mode raises the output by more than 6 dB`() {
        ModeProfiles.ALL.forEach { m ->
            val c = SplCompensation.calculate(m.spatial, m.targetSpl, WorkEnvironmentType.values()[0])
            assertTrue(m.id, c.gainAdjustDb <= 6f)
        }
    }

    @Test
    fun `out of range inputs are clamped and non finite ones are neutral`() {
        assertTrue(nearF(SplCompensation.calculate(2f, 0.5f, 0.5f).gainAdjustDb, 1.5f))
        assertTrue(nearF(SplCompensation.calculate(0f, 5f, 0.5f).delayAdjustMs, 7.5f / 343f * 1000f, 0.1f))
        val nan = SplCompensation.calculate(Float.NaN, 0.5f, 0.5f)
        assertEquals(0f, nan.gainAdjustDb, 0f)
        assertEquals(0f, nan.delayAdjustMs, 0f)
        val inf = SplCompensation.calculate(0f, Float.POSITIVE_INFINITY, 0.5f)
        assertEquals(0f, inf.gainAdjustDb, 0f)
        assertEquals(0f, inf.delayAdjustMs, 0f)
    }
}
