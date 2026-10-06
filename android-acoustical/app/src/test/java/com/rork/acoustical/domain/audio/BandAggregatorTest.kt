package com.rork.acoustical.domain.audio

import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.StandardFrequencies
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

/**
 * Golden-vector test for [BandAggregator].
 *
 * The fixture `src/test/resources/golden_bands.json` (32 bins 0-16 kHz,
 * 1 dB/bin ramp, floor -60 dB, 10 bands of 1/6 octave) documents input bins,
 * floor and expected band levels to 3 decimals. The FALLBACK Kotlin path is
 * what runs on the JVM (no native lib here), so that is what this test
 * verifies, with a tolerance of 1e-2 dB.
 *
 * The native (shared C core) path is verified on the C side in dsp/
 * (dsp/test_dsp.cpp against dsp/golden_bands.json, which must stay in sync
 * with this fixture).
 */
class BandAggregatorTest {

    // --- Fixture (parsed once) ---
    private val root: Map<String, kotlinx.serialization.json.JsonElement> by lazy {
        val text = javaClass.getResource("/golden_bands.json")?.readText()
            ?: error("golden_bands.json no encontrado en test resources")
        Json.parseToJsonElement(text).jsonObject.mapValues { it.value }
    }

    private fun floatArray(key: String): FloatArray =
        root[key]!!.jsonArray.map { it.jsonPrimitive.float }.toFloatArray()

    private val bandFrequencies: FloatArray by lazy { floatArray("bandFrequencies") }
    private val magnitudesDb: FloatArray by lazy { floatArray("magnitudesDb") }
    private val binFrequencies: FloatArray by lazy { floatArray("binFrequencies") }
    private val noiseFloorDb: Float by lazy { root["noiseFloorDb"]!!.jsonPrimitive.float }
    private val expected: FloatArray by lazy { floatArray("expectedLevelsDb") }

    @Test
    fun `kotlin fallback reproduces the golden vector`() {
        val got = BandAggregator.aggregateKotlin(
            bandFrequencies, magnitudesDb, binFrequencies, noiseFloorDb
        )
        assertEquals("mismatched band count", expected.size, got.size)
        for (b in expected.indices) {
            assertTrue(
                "band $b (${bandFrequencies[b]} Hz): got ${got[b]}, expected ${expected[b]}",
                abs(got[b] - expected[b]) <= 1e-2
            )
        }
    }

    @Test
    fun `gating excludes bins at or below the floor`() {
        // Bin 15 (7500 Hz) sits exactly AT the floor: it must not count.
        // Without the strict '>' the 8000 Hz band would be
        // (-60 + -59 + -58) / 3 = -59.0 instead of (-59 + -58) / 2 = -58.5.
        val got = BandAggregator.aggregateKotlin(
            bandFrequencies, magnitudesDb, binFrequencies, noiseFloorDb
        )
        assertEquals(-58.5f, got[8], 1e-2f)
    }

    @Test
    fun `aggregate dispatches to the kotlin fallback on the jvm`() {
        // En JVM no hay .so: aggregate() debe ser idéntico al fallback
        // canónico (en dispositivo con el .so cargado iría por el core C).
        assertFalse("el .so nativo no debe estar disponible en este test JVM",
            NativeDsp.isAvailable)
        val viaDispatch = BandAggregator.aggregate(
            bandFrequencies, magnitudesDb, binFrequencies, noiseFloorDb
        )
        val direct = BandAggregator.aggregateKotlin(
            bandFrequencies, magnitudesDb, binFrequencies, noiseFloorDb
        )
        assertEquals(direct.toList(), viaDispatch.toList())
    }

    @Test
    fun `no gating averages every bin in the window`() {
        // Con NEGATIVE_INFINITY no hay gating: la banda 8000 Hz promedia
        // sus 3 bins (-60, -59, -58) = -59.0 en lugar de -58.5.
        val bands = floatArrayOf(8000f)
        val got = BandAggregator.aggregateKotlin(
            bands, magnitudesDb, binFrequencies, Float.NEGATIVE_INFINITY
        )
        assertEquals(-59.0f, got[0], 1e-2f)
    }

    @Test
    fun `ratio switches between 1/12 and 1/6 octave by band count`() {
        // Dos bins a 500 Hz de distancia alrededor de 8 kHz (7500 y 8500).
        // Con ratio 1/6 de octava (10 bandas) ambos caen en la banda 8000;
        // con ratio 1/12 de octava (124 bandas, ultra) se reparten en varias
        // bandas (las ventanas 1/12 se solapan): ambas configuraciones deben
        // dar resultados distintos — el ratio depende del nº de bandas.
        val bins = FloatArray(32) { i -> i * 500f }
        val mags = FloatArray(32) { -120f }
        mags[15] = -50f   // 7500 Hz
        mags[17] = -50f   // 8500 Hz
        val ten = BandAggregator.aggregateKotlin(
            StandardFrequencies.tenBand, mags, bins, -120f
        )
        assertEquals("los dos bins caen en la banda 8000 Hz (1/6 de octava)",
            -50f, ten[8], 1e-2f)
        val ultra = BandAggregator.aggregateKotlin(
            StandardFrequencies.forCount(BandCount.BANDS_124), mags, bins, -120f
        )
        val tenActive = ten.count { it > -120f }
        val ultraActive = ultra.count { it > -120f }
        assertNotEquals(
            "el ratio 1/6 (10 bandas) y 1/12 (124 bandas) dan ventanas distintas",
            tenActive, ultraActive
        )
        assertTrue("las bandas ultra reparten los dos bins en varias bandas",
            ultraActive >= 2)
    }
}
