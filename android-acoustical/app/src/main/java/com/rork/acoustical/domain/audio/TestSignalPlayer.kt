package com.rork.acoustical.domain.audio

import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Handler
import android.os.Looper
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Test signal types available to verify that an output actually sounds.
 */
enum class TestSignalType(val label: String, val shortLabel: String) {
    PINK_NOISE("Ruido rosa", "Rosa"),
    SWEEP("Barrido 20 Hz - 20 kHz", "Barrido"),
    TONE("Tono 1 kHz", "Tono")
}

/**
 * Multiroute test signal player: every output gets its own AudioTrack routed
 * to its physical device (phone speaker, Bluetooth A2DP, USB, wired) via
 * [AudioTrack.setPreferredDevice]. Several outputs can sound at the same time,
 * each with its own signal, volume, gain and delay.
 *
 * Playback uses static AudioTrack buffers with completion markers, so
 * [onFinished] fires on the main thread exactly when each signal ends.
 */
class TestSignalPlayer {

    private val activeTracks = HashMap<String, AudioTrack>()
    private val lock = Any()
    private val mainHandler = Handler(Looper.getMainLooper())

    /** Whether any test signal is currently playing. */
    val isPlaying: Boolean
        get() = synchronized(lock) { activeTracks.isNotEmpty() }

    /** Ids of the outputs currently sounding. */
    val activeOutputIds: Set<String>
        get() = synchronized(lock) { activeTracks.keys.toSet() }

    /**
     * Play [type] for [durationMs] honouring the output parameters, routed to
     * [device] when provided. Replaces any signal already playing for this
     * output without touching other outputs. No-op (false) when muted.
     */
    fun play(
        outputId: String,
        type: TestSignalType,
        durationMs: Long = DEFAULT_DURATION_MS,
        volume: Float,
        gainDb: Float,
        delayMs: Float,
        isMuted: Boolean,
        device: AudioDeviceInfo? = null,
        onFinished: () -> Unit = {}
    ): Boolean {
        if (isMuted) return false

        val amplitude = (BASE_AMPLITUDE * volume * 10f.pow(gainDb / 20f)).coerceIn(0f, 0.95f)
        if (amplitude <= 0.002f) return false

        stop(outputId)

        // Delay offset quantized to whole samples at 48 kHz (1 sample ≈ 0.021 ms)
        val delaySamples = ((delayMs.coerceIn(0f, 2000f)) / 1000f * SAMPLE_RATE).roundToInt()
        val signalSamples = (durationMs / 1000f * SAMPLE_RATE).toInt()
        val totalSamples = signalSamples + delaySamples
        val samples = ShortArray(totalSamples)

        when (type) {
            TestSignalType.PINK_NOISE -> generatePinkNoise(samples, delaySamples, amplitude)
            TestSignalType.SWEEP -> generateSweep(samples, delaySamples, amplitude)
            TestSignalType.TONE -> generateTone(samples, delaySamples, amplitude)
        }

        val track = buildTrack(totalSamples) ?: return false
        track.write(samples, 0, totalSamples)
        track.setNotificationMarkerPosition(totalSamples - 1)
        track.setPlaybackPositionUpdateListener(object : AudioTrack.OnPlaybackPositionUpdateListener {
            override fun onMarkerReached(t: AudioTrack) {
                mainHandler.post {
                    stop(outputId)
                    onFinished()
                }
            }

            override fun onPeriodicNotification(t: AudioTrack) {
                // Marker-only listener
            }
        })

        // Route to this output's physical device (speaker / BT / USB / wired)
        if (device != null) {
            try {
                track.preferredDevice = device
            } catch (e: Exception) {
                // Routing unavailable — falls back to the system default
            }
        }

        synchronized(lock) { activeTracks[outputId] = track }
        track.play()
        return true
    }

    /** Stop and release the signal of one output, leaving the others playing. */
    fun stop(outputId: String) {
        val track = synchronized(lock) { activeTracks.remove(outputId) } ?: return
        releaseTrack(track)
    }

    /** Stop and release every playing signal. */
    fun stopAll() {
        val tracks = synchronized(lock) {
            val all = activeTracks.values.toList()
            activeTracks.clear()
            all
        }
        tracks.forEach { releaseTrack(it) }
    }

    private fun releaseTrack(track: AudioTrack) {
        try {
            track.pause()
            track.flush()
            track.release()
        } catch (e: IllegalStateException) {
            // Track already released — nothing to do
        }
    }

    // --- Generators ---

    /**
     * Paul Kellet's pink noise filter: spectrally balanced noise with
     * equal energy per octave, like real room noise and PA systems.
     */
    private fun generatePinkNoise(out: ShortArray, offset: Int, amplitude: Float) {
        var b0 = 0.0
        var b1 = 0.0
        var b2 = 0.0
        var b3 = 0.0
        var b4 = 0.0
        var b5 = 0.0
        var b6 = 0.0
        for (i in offset until out.size) {
            val white = kotlin.random.Random.nextDouble() * 2.0 - 1.0
            b0 = 0.99886 * b0 + white * 0.0555179
            b1 = 0.99332 * b1 + white * 0.0750759
            b2 = 0.96900 * b2 + white * 0.1538520
            b3 = 0.86650 * b3 + white * 0.3104856
            b4 = 0.55000 * b4 + white * 0.5329522
            b5 = -0.7616 * b5 - white * 0.0168980
            val pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * PINK_SCALE
            b6 = white * 0.115926
            out[i] = (pink * amplitude).coerceIn(-1.0, 1.0).toFloat().toInt().toShort()
        }
    }

    /** Logarithmic sweep from 20 Hz to 20 kHz across the signal portion. */
    private fun generateSweep(out: ShortArray, offset: Int, amplitude: Float) {
        val count = out.size - offset
        if (count <= 0) return
        val logMin = ln(20.0)
        val logMax = ln(20000.0)
        var phase = 0.0
        for (i in 0 until count) {
            val progress = i.toDouble() / count
            val freq = (logMin + (logMax - logMin) * progress).pow(1.0)
            phase += 2.0 * Math.PI * freq / SAMPLE_RATE
            out[offset + i] = (sin(phase) * amplitude).coerceIn(-1.0, 1.0).toFloat().toInt().toShort()
        }
    }

    /** Constant 1 kHz sine — the classic "does this line sound?" check. */
    private fun generateTone(out: ShortArray, offset: Int, amplitude: Float) {
        for (i in offset until out.size) {
            val t = (i - offset).toDouble() / SAMPLE_RATE
            out[i] = (sin(2.0 * Math.PI * 1000.0 * t) * amplitude).toInt().toShort()
        }
    }

    private fun buildTrack(totalSamples: Int): AudioTrack? {
        val minBuffer = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        )
        return try {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build()
                )
                .setTransferMode(AudioTrack.MODE_STATIC)
                .setBufferSizeInBytes(maxOf(minBuffer, totalSamples * 2))
                .build()
        } catch (e: Exception) {
            null
        }
    }

    companion object {
        const val SAMPLE_RATE = 48000
        const val DEFAULT_DURATION_MS = 2000L
        private const val BASE_AMPLITUDE = 0.3f
        private const val PINK_SCALE = 0.11
    }
}
