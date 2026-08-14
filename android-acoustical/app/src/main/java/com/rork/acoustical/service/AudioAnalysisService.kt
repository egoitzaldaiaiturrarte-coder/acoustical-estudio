package com.rork.acoustical.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.rork.acoustical.MainActivity
import com.rork.acoustical.R
import com.rork.acoustical.domain.audio.AudioEngine

/**
 * Foreground service that keeps the audio analysis engine running in the background.
 *
 * This allows continuous spectrum analysis and room correction even when the app
 * is not in the foreground. The notification shows current SPL and correction status.
 */
class AudioAnalysisService : Service() {

    companion object {
        const val CHANNEL_ID = "acoustical_analysis"
        const val NOTIFICATION_ID = 1001

        const val ACTION_START = "com.rork.acoustical.START"
        const val ACTION_STOP = "com.rork.acoustical.STOP"
        const val ACTION_UPDATE_CONFIG = "com.rork.acoustical.UPDATE_CONFIG"

        const val EXTRA_SPL = "extra_spl"
        const val EXTRA_CORRECTION = "extra_correction"
        const val EXTRA_FRAMES = "extra_frames"

        @Volatile
        var engine: AudioEngine? = null
            private set
    }

    private var notificationManager: NotificationManager? = null

    override fun onCreate() {
        super.onCreate()
        notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                engine?.stop()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_START -> {
                if (engine == null) {
                    engine = AudioEngine()
                }
                engine?.start()
                startForeground(NOTIFICATION_ID, buildNotification(0f, 0f, 0L))
            }
        }
        return START_STICKY
    }

    fun updateNotification(spl: Float, correction: Float, frames: Long) {
        val notification = buildNotification(spl, correction, frames)
        notificationManager?.notify(NOTIFICATION_ID, notification)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Audio Analysis",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Continuous audio spectrum analysis and room correction"
                setShowBadge(false)
            }
            notificationManager?.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(spl: Float, correction: Float, frames: Long): Notification {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val stopIntent = Intent(this, AudioAnalysisService::class.java).apply {
            action = ACTION_STOP
        }
        val stopPendingIntent = PendingIntent.getService(
            this, 1, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val splText = if (spl > 0) "%.1f dB SPL".format(spl) else "Calibrando..."
        val correctionText = if (correction > 0.01f) {
            "Corrección: %+.1f%%".format(correction * 100)
        } else {
            "Sin corrección activa"
        }

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("AcoustiCal — Análisis activo")
            .setContentText("$splText · $correctionText")
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
        engine?.stop()
        engine = null
        super.onDestroy()
    }
}
