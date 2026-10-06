package com.rork.acoustical.domain.audio

import kotlin.math.PI
import kotlin.math.sin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Tests for [SplMeter]: flat (Z) time-domain RMS and the A-weighted
 * spectrum measurement ([SplMeter.computeWeightedSpl]).
 *
 * The A-weighting assertions use the REAL table shipped in the class
 * ([SplMeter.aWeightingFrequencies] / [SplMeter.aWeightingDb], 1/3-octave
 * centers 20 Hz – 20 kHz) — not a reimplementation.
 */
class SplMeterTest {

    @Test
    fun `rms plano - nivel conocido da el dB esperado`() {
        val meter = SplMeter() // calibrationOffset default 120
        // Constant amplitude 0.1 → RMS 0.1 → −20 dBFS → 100 dB SPL.
        val samples = FloatArray(2048) { 0.1f }
        assertEquals(100f, meter.computeSpl(samples), 0.05f)
    }

    @Test
    fun `rms de una senal seno - el rms real de la onda (1/sqrt(2))`() {
        val meter = SplMeter()
        val samples = FloatArray(8192) { i ->
            sin(2.0 * PI * 440.0 * i / 48000.0).toFloat()
        }
        // Peak 1.0 → RMS 1/√2 → −3.01 dBFS → 120 − 3.01 ≈ 117 dB.
        assertEquals(120f - 3.01f, meter.computeSpl(samples), 0.2f)
    }

    @Test
    fun `ponderacion A - un bin a 100 Hz corrige según la tabla real (-19.1 dB)`() {
        val meter = SplMeter()
        // Synthetic 2-bin spectrum: DC bin (skipped, no energy) + one bin at
        // 100 Hz at 0 dBFS.
        val magnitudes = floatArrayOf(-120f, 0f)
        val binFrequencies = floatArrayOf(0f, 100f)
        val a = meter.computeWeightedSpl(magnitudes, binFrequencies)

        // The expected correction is read from the shipped table itself:
        val idx = SplMeter.aWeightingFrequencies.indexOf(100f)
        assertTrue("100 Hz must be a table entry", idx >= 0)
        val tableA = SplMeter.aWeightingDb[idx] // -19.1 dB at 100 Hz
        assertEquals(120f + tableA, a, 0.1f)

        // Sanity: 100 Hz is where the A curve cuts the most — the weighted
        // reading must be far below the flat 120 dB.
        val flat = meter.computeSpl(FloatArray(8) { 1f }) // RMS 1 → 0 dBFS → 120
        assertTrue("A-weighted $a should be >15 dB below flat $flat at 100 Hz", a < flat - 15f)
    }

    @Test
    fun `ponderacion A - a 1 kHz la curva pasa por 0 dB`() {
        val meter = SplMeter()
        val magnitudes = floatArrayOf(-120f, 0f)
        val binFrequencies = floatArrayOf(0f, 1000f)
        val a = meter.computeWeightedSpl(magnitudes, binFrequencies)
        assertEquals(120f, a, 0.1f)
    }

    @Test
    fun `ponderacion A - dos bins suman energia (no dB)`() {
        val meter = SplMeter()
        // Two 0 dBFS bins: 100 Hz (−19.1 dB A) and 1 kHz (0 dB A).
        // Energy = 10^((0−19.1)/10) + 10^(0/10) ≈ 0.0123 + 1 → +0.05 dB.
        val magnitudes = floatArrayOf(-120f, 0f, 0f)
        val binFrequencies = floatArrayOf(0f, 100f, 1000f)
        val a = meter.computeWeightedSpl(magnitudes, binFrequencies)
        assertEquals(120f + 0.05f, a, 0.15f)
    }
}
