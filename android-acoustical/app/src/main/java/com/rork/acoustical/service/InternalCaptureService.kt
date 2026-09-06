package com.rork.acoustical.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
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
import com.rork.acoustical.domain.audio.FftProcessor
import com.rork.acoustical.domain.model.StandardFrequencies
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.pow

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
                startForeground(NOTIFICATION_ID, buildNotification())
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
                // The user revoked the capture from the system cast dialog
                shouldCapture = false
                latestLevels = null
                isCapturing = false
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
            record.release()
            stopSelf()
            return
        }

        shouldCapture = true
        isCapturing = true
        latestLevels = FloatArray(StandardFrequencies.ultra124.size)

        captureThread = Thread {
            record.startRecording()
            val shortBuffer = ShortArray(CAPTURE_FFT_SIZE)
            val floatBuffer = FloatArray(CAPTURE_FFT_SIZE)
            val fft = FftProcessor(CAPTURE_FFT_SIZE)
            var lastPublishMs = 0L

            while (shouldCapture) {
                val read = record.read(shortBuffer, 0, CAPTURE_FFT_SIZE)
                if (read <= 0) continue
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
            record.release()
        }.also { it.start() }
    }

    /** Same 1/12-octave log aggregation the engine's corrector uses. */
    private fun aggregateBands(magnitudesDb: FloatArray, binFreqs: FloatArray): FloatArray {
        val bands = StandardFrequencies.ultra124
        val levels = FloatArray(bands.size)
        val edgeRatio = 2.0.pow(1.0 / 12.0)
        for (b in bands.indices) {
            val center = bands[b]
            val lower = center / edgeRatio
            val upper = center * edgeRatio
            var sum = 0.0
            var count = 0
            for (i in binFreqs.indices) {
                val freq = binFreqs[i]
                if (freq in lower..upper) {
                    sum += magnitudesDb[i]
                    count++
                }
                if (freq > upper) break
            }
            levels[b] = if (count > 0) (sum / count).toFloat() else -120f
        }
        return levels
    }

    private fun releaseCapture() {
        shouldCapture = false
        try {
            captureThread?.join(500)
        } catch (_: InterruptedException) {
        }
        captureThread = null
        latestLevels = null
        isCapturing = false
        try {
            projection?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "Error deteniendo proyección", e)
        }
        projection = null
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
