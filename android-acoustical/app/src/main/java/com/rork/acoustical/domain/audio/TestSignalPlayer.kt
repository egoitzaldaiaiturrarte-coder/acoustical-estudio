package com.rork.acoustical.domain.audio

import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Build
import android.os.Handler
import android.os.Looper
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Test signal types available to verify that an output actually sounds.
 *
 * [SILENCE] goes FIRST, like in the Windows app and the VST: it is the
 * unified "generator off" position (choosing it stops the signal; choosing
 * anything else starts it instantly). The same ordering in the three
 * programs is what makes "Silencio" a real switch, not a fourth waveform.
 */
enum class TestSignalType(val label: String, val shortLabel: String) {
    SILENCE("Silencio", "Silencio"),
    PINK_NOISE("Ruido rosa", "Rosa"),
    SWEEP("Barrido 20 Hz - 20 kHz", "Barrido"),
    TONE("Tono 1 kHz", "Tono");
    /**
     * Waveform index in the shared C core (`ACOUSTICAL_WAVE_*`): used to
     * delegate generation to the exact same stateful generator the Windows
     * app runs. SILENCE maps to 0 by construction of the enum.
     */
    val waveformInt: Int
        get() = when (this) {
            SILENCE -> 0
            PINK_NOISE -> 4
            SWEEP -> 3
            TONE -> 1
        }
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
    // Shared C generator handle (created lazily on first native fill; keeps
    // phase/pink/PRNG state between plays, same as the Windows app).
    @Volatile
    private var nativeSiggenHandle: Long = 0L

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
     *
     * [type] = [TestSignalType.SILENCE] is the unified "generator off": it
     * stops any signal for this output (mirroring the Windows app, where
     * "Silencio" in the combo is the only generator switch).
     *
     * [spatialGainDb] / [spatialDelayMs] add the spatial compensation of the
     * element's position (distance gain + propagation delay — the same C law
     * the Windows app and the VST apply to their real output) ON TOP of the
     * output's own [gainDb]/[delayMs], so a far element sounds at the same
     * level and timing as a close one.
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
        spatialGainDb: Float = 0f,
        spatialDelayMs: Float = 0f,
        onFinished: () -> Unit = {}
    ): Boolean {
        if (isMuted) return false
        if (type == TestSignalType.SILENCE) {
            // "Generator off": kill this output's signal, leave the others.
            stop(outputId)
            return false
        }

        val amplitude = (BASE_AMPLITUDE * volume * 10f.pow((gainDb + spatialGainDb) / 20f)).coerceIn(0f, 0.95f)
        if (amplitude <= 0.002f) return false

        stop(outputId)

        // Delay offset quantized to whole samples at 48 kHz (1 sample ≈ 0.021 ms).
        // The spatial propagation delay joins the output's own delay (both are
        // intentional and must not fight the correction loop downstream).
        val totalDelayMs = (delayMs + spatialDelayMs).coerceIn(0f, 2000f)
        val delaySamples = (totalDelayMs / 1000f * SAMPLE_RATE).roundToInt()
        val signalSamples = (durationMs / 1000f * SAMPLE_RATE).toInt()
        val totalSamples = signalSamples + delaySamples
        val samples = ShortArray(totalSamples)

        // On device, delegate to the shared C stateful generator (the exact
        // code the Windows app runs through its SignalGenerator wrapper):
        // identical phase integration, pink filter and PRNG. Without the
        // native lib (JVM unit tests, stripped builds) — or if creating the
        // handle fails — the pure-Kotlin generators below produce the same
        // signals.
        var filled = false
        if (NativeDsp.isAvailable) {
            if (nativeSiggenHandle == 0L) nativeSiggenHandle = NativeDsp.nativeSiggenCreate()
            if (nativeSiggenHandle != 0L) {
                val f = FloatArray(signalSamples)
                NativeDsp.nativeSiggenFill(
                    nativeSiggenHandle,
                    type.waveformInt,
                    1000f,          // TONE frequency (Sine)
                    amplitude,
                    20f,             // SWEEP start
                    20000f,         // SWEEP end
                    5f,             // SWEEP duration
                    SAMPLE_RATE,
                    f
                )
                for (i in 0 until signalSamples) samples[delaySamples + i] = toShort16(f[i].toDouble())
                filled = true
            }
        }
        if (!filled) {
            when (type) {
                TestSignalType.PINK_NOISE -> generatePinkNoise(samples, delaySamples, amplitude)
                TestSignalType.SWEEP -> generateSweep(samples, delaySamples, amplitude)
                TestSignalType.TONE -> generateTone(samples, delaySamples, amplitude)
                TestSignalType.SILENCE -> {} // unreachable: handled above
            }
        }

        val track = buildTrack(totalSamples) ?: return false
        track.write(samples, 0, totalSamples)
        track.setNotificationMarkerPosition(totalSamples - 1)
        track.setPlaybackPositionUpdateListener(object : AudioTrack.OnPlaybackPositionUpdateListener {
            override fun onMarkerReached(t: AudioTrack) {
                mainHandler.post {
                    // Libera solo ESTA pista (por identidad). Si el usuario
                    // relanzó la misma salida antes de que se ejecute este post,
                    // no debe cortarse la pista nueva.
                    val stillMine = synchronized(lock) { activeTracks[outputId] === t }
                    if (stillMine) {
                        synchronized(lock) { if (activeTracks[outputId] === t) activeTracks.remove(outputId) }
                        releaseTrack(t)
                        onFinished()
                    }
                }
            }

            override fun onPeriodicNotification(t: AudioTrack) {
                // Marker-only listener
            }
        })

        // Route to this output's physical device (speaker / BT / USB / wired).
        // setPreferredDevice requiere API 34; con minSdk 24 lanzaría
        // NoSuchMethodError (no es Exception) en el resto de versiones.
        if (device != null && Build.VERSION.SDK_INT >= 34) {
            try {
                track.preferredDevice = device
            } catch (t: Throwable) {
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

    /** Convierte un valor en [-1, 1] a PCM de 16 bits (escala por 32767). */
    private fun toShort16(v: Double): Short =
        (v.coerceIn(-1.0, 1.0).toFloat() * 32767f).roundToInt().toShort()

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
            out[i] = toShort16(pink * amplitude)
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
            out[offset + i] = toShort16(sin(phase) * amplitude)
        }
    }

    /** Constant 1 kHz sine — the classic "does this line sound?" check. */
    private fun generateTone(out: ShortArray, offset: Int, amplitude: Float) {
        for (i in offset until out.size) {
            val t = (i - offset).toDouble() / SAMPLE_RATE
            out[i] = toShort16(sin(2.0 * Math.PI * 1000.0 * t) * amplitude)
        }
    }

    private fun buildTrack(totalSamples: Int): AudioTrack? {
        val minBuffer = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        )
        if (minBuffer <= 0) return null
        val bufferBytes = maxOf(minBuffer, totalSamples * 2)
        // Android 7/8 (API 24-25) no tiene AudioTrack.Builder (aparece en API 26):
        // referenciarlo allí lanza NoSuchMethodError/NoClassDefFoundError, que son
        // Error, NO Exception — un catch(Exception) no los atrapa y el crash mata al
        // hilo principal. Mismo razonamiento que setPreferredDevice más arriba
        // (líneas de la ruta a API 34): puerta por SDK_INT + catch(Throwable).
        return try {
            if (Build.VERSION.SDK_INT >= 26) {
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
                    .setBufferSizeInBytes(bufferBytes)
                    .build()
            } else {
                // Ruta legacy (minSdk 24): constructor clásico con los mismos parámetros.
                AudioTrack(
                    AudioManager.STREAM_MUSIC,
                    SAMPLE_RATE,
                    AudioFormat.CHANNEL_OUT_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    bufferBytes,
                    AudioTrack.MODE_STATIC
                )
            }
        } catch (t: Throwable) {
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
