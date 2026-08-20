package com.rork.acoustical.domain.audio

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Walkie-talkie voice link for the audio workflow.
 *
 * Captures microphone audio in 20 ms chunks while the operator holds the
 * talk button, hands each chunk to [onAudioChunk] (for transmission over the
 * mesh network), and plays back chunks received from peers.
 */
class WalkieTalkieManager(private val context: Context) {

    companion object {
        private const val SAMPLE_RATE = 16_000
        private const val CHUNK_SAMPLES = 320 // 20 ms @ 16 kHz
    }

    /** Called on a background thread with each captured chunk while talking. */
    var onAudioChunk: ((ShortArray) -> Unit)? = null

    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var scope: CoroutineScope? = null
    private var captureJob: kotlinx.coroutines.Job? = null

    @Volatile
    var isActive: Boolean = false
        private set

    fun hasMicPermission(): Boolean = ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.RECORD_AUDIO
    ) == PackageManager.PERMISSION_GRANTED

    /**
     * Start capturing and playing. Requires RECORD_AUDIO permission.
     * @return true if the link started.
     */
    @SuppressLint("MissingPermission")
    fun start(): Boolean {
        if (isActive) return true
        if (!hasMicPermission()) return false

        val minBufIn = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val minBufOut = AudioTrack.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        if (minBufIn <= 0 || minBufOut <= 0) return false

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                minBufIn * 2
            )
            audioTrack = AudioTrack(
                AudioManager.STREAM_VOICE_CALL,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                minBufOut * 2,
                AudioTrack.PERFORMANCE_MODE_LOW_LATENCY
            )
        } catch (e: Exception) {
            stop()
            return false
        }

        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED ||
            audioTrack?.state != AudioTrack.STATE_INITIALIZED
        ) {
            stop()
            return false
        }

        isActive = true
        audioTrack?.play()
        audioRecord?.startRecording()
        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        captureJob = scope?.launch {
            val buffer = ShortArray(CHUNK_SAMPLES)
            while (isActive && kotlinx.coroutines.currentCoroutineContext().isActive) {
                val read = audioRecord?.read(buffer, 0, CHUNK_SAMPLES) ?: -1
                if (read > 0) {
                    onAudioChunk?.invoke(buffer.copyOf(read))
                }
            }
        }
        return true
    }

    /** Play a chunk received from a peer. */
    fun playChunk(chunk: ShortArray) {
        try {
            audioTrack?.write(chunk, 0, chunk.size)
        } catch (e: IllegalStateException) {
            // Track already released — ignore
        }
    }

    /** Stop the link and release audio resources. */
    fun stop() {
        isActive = false
        captureJob?.cancel()
        captureJob = null
        scope?.cancel()
        scope = null
        try {
            audioRecord?.stop()
        } catch (e: Exception) {
            // Ignore
        }
        try {
            audioTrack?.stop()
        } catch (e: Exception) {
            // Ignore
        }
        audioRecord?.release()
        audioRecord = null
        audioTrack?.release()
        audioTrack = null
    }
}
