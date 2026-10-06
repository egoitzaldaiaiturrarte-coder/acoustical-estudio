package com.rork.acoustical.domain.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.log10
import kotlin.math.pow

/**
 * JVM tests for [Rt60Estimator].
 *
 * The estimator must recover the RT60 of a synthetic exponential energy
 * decay: with a decay of `d` dB per frame the RT60 is 60 dB / (d dB /
 * interval) — fed as band dB levels, so the test also pins the
 * 10^(dB/10) energy conversion (a 20·log10 "amplitude" conversion would fit a
 * slope half as steep and fail the tolerance).
 */
class Rt60EstimatorTest {

    @Test
    fun `exponential decay recovers the known rt60`() {
        // 1 banda, 50 ms entre frames, decaimiento de 5 dB por frame:
        // pendiente -0.1 dB/ms -> RT60 = 60 dB / 0.1 dB/ms = 600 ms.
        val est = Rt60Estimator(bandCount = 1, historySize = 64, sampleIntervalMs = 50)
        val e0 = 10.0.pow(-20.0 / 10.0) // energía lineal de partida (-20 dB)
        val r = 10.0.pow(-5.0 / 10.0)   // ratio de energía por frame (-5 dB)
        // Más frames de las que guarda el anillo: la ventana final (los 64
        // últimos) sigue siendo una exponencial pura -> la recta de dB es
        // exacta y la pendiente medida debe ser la teórica.
        repeat(80) { j ->
            val levelDb = (10.0 * log10(e0 * r.pow(j.toDouble()))).toFloat()
            est.feedFrame(floatArrayOf(levelDb))
        }
        assertEquals(600.0, est.currentRt60Ms.toDouble(), 5.0)
    }

    @Test
    fun `slower decay gives a proportionally larger rt60`() {
        // Decaimiento de 1.5 dB por frame (50 ms) = -0.03 dB/ms:
        // RT60 = 60 / 0.03 = 2000 ms. Un segundo punto (con el primero ya
        // fija la conversión 10^(dB/10)) confirma la escala pendiente->RT60.
        val est = Rt60Estimator(bandCount = 1, historySize = 64, sampleIntervalMs = 50)
        val e0 = 10.0.pow(-20.0 / 10.0) // energía lineal de partida (-20 dB)
        val r = 10.0.pow(-1.5 / 10.0)   // ratio de energía por frame (-1.5 dB)
        repeat(80) { j ->
            val levelDb = (10.0 * log10(e0 * r.pow(j.toDouble()))).toFloat()
            est.feedFrame(floatArrayOf(levelDb))
        }
        assertEquals(2000.0, est.currentRt60Ms.toDouble(), 10.0)
    }

    @Test
    fun `band count is exposed so callers can rebuild on config change`() {
        val est = Rt60Estimator(bandCount = 16)
        assertEquals(16, est.bandCount)
        // Con 16 bandas el frame debe llevar 16 niveles (los extra se ignoran).
        val levels = FloatArray(31) { -20f }
        est.feedFrame(levels)
        assertTrue("tras un feed el estimador debe seguir listo", est.bandCount == 16)
    }
}
