package com.rork.acoustical.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.rork.acoustical.MainActivity
import com.rork.acoustical.R
import com.rork.acoustical.domain.audio.BandAggregator
import com.rork.acoustical.domain.audio.FftProcessor
import com.rork.acoustical.domain.model.StandardFrequencies

/**
 * Captures the internal audio of other apps (Spotify, YouTube…) digitally via
 * MediaProjection + AudioPlaybackCapture, aggregates it into the same 124
 * analysis bands the engine uses, and publishes the levels so the engine can
 * mix them with the microphone (all active inputs at once).
 *
 * Requires the user's projection consent (requested from Ruteos) and only runs
 * on Android 10+ (API 29), where playback capture exists.
 */
class InternalCaptureService : Service() {

    companion object {
        private const val TAG = "InternalCapture"
        private const val CHANNEL_ID = "acoustical_capture"
        private const val NOTIFICATION_ID = 1002

        const val ACTION_START = "com.rork.acoustical.CAPTURE_START"
        const val ACTION_STOP = "com.rork.acoustical.CAPTURE_STOP"
        const val EXTRA_RESULT_CODE = "extra_result_code"
        const val EXTRA_DATA = "extra_data"

        private const val CAPTURE_SAMPLE_RATE = 48000
        private const val CAPTURE_FFT_SIZE = 2048

        /** Latest band levels (124 bands, dB) from captured app audio; null when off. */
        @Volatile
        var latestLevels: FloatArray? = null
            private set

        @Volatile
        var isCapturing: Boolean = false
            private set
    }

    private var projection: MediaProjection? = null
    private var captureThread: Thread? = null
    @Volatile private var captureRecord: AudioRecord? = null
    // Serializa releaseCapture: el callback de revocación de MediaProjection,
    // ACTION_STOP y onDestroy pueden llamarlo a la vez; dos releases en
    // paralelo doble-liberarían el AudioRecord y el plan FFT.
    private val releaseLock = Any()
    @Volatile private var releaseInFlight = false

