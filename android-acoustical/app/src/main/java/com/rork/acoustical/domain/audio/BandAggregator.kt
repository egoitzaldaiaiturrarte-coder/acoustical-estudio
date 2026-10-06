package com.rork.acoustical.domain.audio

import kotlin.math.pow

/**
 * Único punto de agregación de bins crudos del FFT en bandas 1/N de octava.
 *
 * Antes había 3-4 copias del mismo algoritmo (RoomCorrector,
 * InternalCaptureService, NoiseProfiler…), cada una con su ratio de borde y
 * su gating, de modo que los "niveles por banda" no eran comparables entre
 * sí. Todo pasa ahora por aquí:
 *
 *  - Si el core DSP nativo (el mismo C que corre la app Windows, ver
 *    `dsp/acoustical_dsp.c`) está cargado, se delega en
 *    [NativeDsp.nativeAggregateBands].
 *  - Si no (tests JVM, build sin lib nativa), se usa el fallback Kotlin
 *    [aggregateKotlin], que es el algoritmo canónico: idéntico al del core C.
 *
 * Algoritmo (idéntico en las dos vías):
 *  - ratio de borde de banda: 2^(1/12) si hay más de 40 bandas (1/12 de
 *    octava, ultra-resolución) y 2^(1/6) en caso contrario (1/6 de octava).
 *  - Para cada banda centrada en `f`, se promedian los bins cuya frecuencia
 *    cae en [f/ratio, f·ratio] y cuya magnitud supera [noiseFloorDb].
 *  - Una banda sin bin cualificado toma el valor [noiseFloorDb].
 *
 * El umbral [noiseFloorDb] es PARÁMETRO (no constante) para que cada
 * caller elija su gating: el motor pasa el umbral de su config; un caller
 * que quiera agregación pura (sin gating) pasa `Float.NEGATIVE_INFINITY`.
 *
 * Vector de referencia (golden): `app/src/test/resources/golden_bands.json`,
 * mantenido en sincronía con `dsp/golden_bands.json` (el test del lado C
 * vive en dsp/; aquí se verifica el fallback Kotlin).
 */
object BandAggregator {

    /**
     * Agrega [magnitudesDb] en las bandas centradas en [bandFrequencies].
     *
     * @param noiseFloorDb gating de la agregación: solo cuentan los bins que
     *   lo superan; una banda vacía toma este valor. [Float.NEGATIVE_INFINITY]
     *   = sin gating (cuentan todos los bins).
     */
    fun aggregate(
        bandFrequencies: FloatArray,
        magnitudesDb: FloatArray,
        binFrequencies: FloatArray,
        noiseFloorDb: Float = Float.NEGATIVE_INFINITY
    ): FloatArray {
        if (bandFrequencies.isEmpty()) return FloatArray(0)
        // Vía rápida: el core C compartido (misma agregación que Windows).
        if (NativeDsp.isAvailable) {
            val out = FloatArray(bandFrequencies.size)
            NativeDsp.nativeAggregateBands(
                bandFrequencies, magnitudesDb, binFrequencies, noiseFloorDb, out
            )
            return out
        }
        return aggregateKotlin(bandFrequencies, magnitudesDb, binFrequencies, noiseFloorDb)
    }

    /**
     * Implementación Kotlin canónica (prueba en JVM, sin lib nativa).
     *
     * Doble puntero O(bins + bandas): bordes de banda y frecuencias de bin
     * están ordenadas, así la ventana [lower, upper] solo avanza hacia
     * adelante. Igual semántica que el core C (los bordes son inclusivos).
     */
    fun aggregateKotlin(
        bandFrequencies: FloatArray,
        magnitudesDb: FloatArray,
        binFrequencies: FloatArray,
        noiseFloorDb: Float = Float.NEGATIVE_INFINITY
    ): FloatArray {
        val bandCount = bandFrequencies.size
        val bandLevels = FloatArray(bandCount)
        val ratio = if (bandCount > 40) 2.0.pow(1.0 / 12.0) else 2.0.pow(1.0 / 6.0)
        val totalBins = binFrequencies.size

        var start = 0
        var end = 0
        for (b in 0 until bandCount) {
            val center = bandFrequencies[b]
            val lower = center / ratio
            val upper = center * ratio

            while (start < totalBins && binFrequencies[start] < lower) start++
            if (end < start) end = start
            while (end < totalBins && binFrequencies[end] <= upper) end++

            var sum = 0.0
            var count = 0
            for (i in start until end) {
                if (i >= magnitudesDb.size) break
                val mag = magnitudesDb[i]
                if (mag > noiseFloorDb) {
                    sum += mag
                    count++
                }
            }
            bandLevels[b] = if (count > 0) (sum / count).toFloat() else noiseFloorDb
        }
        return bandLevels
    }
}
