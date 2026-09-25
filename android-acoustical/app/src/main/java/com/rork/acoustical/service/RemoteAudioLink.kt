package com.rork.acoustical.service

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import kotlin.math.floor

/**
 * Puente de audio Wi-Fi con el PC (M2/M3). Contraparte de
 * RemoteAudioLink (C++) del lado Windows; ambos hablan UDP,
 * independiente del canal de control TCP 41041.
 *
 *  Puerto 41043 (este móvil escucha): tramas de audio del PC —se
 *      reproducen por el altavoz del móvil, M3— y control JSON
 *      (se reconoce porque el primer byte es '{'):
 *        {"cmd":"mic_start","pcIp":"a.b.c.d","port":41044,"rate":48000,"code":"…"}
 *        {"cmd":"mic_stop"}
 *      El móvil NO abre su micrófono sin el código de emparejamiento
 *      correcto (el mismo de 6 dígitos de Ajustes > PC/Windows).
 *
 *  Puerto 41044 (escucha el PC): tramas del micro del móvil (M2),
 *      10 ms (480 muestras, 48 kHz, mono), desde que el PC pide.
 *
 *  Trama de audio (ambos sentidos): cabecera de 8 bytes BIG-ENDIAN
 *  (u16 seq, u16 rate, u16 ch, u16 reservado) + PCM16 LITTLE-ENDIAN
 *  intercalado.
 *
 *  El PC manda a su propia tasa (44,1k o 48k); el móvil resamplea a
 *  48k con interpolación lineal (continuidad de fase entre tramas)
 *  para el AudioTrack. El micro del móvil siempre captura a 48 kHz.
 *
 *  Limitación aceptada (v1.6.0): la captura en segundo plano exige
 *  Android 14+ tener la app abierta o el servicio de análisis activo
 *  (FGS de micro); si no se puede abrir el micro, se deja un estado
 *  explicativo en micError y el resto sigue funcionando.
 */
class RemoteAudioLink(context: Context, private val codeProvider: () -> String) {

    // === Estado para la UI (Ajustes > PC/Windows) ===

    private val _micStreaming = MutableStateFlow(false)
    /** El micro de este móvil está activo y mandando audio al PC. */
    val micStreaming: StateFlow<Boolean> = _micStreaming.asStateFlow()

    private val _pcPlaying = MutableStateFlow(false)
    /** El PC está reproduciendo audio por el altavoz de este móvil. */
    val pcPlaying: StateFlow<Boolean> = _pcPlaying.asStateFlow()

    private val _micError = MutableStateFlow<String?>(null)
    /** Error del micro (permiso, app en segundo plano…), o null. */
    val micError: StateFlow<String?> = _micError.asStateFlow()

    @Volatile private var running = false
    @Volatile private var listenerSock: DatagramSocket? = null
    private var listenerThread: Thread? = null
    private var playbackThread: Thread? = null

    // === Reproducción (PC → altavoz del móvil, M3) ===

    private val jitter = JitterBuffer()
    @Volatile private var lastFrameMs = 0L
    @Volatile private var track: AudioTrack? = null

    // === Micro (móvil → PC, M2) ===

    @Volatile private var micTarget: Pair<String, Int>? = null
    @Volatile private var micRunning = false
    private var micThread: Thread? = null

    fun start() {
        if (running) return
        running = true
        listenerThread = Thread({ listenLoop() }, "acoustical-remote-audio").apply {
            isDaemon = true
            start()
        }
        playbackThread = Thread({ playbackLoop() }, "acoustical-remote-playback").apply {
            isDaemon = true
            start()
        }
    }

    fun stop() {
        if (!running) return
        running = false
        runCatching { listenerSock?.close() }   // desbloquea el receive
        stopMic()
        runCatching { listenerThread?.join(400) }
        runCatching { playbackThread?.join(400) }
        listenerThread = null
        playbackThread = null
    }

    // === Oyente 41043: control JSON + tramas de audio del PC ===

    private fun listenLoop() {
        val sock = try {
            DatagramSocket(LISTEN_PORT).also {
                it.soTimeout = 250
                listenerSock = it
            }
        } catch (e: Exception) {
            Log.w(TAG, "no se pudo abrir el puerto $LISTEN_PORT (¿ocupado?)", e)
            return
        }
        val buf = ByteArray(MAX_FRAME_BYTES)
        while (running) {
            val packet = DatagramPacket(buf, buf.size)
            try {
                sock.receive(packet)
            } catch (e: SocketTimeoutException) {
                continue
            } catch (e: Exception) {
                break   // el socket se cerró
            }
            val n = packet.length
            if (n < 9) continue
            if (buf[0].toInt() == '{'.code) {
                handleControl(String(buf, 0, n, Charsets.UTF_8))
                continue
            }
            handleFrame(buf, n)
        }
    }

