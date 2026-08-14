package com.rork.acoustical.domain.audio

import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.PI
import kotlin.math.sqrt

/**
 * In-place radix-2 Cooley-Tukey FFT implementation in pure Kotlin.
 * Operates on interleaved real/imaginary arrays for zero allocations in the hot path.
 *
 * @param size must be a power of two (512, 1024, 2048, 4096, 8192).
 */
class FftProcessor(private val size: Int) {

    private val real: FloatArray = FloatArray(size)
    private val imag: FloatArray = FloatArray(size)
    private val window: FloatArray = FloatArray(size)

    private val bitReverseTable: IntArray
    private val magnitudeDb: FloatArray = FloatArray(size / 2)
    private val binFrequencies: FloatArray = FloatArray(size / 2)

    init {
        require(size > 0 && size and (size - 1) == 0) { "FFT size must be power of 2, got $size" }
        bitReverseTable = computeBitReverseTable(size)
        initHammingWindow()
    }

    val binCount: Int get() = size / 2

    private fun initHammingWindow() {
        for (i in 0 until size) {
            window[i] = (0.54f - 0.46f * cos(2.0 * PI * i / (size - 1))).toFloat()
        }
    }

    private fun computeBitReverseTable(n: Int): IntArray {
        val table = IntArray(n)
        var bits = 0
        var temp = n
        while (temp > 1) {
            temp = temp ushr 1
            bits++
        }
        for (i in 0 until n) {
            var rev = 0
            var x = i
            for (j in 0 until bits) {
                rev = (rev shl 1) or (x and 1)
                x = x ushr 1
            }
            table[i] = rev
        }
        return table
    }

    /**
     * Runs the FFT on [input] (must be exactly [size] samples).
     * Applies a Hamming window, performs bit-reversal reordering, then the
     * butterfly operations. Returns a FloatArray of magnitude in dB for each
     * frequency bin (size/2 values).
     *
     * @param input time-domain audio samples, mono, range [-1, 1]
     * @param sampleRate sample rate in Hz, used to compute bin frequencies
     * @return array of dB magnitudes, one per bin
     */
    fun computeMagnitudesDb(input: FloatArray, sampleRate: Int): FloatArray {
        require(input.size >= size) { "Input must have at least $size samples, got ${input.size}" }

        // Apply window and prepare arrays
        for (i in 0 until size) {
            real[i] = input[i] * window[i]
            imag[i] = 0f
        }

        // Bit-reversal permutation
        for (i in 0 until size) {
            val j = bitReverseTable[i]
            if (j > i) {
                val tmpR = real[i]; real[i] = real[j]; real[j] = tmpR
                val tmpI = imag[i]; imag[i] = imag[j]; imag[j] = tmpI
            }
        }

        // Cooley-Tukey butterfly
        var stageSize = 2
        while (stageSize <= size) {
            val halfStage = stageSize / 2
            val angleStep = -2.0 * PI / stageSize
            for (i in 0 until halfStage) {
                val angle = angleStep * i
                val wReal = cos(angle).toFloat()
                val wImag = kotlin.math.sin(angle).toFloat()
                var j = i
                while (j < size) {
                    val k = j + halfStage
                    val tReal = wReal * real[k] - wImag * imag[k]
                    val tImag = wReal * imag[k] + wImag * real[k]
                    real[k] = real[j] - tReal
                    imag[k] = imag[j] - tImag
                    real[j] = real[j] + tReal
                    imag[j] = imag[j] + tImag
                    j += stageSize
                }
            }
            stageSize = stageSize shl 1
        }

        // Compute magnitudes in dB
        val binHz = sampleRate.toFloat() / size.toFloat()
        val logBase = ln(10f)
        val normFactor = 2f / size

        for (i in 0 until binCount) {
            val mag = sqrt(real[i] * real[i] + imag[i] * imag[i]) * normFactor
            magnitudeDb[i] = if (mag > 1e-10f) {
                20f * (ln(mag) / logBase)
            } else {
                -120f
            }
            binFrequencies[i] = i * binHz
        }

        return magnitudeDb
    }

    fun getBinFrequencies(sampleRate: Int): FloatArray {
        val binHz = sampleRate.toFloat() / size.toFloat()
        for (i in 0 until binCount) {
            binFrequencies[i] = i * binHz
        }
        return binFrequencies
    }
}
