package com.rork.acoustical.domain.audio

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import com.rork.acoustical.domain.model.AudioConfig
import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.domain.model.SpectrumFrame
import com.rork.acoustical.domain.model.StandardFrequencies
import com.rork.acoustical.domain.model.SupportBand
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Core audio analysis engine.
 *
 * Captures audio from the device microphone using AudioRecord, runs FFT analysis
 * at the configured interval, compares the measured spectrum against a reference,
 * and computes dynamic EQ corrections. Designed to run in a foreground service
 * for continuous background operation.
 *
 * The engine does NOT modify system audio output (Android does not allow apps to
 * insert global audio effects without root). Instead, it provides real-time
 * analysis and correction recommendations that can be applied to the app's own
 * audio playback path or used as a measurement/reference tool.
 */
class AudioEngine {

    companion object {
        private const val TAG = "AudioEngine"
        private const val CHANNEL_IN = AudioFormat.CHANNEL_IN_MONO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
        private const val DYNAMIC_EQ_COUNT = 3
    }

    private var scope: CoroutineScope? = null
    private var analysisJob: Job? = null
    private var audioRecord: AudioRecord? = null

    private var fftProcessor: FftProcessor? = null
    private var roomCorrector: RoomCorrector? = null
    private var splMeter: SplMeter? = null
    private var noiseProfiler: NoiseProfiler? = null
    private val sweepers = arrayOfNulls<SweeperProcessor>(DYNAMIC_EQ_COUNT)
    private val dynamicCfgs = arrayOf(
        DynamicEqConfig(startFrom = SweepDirection.NEED_BASED),
        DynamicEqConfig(startFrom = SweepDirection.BOTTOM_UP),
        DynamicEqConfig(startFrom = SweepDirection.TOP_DOWN)
    )
    private val sweeperEnabled = BooleanArray(DYNAMIC_EQ_COUNT)
    private val supportBandsByEq = Array(DYNAMIC_EQ_COUNT) { emptyList<SupportBand>() }
    private var sweeperJob: Job? = null

    private var config: AudioConfig = AudioConfig.Default
    private var bandFrequencies: FloatArray = StandardFrequencies.tenBand
    private var bands: MutableList<EqBand> = mutableListOf()

    // Reference spectrum (the "ideal" or source signature)
    private var referenceLevels: FloatArray? = null
    private var isReferenceCaptured: Boolean = false

    // Callback for UI updates
    var onAnalysisUpdate: ((AnalysisResult) -> Unit)? = null
    var onNoiseCaptureProgress: ((Float) -> Unit)? = null
    var onNoiseCaptureComplete: (() -> Unit)? = null
    /** Invoked when the engine cannot start (permission denied, mic unavailable). */
    var onStartFailed: ((String) -> Unit)? = null
    /** Invoked on every processor decision (decide every 800 ms, values adjust every 10 ms). */
    var onSweepUpdate: ((SweepProcess, SweeperProcessor.SweepStep) -> Unit)? = null

    // Shared state between the analysis loop and the sweeper loop
    @Volatile private var lastMeasuredLevels: FloatArray? = null
    @Volatile private var appCaptureLevels: FloatArray? = null

    private var framesAnalyzed: Long = 0L
    private var isRunning: Boolean = false

    /**
     * Result of a single analysis cycle, sent to the UI.
     */
    data class AnalysisResult(
        val measuredSpectrum: SpectrumFrame,
        val correctedSpectrum: SpectrumFrame?,
        val bands: List<EqBand>,
        val spl: Float,
        val peakSpl: Float,
        val averageSpl: Float,
        val correctionIntensity: Float,
        val cpuLoadPercent: Float,
        val framesAnalyzed: Long,
        val noiseSpectrum: SpectrumFrame? = null,
        /** Per dynamic-EQ gain curves (auto corrections + its own support bands). */
        val dynamicEqGainsL: List<FloatArray> = emptyList(),
        val dynamicEqGainsR: List<FloatArray> = emptyList()
    )