    private fun handleControl(text: String) {
        val obj = runCatching {
            Json.parseToJsonElement(text).jsonObject
        }.getOrNull() ?: return
        when ((obj["cmd"] as? JsonPrimitive)?.content) {
            "mic_start" -> {
                val code = (obj["code"] as? JsonPrimitive)?.content
                if (code == null || code != codeProvider()) {
                    Log.i(TAG, "mic_start ignorado: código no válido")
                    return
                }
                val ip = (obj["pcIp"] as? JsonPrimitive)?.content.orEmpty()
                val port = (obj["port"] as? JsonPrimitive)?.content?.let {
                    it.toIntOrNull() ?: it.toDoubleOrNull()?.toInt()
                } ?: MIC_PORT_PC
                if (ip.isEmpty()) return
                startMic(ip, port)
            }
            "mic_stop" -> stopMic()
        }
    }

    private fun handleFrame(b: ByteArray, n: Int) {
        val rate = ((b[2].toInt() and 0xFF) shl 8) or (b[3].toInt() and 0xFF)
        val ch = ((b[4].toInt() and 0xFF) shl 8) or (b[5].toInt() and 0xFF)
        if (rate !in 8000..96000 || (ch != 1 && ch != 2)) return
        val samples = (n - 8) / (2 * ch)
        if (samples <= 0 || samples > 4096) return
        val pcm = ShortArray(samples * 2)
        for (i in 0 until samples) {
            if (ch == 2) {
                pcm[i * 2] = le16(b, 8 + i * 4)
                pcm[i * 2 + 1] = le16(b, 10 + i * 4)
            } else {
                val v = le16(b, 8 + i * 2)
                pcm[i * 2] = v
                pcm[i * 2 + 1] = v
            }
        }
        lastFrameMs = System.currentTimeMillis()
        jitter.write(pcm, rate)
    }

    private fun le16(b: ByteArray, off: Int): Short =
        (((b[off + 1].toInt() and 0xFF) shl 8) or (b[off].toInt() and 0xFF)).toShort()

    // === Reproducción: buffer de jitter + resampleo a 48k + AudioTrack ===

    private fun playbackLoop() {
        val outBuf = ShortArray(CHUNK * 2)
        while (running) {
            val produced = jitter.read(outBuf, CHUNK)
            if (produced > 0) {
                val t = track ?: createTrack()
                if (t != null) {
                    try {
                        t.write(outBuf, 0, produced * 2)
                    } catch (e: Exception) {
                        releaseTrack()   // el siguiente ciclo la crea de nuevo
                    }
                }
            } else {
                runCatching { Thread.sleep(5) }
            }
            val playing = recentAudio()
            if (playing != _pcPlaying.value) _pcPlaying.value = playing
        }
        releaseTrack()
    }

    private fun recentAudio() = System.currentTimeMillis() - lastFrameMs < 1500

