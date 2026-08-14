package com.rork.acoustical.domain.console

/**
 * Maps AcoustiCal's internal band corrections to console-specific OSC addresses.
 *
 * Each console manufacturer uses different OSC path conventions for EQ:
 * - X32/M32: /ch/01/eq/1/gain, /ch/01/eq/1/freq, /ch/01/eq/1/q
 * - A&H SQ: /ch/input/1/eq/1/gain, /ch/input/1/eq/1/freq
 * - Yamaha TF: /ch/1/eq/1/gain
 * - Generic: configurable prefix
 */
object ConsoleProfiles {

    /**
     * Generate the OSC address for a specific EQ parameter on the target console.
     */
    fun eqGainAddress(consoleType: ConsoleType, channel: ConsoleChannel, bandIndex: Int): String {
        val ch = formatChannel(consoleType, channel)
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> "/ch/$ch/eq/${bandIndex + 1}/gain"
            ConsoleType.ALLEN_HEATH_SQ -> "/ch/input/$ch/eq/${bandIndex + 1}/gain"
            ConsoleType.ALLEN_HEATH_QU -> "/ch/$ch/eq/${bandIndex + 1}/gain"
            ConsoleType.YAMAHA_TF -> "/ch/$ch/eq/${bandIndex + 1}/gain"
            ConsoleType.GENERIC_OSC -> "/eq/band/${bandIndex + 1}/gain"
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> ""
        }
    }

    fun eqFrequencyAddress(consoleType: ConsoleType, channel: ConsoleChannel, bandIndex: Int): String {
        val ch = formatChannel(consoleType, channel)
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> "/ch/$ch/eq/${bandIndex + 1}/freq"
            ConsoleType.ALLEN_HEATH_SQ -> "/ch/input/$ch/eq/${bandIndex + 1}/freq"
            ConsoleType.ALLEN_HEATH_QU -> "/ch/$ch/eq/${bandIndex + 1}/freq"
            ConsoleType.YAMAHA_TF -> "/ch/$ch/eq/${bandIndex + 1}/freq"
            ConsoleType.GENERIC_OSC -> "/eq/band/${bandIndex + 1}/freq"
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> ""
        }
    }

    fun eqQAddress(consoleType: ConsoleType, channel: ConsoleChannel, bandIndex: Int): String {
        val ch = formatChannel(consoleType, channel)
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> "/ch/$ch/eq/${bandIndex + 1}/q"
            ConsoleType.ALLEN_HEATH_SQ -> "/ch/input/$ch/eq/${bandIndex + 1}/q"
            ConsoleType.ALLEN_HEATH_QU -> "/ch/$ch/eq/${bandIndex + 1}/q"
            ConsoleType.YAMAHA_TF -> "/ch/$ch/eq/${bandIndex + 1}/q"
            ConsoleType.GENERIC_OSC -> "/eq/band/${bandIndex + 1}/q"
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> ""
        }
    }

    /**
     * Convert AcoustiCal gain (dB, float) to the console's internal representation.
     * X32 uses 0.0–1.0 floats for EQ gain where 0.5 = 0 dB.
     * A&H uses dB directly as float.
     * Yamaha uses 0.0–1.0 similarly to X32.
     */
    fun gainToConsoleValue(consoleType: ConsoleType, gainDb: Float): Float {
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> {
                // X32 EQ gain: -15 to +15 dB maps to 0.0 to 1.0 (0.5 = 0 dB)
                ((gainDb + 15f) / 30f).coerceIn(0f, 1f)
            }
            ConsoleType.ALLEN_HEATH_SQ,
            ConsoleType.ALLEN_HEATH_QU -> {
                // A&H sends dB directly
                gainDb.coerceIn(-24f, 24f)
            }
            ConsoleType.YAMAHA_TF -> {
                // Yamaha TF: ±18 dB maps to 0.0–1.0
                ((gainDb + 18f) / 36f).coerceIn(0f, 1f)
            }
            ConsoleType.GENERIC_OSC -> gainDb
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> gainDb
        }
    }

    /**
     * Convert frequency Hz to console's internal value.
     * X32/M32 uses log scale: 0.0 (20 Hz) to 1.0 (20 kHz)
     */
    fun freqToConsoleValue(consoleType: ConsoleType, freqHz: Float): Float {
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> {
                val logMin = kotlin.math.ln(20f)
                val logMax = kotlin.math.ln(20000f)
                ((kotlin.math.ln(freqHz) - logMin) / (logMax - logMin)).coerceIn(0f, 1f)
            }
            ConsoleType.ALLEN_HEATH_SQ,
            ConsoleType.ALLEN_HEATH_QU -> freqHz
            ConsoleType.YAMAHA_TF -> freqHz
            ConsoleType.GENERIC_OSC -> freqHz
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> freqHz
        }
    }

    /**
     * Convert Q value to console's internal value.
     * Most consoles accept Q directly, but X32 uses 0.1–50 mapped to 0.0–1.0.
     */
    fun qToConsoleValue(consoleType: ConsoleType, q: Float): Float {
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> {
                ((q - 0.1f) / 49.9f).coerceIn(0f, 1f)
            }
            ConsoleType.ALLEN_HEATH_SQ,
            ConsoleType.ALLEN_HEATH_QU -> q
            ConsoleType.YAMAHA_TF -> q
            ConsoleType.GENERIC_OSC -> q
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> q
        }
    }

    /**
     * Format the channel number for the OSC address.
     * X32 expects zero-padded two-digit: 01, 02, ..., 32
     * A&H uses plain numbers
     */
    private fun formatChannel(consoleType: ConsoleType, channel: ConsoleChannel): String {
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> channel.bus.toString().padStart(2, '0')
            ConsoleType.ALLEN_HEATH_SQ,
            ConsoleType.ALLEN_HEATH_QU -> channel.bus.toString()
            ConsoleType.YAMAHA_TF -> channel.bus.toString()
            ConsoleType.GENERIC_OSC -> channel.bus.toString()
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> channel.bus.toString()
        }
    }

    /**
     * The number of parametric EQ bands the console supports per channel.
     */
    fun eqBandCountFor(consoleType: ConsoleType): Int {
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> 6
            ConsoleType.ALLEN_HEATH_SQ -> 4
            ConsoleType.ALLEN_HEATH_QU -> 4
            ConsoleType.YAMAHA_TF -> 4
            ConsoleType.GENERIC_OSC -> 8
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> 0
        }
    }

    /**
     * Default EQ band center frequencies per console type (Hz).
     */
    fun defaultBandFrequencies(consoleType: ConsoleType): FloatArray {
        return when (consoleType) {
            ConsoleType.BEHRINGER_X32,
            ConsoleType.MIDAS_M32 -> floatArrayOf(100f, 250f, 500f, 1000f, 2500f, 8000f)
            ConsoleType.ALLEN_HEATH_SQ,
            ConsoleType.ALLEN_HEATH_QU -> floatArrayOf(250f, 1000f, 4000f, 8000f)
            ConsoleType.YAMAHA_TF -> floatArrayOf(200f, 800f, 3000f, 10000f)
            ConsoleType.GENERIC_OSC -> floatArrayOf(60f, 170f, 350f, 1000f, 3500f, 6000f, 12000f, 16000f)
            ConsoleType.USB_DIRECT, ConsoleType.MANUAL -> FloatArray(0)
        }
    }
}