    /**
     * Configure the engine with new parameters. Takes effect on next start or analysis cycle.
     */
    fun configure(newConfig: AudioConfig) {
        config = newConfig
        bandFrequencies = StandardFrequencies.forCount(newConfig.bandCount)
        fftProcessor = FftProcessor(newConfig.fftSize.samples)
        roomCorrector = RoomCorrector.create(
            bandFrequencies = bandFrequencies,
            sampleRate = newConfig.sampleRate.hz,
            fftSize = newConfig.fftSize.samples,
            maxGainDb = newConfig.maxGainDb,
            // High correction limits automatically relax the smoothing to avoid oscillation
            smoothingFactor = newConfig.effectiveSmoothingFactor,
            noiseFloorDb = newConfig.noiseFloorDb
        )
        splMeter = SplMeter(calibrationOffset = 120f)
        noiseProfiler = NoiseProfiler(
            binCount = newConfig.fftSize.binCount,
            maxCaptureFrames = 50,
            gateRange = 6f
        )
        for (i in 0 until DYNAMIC_EQ_COUNT) {
            sweepers[i] = SweeperProcessor(bandFrequencies, dynamicCfgs[i])
        }
        bands = bandFrequencies.mapIndexed { i, freq ->
            EqBand(index = i, centerFreq = freq, gainDb = 0f, targetGainDb = 0f)
        }.toMutableList()
    }