    private fun createTrack(): AudioTrack? = try {
        val fmt = AudioFormat.Builder()
            .setSampleRate(TARGET_RATE)
            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .build()
        val min = AudioTrack.getMinBufferSize(
            TARGET_RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val bufSize = maxOf(min, CHUNK * 4)
        val t = AudioTrack(
            AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build(),
            fmt, bufSize, AudioTrack.MODE_STREAM, 0
        )
        t.play()
        track = t
        t
    } catch (e: Exception) {
        Log.w(TAG, "no se pudo crear el AudioTrack", e)
        null
    }

    private fun releaseTrack() {
        runCatching { track?.stop() }
        runCatching { track?.release() }
        track = null
    }

    // === Micro: captura 48k mono → tramas de 10 ms al PC ===

    private fun startMic(pcIp: String, port: Int) {
        micTarget = pcIp to port
        if (micRunning) return   // ya captura: solo se actualiza el destino
        micRunning = true
        _micError.value = null
        micThread = Thread({ micLoop() }, "acoustical-remote-mic").apply {
            isDaemon = true
            start()
        }
    }

    private fun stopMic() {
        micRunning = false
        runCatching { micThread?.join(400) }
        micThread = null
        _micStreaming.value = false
    }

    private fun micLoop() {
        var error: String? = null
        val minBuf = AudioRecord.getMinBufferSize(
            MIC_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val rec = try {
            AudioRecord(
                MediaRecorder.AudioSource.MIC,
                MIC_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                maxOf(minBuf, MIC_RATE / 5) * 2
            )
        } catch (e: Exception) {
            null
        }
        if (rec == null || rec.state != AudioRecord.STATE_INITIALIZED) {
            rec?.release()
            micRunning = false
            error = "No se pudo abrir el micrófono: deja la app abierta (o el análisis en " +
                "segundo plano) para que el PC lo use"
            _micError.value = error
            return
        }
        val sock = runCatching { DatagramSocket() }.getOrNull()
            ?: run {
                rec.release()
                micRunning = false
                error = "Sin socket UDP para enviar el micrófono"
                _micError.value = error
                return
            }
        val scratch = ShortArray(MIC_RATE / 5)   // 20 ms por lectura
        val acc = ShortArray(MIC_RATE)           // 1 s de acumulación
        var accSize = 0
        var seq = 0
        var lastIp: String? = null
        var addr: InetAddress? = null
        val frame = ByteArray(8 + FRAMES_SAMPLES * 2)
        runCatching { rec.startRecording() }
        _micStreaming.value = true
        while (micRunning) {
            val target = micTarget ?: break
            val got = try {
                rec.read(scratch, 0, scratch.size)
            } catch (e: Exception) {
                break
            }
            if (got <= 0) {
                // lectura fatal (permiso retirado, servicio de micro soltado…)
                error = "El micrófono se ha cerrado: abre la app o el análisis para " +
                    "reanudar el envío al PC"
                break
            }
            if (accSize + got > acc.size) accSize = 0
            System.arraycopy(scratch, 0, acc, accSize, got)
            accSize += got
            while (accSize >= FRAMES_SAMPLES) {
                // Cabecera big-endian: seq, rate, ch=1, reservado
                frame[0] = ((seq ushr 8) and 0xFF).toByte()
                frame[1] = (seq and 0xFF).toByte()
                frame[2] = ((MIC_RATE ushr 8) and 0xFF).toByte()
                frame[3] = (MIC_RATE and 0xFF).toByte()
                frame[4] = 0
                frame[5] = 1
                frame[6] = 0
                frame[7] = 0
                for (i in 0 until FRAMES_SAMPLES) {
                    val s = acc[i].toInt()
                    frame[8 + i * 2] = (s and 0xFF).toByte()
                    frame[9 + i * 2] = ((s ushr 8) and 0xFF).toByte()
                }
                System.arraycopy(acc, FRAMES_SAMPLES, acc, 0, accSize - FRAMES_SAMPLES)
                accSize -= FRAMES_SAMPLES
                seq = (seq + 1) and 0xFFFF
                if (lastIp != target.first || addr == null) {
                    val a = runCatching { InetAddress.getByName(target.first) }.getOrNull()
                        ?: continue
                    addr = a
                    lastIp = target.first
                }
                try {
                    sock.send(DatagramPacket(frame, frame.size, addr, target.second))
                } catch (e: Exception) {
                    // UDP sin estado: si el PC no contesta, seguimos
                }
            }
        }
        runCatching { rec.stop() }
        rec.release()
        runCatching { sock.close() }
        micRunning = false
        _micStreaming.value = false
        if (error != null) _micError.value = error
    }

    // === Buffer de jitter + resampleo lineal ===

    /**
     * Guarda las tramas recibidas (estéreo intercalado) y las lee
     * resampleadas a 48k con interpolación lineal.
     *
     * La posición de lectura es fraccional y se conserva entre tramas,
     * lo que mantiene la fase continua aunque el audio llegue en trozos
     * de 10 ms. Si el buffer se llena se descarta el audio más antiguo
     * (auto-limitado, como el del Hub).
     */
    private class JitterBuffer {
        private val cap = MAX_FRAMES * FRAMES_SAMPLES   // muestras por canal
        private val data = ShortArray(cap * 2)
        private var filled = 0       // muestras válidas
        private var readPos = 0.0    // posición de lectura fraccional
        @Volatile var rate = TARGET_RATE   // tasa de la última trama
            private set

        fun write(frame: ShortArray, rateHz: Int) {
            rate = rateHz
            val n = frame.size / 2
            if (filled + n > cap) {
                val drop = filled + n - cap
                val keep = filled - drop
                System.arraycopy(data, keep * 2, data, 0, keep * 2)
                filled = keep
                readPos = (readPos - drop).coerceAtLeast(0.0)
            }
            System.arraycopy(frame, 0, data, filled * 2, n * 2)
            filled += n
        }

        /** Resamplea hasta [outSamples] muestras de salida a 48k; devuelve las producidas. */
        fun read(out: ShortArray, outSamples: Int): Int {
            val ratio = rate.toDouble() / TARGET_RATE
            var produced = 0
            for (i in 0 until outSamples) {
                if (readPos + 1.0 >= filled) break
                val i0 = floor(readPos).toInt()
                val frac = (readPos - i0).toFloat()
                out[i * 2] = lerp(data[i0 * 2], data[(i0 + 1) * 2], frac)
                out[i * 2 + 1] = lerp(data[i0 * 2 + 1], data[(i0 + 1) * 2 + 1], frac)
                readPos += ratio
                produced++
            }
            return produced
        }

        private fun lerp(a: Short, b: Short, frac: Float): Short =
            (a.toInt() + (b.toInt() - a.toInt()) * frac).toInt()
                .coerceIn(-32768, 32767).toShort()
    }

    companion object {
        const val LISTEN_PORT = 41043     // audio + control que manda el PC
        const val MIC_PORT_PC = 41044     // puerto donde el PC escucha el micro
        const val MIC_RATE = 48000        // el móvil siempre captura a 48k
        const val FRAMES_SAMPLES = 480    // 10 ms a 48k

        private const val TARGET_RATE = 48000
        private const val CHUNK = 480          // muestras de salida por escritura
        private const val MAX_FRAMES = 40      // ~400 ms de buffer
        private const val MAX_FRAME_BYTES = 8 + 4096 * 4   // tope de recepción (estéreo)
        private const val TAG = "RemoteAudioLink"
    }
}