    @Volatile
    private var shouldCapture: Boolean = false

    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Captura de audio interno",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Captura digital del audio de otras apps para el análisis"
                setShowBadge(false)
            }
            manager.createNotificationChannel(channel)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                releaseCapture()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_START -> {
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                    Log.w(TAG, "AudioPlaybackCapture requiere Android 10+")
                    stopSelf()
                    return START_NOT_STICKY
                }
                val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, Int.MIN_VALUE)
                val data = intent.getParcelableExtra<android.content.Intent>(EXTRA_DATA)
                if (resultCode == Int.MIN_VALUE || data == null) {
                    stopSelf()
                    return START_NOT_STICKY
                }
                val notification = buildNotification()
                if (Build.VERSION.SDK_INT >= 29) {
                    // targetSdk 34+ exige el tipo; sin él, MissingForegroundServiceTypeException
                    startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
                } else {
                    startForeground(NOTIFICATION_ID, notification)
                }
                startCapture(resultCode, data)
            }
        }
        return START_NOT_STICKY
    }

    private fun startCapture(resultCode: Int, data: android.content.Intent) {
        if (isCapturing) return
        val projectionManager =
            getSystemService(Context.MEDIA_PROJECTION_SERVICE) as? MediaProjectionManager ?: return

        val mediaProjection = try {
            projectionManager.getMediaProjection(resultCode, data)
        } catch (e: Exception) {
            Log.e(TAG, "MediaProjection rechazada", e)
            stopSelf()
            return
        } ?: return

        projection = mediaProjection
        mediaProjection.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() {
                // El usuario revocó la captura desde el diálogo de sistema:
                // pasar por releaseCapture() libera el AudioRecord y el plan
                // FFT del hilo de captura. Antes (stopForeground + stopSelf
                // directos) el hilo se fugaba con los recursos y
                // isCapturing/latestLevels quedaban huérfanos, con el motor
                // mezclando niveles de una captura ya muerta.
                releaseCapture()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }, null)

        val playbackConfig = AudioPlaybackCaptureConfiguration.Builder(mediaProjection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
            .build()

        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(CAPTURE_SAMPLE_RATE)
            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
            .build()

        val minBuffer = AudioRecord.getMinBufferSize(
            CAPTURE_SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val record = try {
            AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(maxOf(minBuffer, CAPTURE_FFT_SIZE * 2))
                .setAudioPlaybackCaptureConfig(playbackConfig)
                .build()
        } catch (e: Exception) {
            Log.e(TAG, "No se pudo abrir la captura de audio", e)
            stopSelf()
            return
        }

        if (record.state != AudioRecord.STATE_INITIALIZED) {
            Log.e(TAG, "AudioRecord de captura no inicializado")
            runCatching { record.release() }
            stopSelf()
            return
        }

        shouldCapture = true
        isCapturing = true
        latestLevels = FloatArray(StandardFrequencies.ultra124.size)
        // Referencia a nivel de clase para que releaseCapture() pueda hacer
        // stop()/release() del record aunque el hilo siga bloqueado en read().
        captureRecord = record

        captureThread = Thread {
            record.startRecording()
            val shortBuffer = ShortArray(CAPTURE_FFT_SIZE)
            val floatBuffer = FloatArray(CAPTURE_FFT_SIZE)
            val fft = FftProcessor(CAPTURE_FFT_SIZE)
            var lastPublishMs = 0L

            while (shouldCapture) {
                val read = record.read(shortBuffer, 0, CAPTURE_FFT_SIZE)
                when {
                    read < 0 -> {
                        // Valor negativo = error real de read()
                        // (ERROR_BAD_STATE/ERROR_INVALID_OPERATION/…): salir
                        // del bucle en vez de hacer spin a 100% de CPU.
                        // "Aún no hay datos" es read == 0 (caso de abajo).
                        Log.e(TAG, "AudioRecord.read devolvió error: $read (estado=${record.state})")
                        break
                    }
                    read == 0 -> {
                        // 50 ms (antes 2): «aún no hay datos» es frecuente con
                        // la entrada suspendida en segundo plano; a 2 ms el
                        // bucle spinnearía con cientos de lecturas vacías/segundo.
                        try { Thread.sleep(50) } catch (_: InterruptedException) {}
                        continue
                    }
                }
                for (i in 0 until read) {
                    floatBuffer[i] = shortBuffer[i] / 32768.0f
                }
                for (i in read until CAPTURE_FFT_SIZE) {
                    floatBuffer[i] = 0f
                }

                val now = System.currentTimeMillis()
                if (now - lastPublishMs >= 50) {
                    lastPublishMs = now
                    val magnitudes = fft.computeMagnitudesDb(floatBuffer, CAPTURE_SAMPLE_RATE)
                    val binFreqs = fft.getBinFrequencies(CAPTURE_SAMPLE_RATE)
                    latestLevels = aggregateBands(magnitudes, binFreqs)
                }
            }
            try {
                record.stop()
            } catch (e: Exception) {
                Log.w(TAG, "Error deteniendo captura", e)
            }
            try {
                record.release()
            } catch (e: Exception) {
                Log.w(TAG, "Error liberando el AudioRecord de captura", e)
            }
            fft.close()  // libera el plan FFT nativo (ver FftProcessor.close)
            // El hilo limpia su propio estado al terminar, SOLO si sigue
            // siendo el registrado: así un «zombi» (cuyo join expiró en
            // releaseCapture) no borra el estado de una captura nueva que
            // se hubiera montado en el entretanto.
            if (captureThread === Thread.currentThread()) {
                captureThread = null
                captureRecord = null
                latestLevels = null
                isCapturing = false
            }
        }.also { it.start() }
    }

    /**
     * Agregación en el único punto canónico ([BandAggregator]). Antes había
     * aquí una copia propia con ratio FIJO 2^(1/12) y sin gating, por lo que
     * sus niveles no eran comparables con los del micrófono. Elige floor -120
     * (su antiguo valor de banda vacía): en la práctica no gatea nada y una
     * banda sin bins sigue valiendo -120 como antes.
     */
    private fun aggregateBands(magnitudesDb: FloatArray, binFreqs: FloatArray): FloatArray {
        return BandAggregator.aggregate(
            StandardFrequencies.ultra124, magnitudesDb, binFreqs, -120f
        )
    }

    private fun releaseCapture() {
        // Idempotente y serializado: revocación del sistema, ACTION_STOP y
        // onDestroy pueden llegar en cualquier orden y a la vez.
        synchronized(releaseLock) {
            if (releaseInFlight) return
            releaseInFlight = true
        }
        shouldCapture = false
        // Primero detener la proyección (es lo que desbloquea el record.read en
        // el hilo de captura) y DESPUÉS esperar al hilo. Antes el join iba antes
        // y expiraba porque el hilo seguía bloqueado en read.
        try {
            projection?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "Error deteniendo proyección", e)
        }
        projection = null
        // stop() del AudioRecord DESDE AQUÍ, antes del join: desbloquea un
        // read() en curso en el hilo de captura. Sin esto, si el hilo está
        // bloqueado en read (entrada suspendida en segundo plano), el join
        // expira y el hilo — con su AudioRecord y su plan FFT — se fuga.
        val record = captureRecord
        runCatching { record?.stop() }
        try {
            captureThread?.join(500)
        } catch (_: InterruptedException) {
        }
        if (record != null && captureThread?.isAlive == true) {
            // El join expiró y el hilo sigue vivo: forzar el release lo
            // desbloquea; el read lanza, el catch del bucle lo saca y el
            // hilo termina solo (limpiando su propio estado al morir).
            Log.w(TAG, "Hilo de captura aún vivo tras el join; forzando release")
            runCatching { record.release() }
        }
        // Re-evaluar el estado según el hilo siga vivo: si ya terminó (o
        // terminó en el intento) aseguramos limpieza — por si su limpieza
        // propia no llegó a ejecutarse (excepción en el bucle) —; si sigue
        // vivo, la captura sigue activa y el propio hilo limpiará al morir.
        if (captureThread?.isAlive != true) {
            captureThread = null
            captureRecord = null
            latestLevels = null
            isCapturing = false
        }
        synchronized(releaseLock) {
            releaseInFlight = false
        }
    }

    private fun buildNotification(): Notification {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stopIntent = Intent(this, InternalCaptureService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPendingIntent = PendingIntent.getService(
            this, 2, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("AcoustiCal — Captura de audio interno")
            .setContentText("Capturando el audio de otras apps para el análisis")
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentIntent(pendingIntent)
            .addAction(0, "Detener", stopPendingIntent)
            .setOngoing(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        releaseCapture()
        super.onDestroy()
    }
}
