package com.rork.acoustical.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
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

        /** Set by the ViewModel: invoked when the notification's stop action fires. */
        @Volatile
        var onStopRequested: (() -> Unit)? = null
    }

    private var notificationManager: NotificationManager? = null

    /**
     * Puente resultado-motor → notificación. El servicio se suscribe al motor
     * por los listeners adicionales (AudioEngine.addAnalysisListener) y NO
     * por el slot onAnalysisUpdate, que es del ViewModel: pisarlo rompería la
     * UI. [updateNotification] hace el throttle de ~1/s.
     */
    private val analysisToNotification = { r: AudioEngine.AnalysisResult ->
        updateNotification(r.spl, r.correctionIntensity, r.framesAnalyzed)
    }

    /**
     * Último envío de notificación (ms). El motor notifica a la cadencia de
     * análisis (cada 10-100 ms según el intervalo configurado); actualizar la
     * notificación a ese ritmo saturaría al sistema. Una por segundo basta.
     */
    @Volatile private var lastNotificationMs = 0L

    override fun onCreate() {
        super.onCreate()
        notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                engine?.stop()
                // Guardado: si este ACTION_STOP llega a una instancia que nunca
                // hizo startForeground (p. ej. re-entrada por un segundo stop),
                // stopForeground lanzaría IllegalStateException.
                runCatching { stopForeground(STOP_FOREGROUND_REMOVE) }
                stopSelf()
                onStopRequested?.invoke()
                return START_NOT_STICKY
            }
            ACTION_START -> {
                // startForeground ANTES de abrir el micro: en un dispositivo
                // lento (AudioRecord a alta tasa + planificación de la FFT) el
                // engine.start() puede superar la ventana de 5 s que Android
                // concede a startForegroundService y el proceso crashea con
                // ForegroundServiceDidNotStartInTimeException.
                startForegroundCompat(buildNotification(0f, 0f, 0L))

                // Reuse the shared engine created by the ViewModel when present;
                // only fall back to our own instance after a sticky restart.
                if (engine == null) {
                    engine = AudioEngine()
                }
                val started = engine?.start() ?: false
                if (started) {
                    // Alimentar la notificación con los datos reales del
                    // análisis: antes NADIE llamaba a updateNotification y la
                    // notificación quedaba congelada en «Calibrando...».
                    engine?.addAnalysisListener(analysisToNotification)
                } else {
                    // El micro no se pudo abrir: el FGS no tiene razón de ser.
                    // Si era el motor compartido, el error ya llegó a la UI por
                    // el propio callback onStartFailed del motor.
                    stopSelf()
                }
            }
        }
        // NOT_STICKY: si el proceso muere, el servicio no revivie con intent==null
        // (antes dejaba un FGS huérfano con notificación para siempre).
        return START_NOT_STICKY
    }

    fun updateNotification(spl: Float, correction: Float, frames: Long) {
        val now = System.currentTimeMillis()
        if (now - lastNotificationMs < 1000L) return
        lastNotificationMs = now
        val notification = buildNotification(spl, correction, frames)
        notificationManager?.notify(NOTIFICATION_ID, notification)
    }

    /**
     * startForeground con tipo explícito. Con targetSdk 34+ la sobrecarga a 2
     * argumentos lanza MissingForegroundServiceTypeException; hay que declarar el
     * tipo (microphone) desde API 29.
     */
    private fun startForegroundCompat(notification: Notification) {
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(
                NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
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
        // Desuscribirse antes de parar: si el motor es el compartido con el
        // ViewModel puede seguir corriendo, y no queremos que la notificación
        // de un servicio ya muerto siga actualizándose.
        engine?.removeAnalysisListener(analysisToNotification)
        engine?.stop()
        engine = null
        super.onDestroy()
    }
}
