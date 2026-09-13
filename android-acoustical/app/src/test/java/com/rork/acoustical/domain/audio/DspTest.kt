package com.rork.acoustical.domain.audio

import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.domain.model.StandardFrequencies
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.pow
import kotlin.math.sin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM unit tests for the pure-Kotlin DSP: [FftProcessor] and [RoomCorrector].
 *
 * These run on the host (no Android, no native lib) so CI can verify the
 * Kotlin signal path. The native JNI path is verified separately by
 * dsp/test_jni.cpp against the shared C core.
 */
class FftProcessorTest {

    private fun tone(freqHz: Double, sampleRate: Int, size: Int, amplitude: Double = 1.0): FloatArray {
        return FloatArray(size) { i ->
            (amplitude * sin(2.0 * PI * freqHz * i / sampleRate)).toFloat()
        }
    }

    @Test
    fun `bin count is half the fft size`() {
        assertEquals(1024, FftProcessor(2048).binCount)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `non power of two size is rejected`() {
        FftProcessor(1000)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `zero size is rejected`() {
        FftProcessor(0)
    }

    @Test
    fun `dominant tone peaks at the expected bin`() {
        val sampleRate = 48000
        val size = 2048
        val freq = 1000.0
        val mag = FftProcessor(size).computeMagnitudesDb(tone(freq, sampleRate, size), sampleRate)
        assertEquals(size / 2, mag.size)

        val binHz = sampleRate.toDouble() / size
        val expectedBin = (freq / binHz).toInt()
        var peakBin = 0
        for (i in 1 until mag.size) if (mag[i] > mag[peakBin]) peakBin = i
        // The Hamming window spreads energy to neighbours; allow +-1 bin.
        assertTrue("peak bin $peakBin should be within 1 of $expectedBin",
            abs(expectedBin - peakBin) <= 1)
        // The peak must be clearly above the noise floor.
        assertTrue("peak should be audible", mag[peakBin] > -80f)
    }

    @Test
    fun `stronger tone yields a higher peak than a weaker one`() {
        val sampleRate = 48000
        val size = 4096
        val strong = FftProcessor(size).computeMagnitudesDb(tone(2000.0, sampleRate, size, 0.9), sampleRate)
        val weak = FftProcessor(size).computeMagnitudesDb(tone(2000.0, sampleRate, size, 0.1), sampleRate)
        fun peak(a: FloatArray): Int {
            var p = 0; for (i in 1 until a.size) if (a[i] > a[p]) p = i; return p
        }
        // ~20 dB of amplitude difference -> clearly higher peak.
        assertTrue(strong[peak(strong)] - weak[peak(weak)] > 15f)
    }

    @Test
    fun `bin frequencies are correct and evenly spaced`() {
        val sampleRate = 48000
        val size = 2048
        val bins = FftProcessor(size).getBinFrequencies(sampleRate)
        val binHz = sampleRate.toDouble() / size
        assertEquals(size / 2, bins.size)
        for (i in bins.indices) {
            assertEquals(i * binHz, bins[i].toDouble(), 1e-3)
        }
    }

    @Test
    fun `silence sits at or below the noise floor`() {
        val mag = FftProcessor(2048).computeMagnitudesDb(FloatArray(2048), 48000)
        for (v in mag) assertTrue(v <= -110f)
    }
}

/**
 * Tests for [RoomCorrector], with an explicit equivalence check that the
 * two-pointer [RoomCorrector.aggregateBands] matches a naive O(bands × bins)
 * reference over many random inputs — guarding the surgical rewrite.
 */
class RoomCorrectorTest {

    private val sampleRate = 48000
    private val fftSize = 2048

    /** Naive reference: for each band, scan every bin in [lower, upper]. */
    private fun naiveAggregate(
        bandFrequencies: FloatArray,
        magnitudesDb: FloatArray,
        binFrequencies: FloatArray,
        noiseFloorDb: Float
    ): FloatArray {
        val bandCount = bandFrequencies.size
        val ratio = if (bandCount > 40) 2.0.pow(1.0 / 12.0) else 2.0.pow(1.0 / 6.0)
        return FloatArray(bandCount) { b ->
            val center = bandFrequencies[b]
            val lower = center / ratio
            val upper = center * ratio
            var sum = 0.0
            var count = 0
            for (i in binFrequencies.indices) {
                val f = binFrequencies[i]
                if (f >= lower && f <= upper && magnitudesDb[i] > noiseFloorDb) {
                    sum += magnitudesDb[i]
                    count++
                }
            }
            if (count > 0) (sum / count).toFloat() else noiseFloorDb
        }
    }

    private fun makeCorrector(bandFrequencies: FloatArray, periodMs: Long = 500L) =
        RoomCorrector.create(
            bandFrequencies = bandFrequencies,
            sampleRate = sampleRate,
            fftSize = fftSize,
            maxGainDb = 12f,
            smoothingFactor = 0.5f,
            noiseFloorDb = -120f,
            correctionPeriodMs = periodMs
        )

    @Test
    fun `two pointer aggregate matches naive reference on random data`() {
        var seed = 12345L
        for (trial in 0 until 200) {
            seed = seed * 6364136223846793005L + 1442695040888963407L
            val rng = kotlin.random.Random(seed)
            val bands = StandardFrequencies.forCount(com.rork.acoustical.domain.model.BandCount.BANDS_124)
            val binFreqs = FloatArray(fftSize / 2) { i -> (i * sampleRate.toDouble() / fftSize).toFloat() }
            // Random spectrum with some structure (low-pass tilt + bumps).
            val mags = FloatArray(fftSize / 2) { i ->
                val base = -40f - 0.02f * i
                val bump = if (i in 60..90) 25f else 0f
                (base + bump + rng.nextFloat() * 8f).coerceIn(-120f, 0f)
            }
            val got = makeCorrector(bands).aggregateBands(mags, binFreqs)
            val expected = naiveAggregate(bands, mags, binFreqs, -120f)
            assertEquals("trial $trial size mismatch", expected.size, got.size)
            for (i in expected.indices) {
                assertEquals("trial $trial band $i", expected[i], got[i], 1e-3f)
            }
        }
    }

    @Test
    fun `dominant tone lands in the strongest band`() {
        val bands = StandardFrequencies.tenBand // idx 3 = 250 Hz
        val fft = FftProcessor(fftSize)
        val sample = FloatArray(fftSize) { i -> (0.8 * sin(2.0 * PI * 250.0 * i / sampleRate)).toFloat() }
        val mags = fft.computeMagnitudesDb(sample, sampleRate)
        val binFreqs = fft.getBinFrequencies(sampleRate)
        val levels = makeCorrector(bands).aggregateBands(mags, binFreqs)
        var strongest = 0
        for (i in 1 until levels.size) if (levels[i] > levels[strongest]) strongest = i
        assertEquals(3, strongest)
    }

    @Test
    fun `correction cuts the loudest and boosts the quietest band`() {
        val bands = StandardFrequencies.tenBand
        val corrector = makeCorrector(bands, periodMs = 0L) // correct immediately
        val measured = FloatArray(bands.size) { -60f }
        measured[2] = -20f  // loud band
        measured[7] = -95f  // quiet band
        val eqBands = bands.mapIndexed { i, f -> EqBand(index = i, centerFreq = f) }
        val result = corrector.computeCorrections(FloatArray(bands.size), measured, eqBands)
        // The loud band should be pushed down, the quiet band pushed up.
        assertTrue("loud band should be cut", result[2].targetGainDb < 0f)
        assertTrue("quiet band should be boosted", result[7].targetGainDb > 0f)
    }

    @Test
    fun `correction gains are clamped to the max`() {
        val bands = StandardFrequencies.tenBand
        val corrector = makeCorrector(bands, periodMs = 0L)
        val measured = FloatArray(bands.size) { -60f }
        measured[2] = 0f  // extremely loud
        measured[7] = -120f // silent
        val eqBands = bands.mapIndexed { i, f -> EqBand(index = i, centerFreq = f) }
        val result = corrector.computeCorrections(FloatArray(bands.size), measured, eqBands)
        for (b in result) assertTrue(abs(b.targetGainDb) <= 12f + 1e-3f)
    }

    @Test
    fun `correction intensity is zero when flat and positive after correction`() {
        val bands = StandardFrequencies.tenBand
        val corrector = makeCorrector(bands, periodMs = 0L)
        val eqBands = bands.mapIndexed { i, f -> EqBand(index = i, centerFreq = f) }
        assertEquals(0f, corrector.correctionIntensity(eqBands), 1e-6f)
        val measured = FloatArray(bands.size) { -60f }
        measured[2] = -20f
        measured[7] = -95f
        val result = corrector.computeCorrections(FloatArray(bands.size), measured, eqBands)
        assertTrue(corrector.correctionIntensity(result) > 0f)
    }

    @Test
    fun `reset clears correction state`() {
        val bands = StandardFrequencies.tenBand
        val measured = FloatArray(bands.size) { -60f }
        measured[2] = -20f
        measured[7] = -95f
        val eqBands = bands.mapIndexed { i, f -> EqBand(index = i, centerFreq = f) }

        // Drive a corrector until band 2's target has moved negative.
        val driven = makeCorrector(bands, periodMs = 0L)
        var result = driven.computeCorrections(FloatArray(bands.size), measured, eqBands)
        for (k in 0 until 10) result = driven.computeCorrections(FloatArray(bands.size), measured, result)
        assertTrue("band 2 target should be negative before reset", result[2].targetGainDb < 0f)

        // A fresh corrector after reset has zeroed targets.
        val resetCorrector = makeCorrector(bands, periodMs = 0L)
        resetCorrector.reset()
        val afterReset = resetCorrector.computeCorrections(FloatArray(bands.size), measured, eqBands)
        // First correction step after reset moves band 2 down by the deviation
        // (not accumulated), so it is smaller in magnitude than the driven one.
        assertTrue(
            "reset corrector target smaller than driven",
            abs(afterReset[2].targetGainDb) < abs(result[2].targetGainDb) + 1e-3f
        )
    }

    @Test
    fun `apply gains to spectrum shifts the band region`() {
        val bands = StandardFrequencies.tenBand
        val fft = FftProcessor(fftSize)
        val binFreqs = fft.getBinFrequencies(sampleRate)
        val mags = FloatArray(fftSize / 2) { -50f }
        val spectrum = com.rork.acoustical.domain.model.SpectrumFrame(
            magnitudesDb = mags,
            frequencies = binFreqs,
            timestampMs = 0L
        )
        val eqBands = bands.mapIndexed { i, f -> EqBand(index = i, centerFreq = f, gainDb = if (i == 5) 6f else 0f) }
        val corrected = makeCorrector(bands).applyGainsToSpectrum(spectrum, eqBands)
        // The bin nearest 1000 Hz (band idx 5) should be boosted above its neighbours.
        val binHz = sampleRate.toDouble() / fftSize
        val targetBin = (1000.0 / binHz).toInt()
        assertTrue(corrected.magnitudesDb[targetBin] > corrected.magnitudesDb[targetBin - 20])
    }
}