    /**
     * Start the audio analysis loop.
     * Requires RECORD_AUDIO permission.
     *
     * @return true if capture started, false if the microphone could not be opened
     */
    fun start(): Boolean {
        if (isRunning) return true
        isRunning = true

        scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

        ensureConfigured()

        val sampleRate = config.sampleRate.hz
        val fftSize = config.fftSize.samples
        val bufferSize = maxOf(
            AudioRecord.getMinBufferSize(sampleRate, CHANNEL_IN, AUDIO_FORMAT),
            fftSize * 2
        )

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                CHANNEL_IN,
                AUDIO_FORMAT,
                bufferSize
            )
        } catch (e: SecurityException) {
            Log.e(TAG, "RECORD_AUDIO permission not granted", e)
            isRunning = false
            onStartFailed?.invoke("Sin permiso de micrófono: concédelo para iniciar el análisis")
            return false
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create AudioRecord", e)
            isRunning = false
            onStartFailed?.invoke("No se pudo abrir el micrófono de este dispositivo")
            return false
        }

        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            Log.e(TAG, "AudioRecord failed to initialize")
            isRunning = false
            onStartFailed?.invoke("El micrófono no está disponible (¿otra app lo está usando?)")
            return false
        }

        audioRecord?.startRecording()
        startAnalysisLoop(bufferSize, fftSize, sampleRate)
        if (sweeperEnabled.any { it }) startSweeperLoop()
        return true
    }

    /**
     * Stop the engine and release all audio resources.
     */
    fun stop() {
        isRunning = false
        analysisJob?.cancel()
        analysisJob = null
        sweeperJob?.cancel()
        sweeperJob = null
        lastMeasuredLevels = null
        try {
            audioRecord?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "Error stopping AudioRecord", e)
        }
        audioRecord?.release()
        audioRecord = null
        scope?.cancel()
        scope = null
        framesAnalyzed = 0L
    }

    /**
     * Capture the current measured spectrum as the reference (target) signature.
     */
    fun captureReference() {
        val corrector = roomCorrector ?: return
        val levels = corrector.getCurrentBandLevels()
        referenceLevels = levels.copyOf()
        isReferenceCaptured = true
        // Sweeps and corrections restart automatically after every capture
        corrector.reset()
        resetSweepers()
    }

    /**
     * Clear the reference spectrum.
     */
    fun clearReference() {
        referenceLevels = null
        isReferenceCaptured = false
    }

    fun isReferenceCaptured(): Boolean = isReferenceCaptured

    // === The three dynamic EQs (same corrector, different settings) ===

    /** Change one dynamic EQ's parameters (interval, gain, speed, extra sweeps…). */
    fun setDynamicEqConfig(index: Int, cfg: DynamicEqConfig) {
        if (index !in 0 until DYNAMIC_EQ_COUNT) return
        dynamicCfgs[index] = cfg
        sweepers[index]?.applyConfig(cfg)
    }

    /** Enable or disable one dynamic EQ. */
    fun setDynamicEqEnabled(index: Int, enabled: Boolean) {
        if (index !in 0 until DYNAMIC_EQ_COUNT) return
        sweeperEnabled[index] = enabled
        refreshSweeperLoop()
    }

    fun setDynamicEqMixerLevel(index: Int, level: Float) {
        if (index !in 0 until DYNAMIC_EQ_COUNT) return
        sweepers[index]?.mixerLevel = level.coerceIn(0f, 1f)
    }

    /** Linked = each EQ corrects L and R together; unlinked = channels alternate. */
    fun setEqChannelLinked(linked: Boolean) {
        sweepers.forEach { it?.channelLinked = linked }
    }

    /** One dynamic EQ's free-frequency support bands. */
    fun setSupportBands(index: Int, bands: List<SupportBand>) {
        if (index !in 0 until DYNAMIC_EQ_COUNT) return
        supportBandsByEq[index] = bands
    }

    private fun refreshSweeperLoop() {
        if (sweeperEnabled.any { it }) startSweeperLoop() else stopSweeperLoop()
    }

    private fun startSweeperLoop() {
        if (!isRunning || sweeperJob?.isActive == true) return
        if (sweepers.any { it == null }) return
        sweeperJob = scope?.launch(Dispatchers.Default) {
            while (isActive && isRunning) {
                lastMeasuredLevels?.let { levels ->
                    val now = System.currentTimeMillis()
                    for (i in 0 until DYNAMIC_EQ_COUNT) {
                        if (!sweeperEnabled[i]) continue
                        sweepers[i]?.step(levels, now)?.let { step ->
                            onSweepUpdate?.invoke(SweepProcess.entries[i], step)
                        }
                    }
                }
                delay(SweeperProcessor.TICK_MS)
            }
        }
    }

    private fun stopSweeperLoop() {
        sweeperJob?.cancel()
        sweeperJob = null
    }

    private fun resetSweepers() {
        sweepers.forEach { it?.reset() }
    }

    // === Free support bands (one set per dynamic EQ) ===

    /** Sum every dynamic EQ's support bands into a spectrum (simulated corrected output). */
    private fun applySupportBands(spectrum: SpectrumFrame): SpectrumFrame {
        if (supportBandsByEq.all { it.isEmpty() }) return spectrum
        val corrected = spectrum.magnitudesDb.copyOf()
        for (i in spectrum.frequencies.indices) {
            corrected[i] += supportGainAt(spectrum.frequencies[i])
        }
        return spectrum.copy(magnitudesDb = corrected)
    }

    private fun supportGainAt(freqHz: Float): Float {
        var gain = 0f
        for (eqBands in supportBandsByEq) {
            for (band in eqBands) gain += supportTaper(freqHz, band)
        }
        return gain
    }

    /** Sum a dynamic EQ's free support bands into its per-band gain curve. */
    private fun addSupportGains(gains: FloatArray, supportBands: List<SupportBand>): FloatArray {
        if (supportBands.isEmpty()) return gains
        val out = gains.copyOf()
        for (b in out.indices) {
            var extra = 0f
            for (band in supportBands) extra += supportTaper(bandFrequencies[b], band)
            out[b] = (out[b] + extra).coerceIn(-DynamicEqConfig.MAX_GAIN_DB, DynamicEqConfig.MAX_GAIN_DB)
        }
        return out
    }

    private fun supportTaper(freqHz: Float, band: SupportBand): Float {
        if (band.gainDb == 0f || band.frequencyHz <= 0f) return 0f
        val octaves = kotlin.math.abs(kotlin.math.log10(freqHz / band.frequencyHz))
        val width = 1f / band.q.coerceAtLeast(0.5f)
        return if (octaves < width) band.gainDb * (1f - octaves / width) else 0f
    }

    // === Mixed inputs ===

    /**
     * Publish the latest band levels captured from internal app audio so they
     * can be mixed with the microphone (all active inputs sound at once).
     */
    fun setAppCaptureLevels(levels: FloatArray?) {
        appCaptureLevels = levels
    }

    fun getBands(): List<EqBand> = bands.toList()

    fun setBandGain(index: Int, gainDb: Float) {
        if (index in bands.indices) {
            val clamped = gainDb.coerceIn(-config.maxGainDb, config.maxGainDb)
            bands[index] = bands[index].copy(gainDb = clamped, targetGainDb = clamped)
        }
    }

    fun resetBands() {
        for (i in bands.indices) {
            bands[i] = bands[i].copy(gainDb = 0f, targetGainDb = 0f)
        }
        roomCorrector?.reset()
    }

    private fun ensureConfigured() {
        if (fftProcessor == null) {
            configure(config)
        }
    }

    private fun startAnalysisLoop(bufferSize: Int, fftSize: Int, sampleRate: Int) {
        val scope = scope ?: return
        val interval = config.analysisInterval.ms
        val intervalNanos = interval * 1_000_000

        analysisJob = scope.launch {
            val shortBuffer = ShortArray(bufferSize)
            val floatBuffer = FloatArray(fftSize)
            val analysisStartNanos = System.nanoTime()

            while (isActive && isRunning) {
                val cycleStart = System.nanoTime()

                val readResult = audioRecord?.read(shortBuffer, 0, bufferSize) ?: -1
                if (readResult <= 0) {
                    delay(interval)
                    continue
                }

                // Convert 16-bit PCM to float [-1, 1]
                val samplesToRead = minOf(readResult, fftSize)
                for (i in 0 until samplesToRead) {
                    floatBuffer[i] = shortBuffer[i] / 32768.0f
                }
                // Zero-fill remaining if buffer wasn't fully filled
                for (i in samplesToRead until fftSize) {
                    floatBuffer[i] = 0f
                }

                // Run analysis on Default dispatcher
                val result = withContext(Dispatchers.Default) {
                    analyzeFrame(floatBuffer, sampleRate, fftSize)
                }

                result?.let { onAnalysisUpdate?.invoke(it) }

                framesAnalyzed++

                // Pace the loop according to the configured interval
                val elapsed = System.nanoTime() - cycleStart
                val sleepNanos = intervalNanos - elapsed
                if (sleepNanos > 0) {
                    delay(sleepNanos / 1_000_000)
                }
            }
        }
    }

    private fun analyzeFrame(
        samples: FloatArray,
        sampleRate: Int,
        fftSize: Int
    ): AnalysisResult? {
        val fft = fftProcessor ?: return null
        val corrector = roomCorrector ?: return null
        val meter = splMeter ?: return null

        // Compute SPL from time-domain samples
        val spl = meter.computeSpl(samples)

        // Compute frequency spectrum
        val rawMagnitudesDb = fft.computeMagnitudesDb(samples, sampleRate)
        val binFreqs = fft.getBinFrequencies(sampleRate)

        // Subtract noise profile if enabled and available
        val profiler = noiseProfiler
        val magnitudesDb = if (config.noiseSubtractionEnabled && profiler != null && profiler.hasProfile()) {
            profiler.subtractNoise(rawMagnitudesDb)
        } else {
            rawMagnitudesDb
        }

        // Feed noise profiler if capturing
        if (profiler != null && profiler.isCapturing()) {
            val captureDone = profiler.feedFrame(rawMagnitudesDb)
            onNoiseCaptureProgress?.invoke(profiler.getCaptureProgress())
            if (captureDone) {
                onNoiseCaptureComplete?.invoke()
                // Sweeps and corrections restart automatically after captures
                roomCorrector?.reset()
                resetSweepers()
            }
        }

        // Build noise spectrum for display
        val noiseSpectrum: SpectrumFrame? = if (profiler != null && profiler.hasProfile()) {
            SpectrumFrame(
                magnitudesDb = profiler.getNoiseProfile()!!,
                frequencies = binFreqs,
                timestampMs = System.currentTimeMillis()
            )
        } else null

        val spectrum = SpectrumFrame(
            magnitudesDb = magnitudesDb,
            frequencies = binFreqs,
            timestampMs = System.currentTimeMillis()
        )

        // Aggregate into perceptual bands
        val measuredLevels = corrector.aggregateBands(magnitudesDb, binFreqs)
        lastMeasuredLevels = measuredLevels

        // Mix every active input: each band level is the strongest of the
        // microphone and the captured app audio (all inputs sound at once).
        val capture = appCaptureLevels
        val effectiveLevels = if (capture != null && capture.size == measuredLevels.size) {
            FloatArray(measuredLevels.size) { maxOf(measuredLevels[it], capture[it]) }
        } else {
            measuredLevels
        }

        // Compute corrections if we have a reference
        var correctedBands = bands.toList()
        var correctedSpectrum: SpectrumFrame? = null
        var correctionIntensity = 0f

        if (config.correctionEnabled && isReferenceCaptured) {
            val refLevels = referenceLevels ?: effectiveLevels
            correctedBands = corrector.computeCorrections(refLevels, effectiveLevels, bands)
            bands.clear()
            bands.addAll(correctedBands)
            correctedSpectrum = corrector.applyGainsToSpectrum(spectrum, correctedBands)
            correctionIntensity = corrector.correctionIntensity(correctedBands)
        } else if (config.correctionEnabled && !isReferenceCaptured) {
            // Without a reference the corrector falls back to the mean of the
            // active bands as its neutral point (see RoomCorrector).
            val flatRef = FloatArray(bandFrequencies.size) { 0f }
            correctedBands = corrector.computeCorrections(flatRef, effectiveLevels, bands)
            bands.clear()
            bands.addAll(correctedBands)
            correctedSpectrum = corrector.applyGainsToSpectrum(spectrum, correctedBands)
            correctionIntensity = corrector.correctionIntensity(correctedBands)
        }
        correctedSpectrum = correctedSpectrum?.let { applySupportBands(it) }

        // Per dynamic-EQ gain curves (auto corrections + its own support bands)
        val eqGainsL = ArrayList<FloatArray>(DYNAMIC_EQ_COUNT)
        val eqGainsR = ArrayList<FloatArray>(DYNAMIC_EQ_COUNT)
        for (i in 0 until DYNAMIC_EQ_COUNT) {
            val autoL = sweepers[i]?.gainsL() ?: FloatArray(bandFrequencies.size)
            val autoR = sweepers[i]?.gainsR() ?: FloatArray(bandFrequencies.size)
            eqGainsL.add(addSupportGains(autoL, supportBandsByEq[i]))
            eqGainsR.add(addSupportGains(autoR, supportBandsByEq[i]))
        }

        return AnalysisResult(
            measuredSpectrum = spectrum,
            correctedSpectrum = correctedSpectrum,
            bands = correctedBands,
            spl = spl,
            peakSpl = meter.getPeakSpl(),
            averageSpl = meter.getAverageSpl(),
            correctionIntensity = correctionIntensity,
            cpuLoadPercent = 0f,
            framesAnalyzed = framesAnalyzed,
            noiseSpectrum = noiseSpectrum,
            dynamicEqGainsL = eqGainsL,
            dynamicEqGainsR = eqGainsR
        )
    }

    fun isRunning(): Boolean = isRunning

    fun getConfig(): AudioConfig = config

    /**
     * Start a noise capture session. The user should be silent while ambient
     * noise is recorded for [maxCaptureFrames] analysis cycles.
     */
    fun startNoiseCapture() {
        noiseProfiler?.startCapture()
    }

    /**
     * Cancel an ongoing noise capture.
     */
    fun cancelNoiseCapture() {
        noiseProfiler?.cancelCapture()
    }

    /**
     * Clear the stored noise profile.
     */
    fun clearNoiseProfile() {
        noiseProfiler?.clearProfile()
    }

    fun hasNoiseProfile(): Boolean = noiseProfiler?.hasProfile() ?: false

    fun isNoiseCapturing(): Boolean = noiseProfiler?.isCapturing() ?: false

    fun getNoiseCaptureProgress(): Float = noiseProfiler?.getCaptureProgress() ?: 0f
}
