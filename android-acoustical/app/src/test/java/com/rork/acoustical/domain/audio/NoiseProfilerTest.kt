package com.rork.acoustical.domain.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Tests for [NoiseProfiler.subtractNoise] (bug A2 regression).
 *
 * The old formula did the subtraction in dB and, with negative levels,
 * "measured - noise*(1-ratio)" became an ADDITION that amplified the noise
 * by up to +40 dB. The current formula works in the linear domain, which
 * guarantees the two invariants tested here:
 *  - result never exceeds the measured level (no amplification),
 *  - result never goes below the noise floor.
 */
class NoiseProfilerTest {

    /** Builds a profiler whose captured profile is exactly [profile]. */
    private fun profilerWith(profile: FloatArray): NoiseProfiler {
        val p = NoiseProfiler(binCount = profile.size, maxCaptureFrames = 2)
        p.startCapture()
        p.feedFrame(profile)
        p.feedFrame(profile) // second identical frame → average = profile
        assertTrue(p.hasProfile())
        return p
    }

    @Test
    fun `sin perfil - el medido vuelve intacto (misma instancia)`() {
        val p = NoiseProfiler(binCount = 4)
        val measured = floatArrayOf(-40f, -50f, -60f, -70f)
        assertSame(measured, p.subtractNoise(measured))
    }

    @Test
    fun `medido por debajo o en el piso - el resultado se queda en el ruido`() {
        val p = profilerWith(floatArrayOf(-60f, -60f))
        val out = p.subtractNoise(floatArrayOf(-80f, -60f))
        assertEquals(-60f, out[0], 1e-4f)
        assertEquals(-60f, out[1], 1e-4f)
    }

    @Test
    fun `zona de transicion - el resultado nunca supera al medido (bug A2)`() {
        val p = profilerWith(floatArrayOf(-60f))
        val gateRange = 6f
        // Every point strictly between noise and noise+gateRange: the old
        // dB-adding formula could push the output ABOVE the measured level;
        // now the linear subtraction keeps result within [noise, measured].
        for (k in 1 until 60) {
            val measured = -60f + k * 0.1f
            val out = p.subtractNoise(floatArrayOf(measured))[0]
            assertTrue(
                "k=$k: out=$out > measured=$measured (amplificó el ruido)",
                out <= measured + 1e-3f
            )
            assertTrue("k=$k: out=$out < noise=-60", out >= -60f - 1e-3f)
        }
        // Near the top of the gate the signal almost passes through.
        val justBelow = -60f + 5.9f
        val out = p.subtractNoise(floatArrayOf(justBelow))[0]
        assertTrue("just below gate: out=$out > measured=$justBelow", out <= justBelow + 1e-3f)
    }

    @Test
    fun `medido muy por encima del ruido (fuera del gate) - se conserva tal cual`() {
        val p = profilerWith(floatArrayOf(-60f))
        val out = p.subtractNoise(floatArrayOf(-30f))
        assertEquals(-30f, out[0], 1e-4f)
    }

    @Test
    fun `perfil multibanda - se procesa bin a bin`() {
        val p = profilerWith(floatArrayOf(-60f, -50f, -40f))
        val measured = floatArrayOf(-90f, -47f, -40f)
        val out = p.subtractNoise(measured)
        assertEquals(-60f, out[0], 1e-4f) // below floor → floor
        assertTrue("middle bin amplified: ${out[1]}", out[1] <= -47f + 1e-3f) // transition
        assertEquals(-40f, out[2], 1e-4f) // above floor → unchanged
    }
}
