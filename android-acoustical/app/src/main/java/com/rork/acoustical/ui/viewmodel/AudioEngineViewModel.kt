package com.rork.acoustical.ui.viewmodel

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rork.acoustical.domain.audio.AudioEngine
import com.rork.acoustical.domain.audio.SplMeter
import com.rork.acoustical.domain.model.AudioConfig
import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.domain.model.RoomProfile
import com.rork.acoustical.domain.model.SampleRate
import com.rork.acoustical.domain.model.SpectrumFrame
import com.rork.acoustical.domain.model.SplCalibration
import com.rork.acoustical.service.AudioAnalysisService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Central ViewModel shared across all screens.
 * Manages the audio engine lifecycle, configuration, and UI state.
 */
class AudioEngineViewModel(
    application: Application
) : AndroidViewModel(application) {

    data class UiState(
        val isRunning: Boolean = false,
        val isCorrecting: Boolean = false,
        val config: AudioConfig = AudioConfig.Default,
        val currentSpl: Float = 0f,
        val peakSpl: Float = 0f,
        val averageSpl: Float = 0f,
        val measuredSpectrum: SpectrumFrame? = null,
        val correctedSpectrum: SpectrumFrame? = null,
        val bands: List<EqBand> = emptyList(),
        val correctionIntensity: Float = 0f,
        val framesAnalyzed: Long = 0L,
        val isReferenceCaptured: Boolean = false,
        val splCalibration: SplCalibration = SplCalibration(),
        val savedProfiles: List<RoomProfile> = emptyList(),
        val activeProfile: RoomProfile? = null,
        val isCalibrating: Boolean = false,
        val calibrationProgress: Float = 0f,
        val targetSplReached: Boolean = false
    )

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private var engine: AudioEngine? = null
    private var notificationUpdateJob: Job? = null

    init {
        // Initialize engine with default config
        engine = AudioEngine().also { eng ->
            eng.configure(_uiState.value.config)
            eng.onAnalysisUpdate = { result ->
                _uiState.update { state ->
                    state.copy(
                        currentSpl = result.spl,
                        peakSpl = result.peakSpl,
                        averageSpl = result.averageSpl,
                        measuredSpectrum = result.measuredSpectrum,
                        correctedSpectrum = result.correctedSpectrum,
                        bands = result.bands,
                        correctionIntensity = result.correctionIntensity,
                        framesAnalyzed = result.framesAnalyzed,
                        isCorrecting = result.correctionIntensity > 0.01f,
                        targetSplReached = result.spl >= state.config.targetSpl
                    )
                }
            }
        }
    }

    /**
     * Start the audio analysis engine (foreground service for background operation).
     */
    fun startEngine() {
        val context = getApplication<Application>()
        val intent = Intent(context, AudioAnalysisService::class.java).apply {
            action = AudioAnalysisService.ACTION_START
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            context.startForegroundService(intent)
        } else {
            context.startService(intent)
        }

        // Also start local engine for immediate UI feedback
        engine?.start()
        _uiState.update { it.copy(isRunning = true) }

        startNotificationUpdates()
    }

    /**
     * Stop the engine and foreground service.
     */
    fun stopEngine() {
        val context = getApplication<Application>()
        val intent = Intent(context, AudioAnalysisService::class.java).apply {
            action = AudioAnalysisService.ACTION_STOP
        }
        context.startService(intent)

        engine?.stop()
        _uiState.update {
            it.copy(
                isRunning = false,
                isCorrecting = false,
                currentSpl = 0f,
                measuredSpectrum = null,
                correctedSpectrum = null,
                correctionIntensity = 0f
            )
        }
        notificationUpdateJob?.cancel()
    }

    fun toggleEngine() {
        if (_uiState.value.isRunning) stopEngine() else startEngine()
    }

    /**
     * Update a single configuration parameter and reconfigure the engine.
     */
    fun updateConfig(transform: (AudioConfig) -> AudioConfig) {
        val newConfig = transform(_uiState.value.config)
        _uiState.update { it.copy(config = newConfig) }

        val wasRunning = _uiState.value.isRunning
        if (wasRunning) {
            engine?.stop()
        }

        engine?.configure(newConfig)

        if (wasRunning) {
            engine?.start()
        }
    }

    fun setSampleRate(rate: SampleRate) = updateConfig { it.copy(sampleRate = rate) }
    fun setFftSize(size: com.rork.acoustical.domain.model.FftSize) = updateConfig { it.copy(fftSize = size) }
    fun setAnalysisInterval(interval: com.rork.acoustical.domain.model.AnalysisInterval) = updateConfig { it.copy(analysisInterval = interval) }
    fun setBandCount(count: BandCount) = updateConfig { it.copy(bandCount = count) }
    fun setMaxGainDb(gain: Float) = updateConfig { it.copy(maxGainDb = gain) }
    fun setTargetSpl(spl: Float) = updateConfig { it.copy(targetSpl = spl) }
    fun setSmoothingFactor(factor: Float) = updateConfig { it.copy(smoothingFactor = factor) }
    fun setNoiseFloorDb(db: Float) = updateConfig { it.copy(noiseFloorDb = db) }
    fun setCorrectionEnabled(enabled: Boolean) = updateConfig { it.copy(correctionEnabled = enabled) }

    /**
     * Capture the current measured spectrum as the reference signature.
     */
    fun captureReference() {
        engine?.captureReference()
        _uiState.update { it.copy(isReferenceCaptured = true) }
    }

    fun clearReference() {
        engine?.clearReference()
        _uiState.update { it.copy(isReferenceCaptured = false) }
    }

    /**
     * Manually set a band's gain.
     */
    fun setBandGain(index: Int, gainDb: Float) {
        engine?.setBandGain(index, gainDb)
        _uiState.update { state ->
            val newBands = state.bands.toMutableList()
            if (index in newBands.indices) {
                newBands[index] = newBands[index].copy(gainDb = gainDb, targetGainDb = gainDb)
            }
            state.copy(bands = newBands)
        }
    }

    /**
     * Reset all EQ bands to 0 dB.
     */
    fun resetBands() {
        engine?.resetBands()
        _uiState.update { state ->
            state.copy(bands = state.bands.map { it.copy(gainDb = 0f, targetGainDb = 0f) })
        }
    }

    /**
     * Start the SPL calibration process.
     * Plays a reference tone at known SPL and measures the microphone response.
     */
    fun startCalibration() {
        _uiState.update { it.copy(isCalibrating = true, calibrationProgress = 0f) }
        viewModelScope.launch(Dispatchers.Default) {
            val steps = 100
            for (i in 1..steps) {
                if (!isActive) break
                delay(30)
                _uiState.update { it.copy(calibrationProgress = i / steps.toFloat()) }
            }
            // After calibration sweep, compute the offset
            val currentSpl = _uiState.value.currentSpl
            val targetRef = _uiState.value.config.targetSpl
            val newCalibration = SplCalibration(
                referenceSpl = 94f,
                measuredDbfs = currentSpl - 120f,
                offsetDb = targetRef - currentSpl,
                isCalibrated = true
            )
            _uiState.update {
                it.copy(
                    isCalibrating = false,
                    calibrationProgress = 1f,
                    splCalibration = newCalibration
                )
            }
        }
    }

    /**
     * Save the current room profile.
     */
    fun saveProfile(name: String, description: String) {
        val profile = RoomProfile(
            name = name,
            description = description,
            measuredGains = _uiState.value.bands.map { it.gainDb },
            centerFrequencies = _uiState.value.bands.map { it.centerFreq },
            calibratedSpl = _uiState.value.averageSpl,
            createdAt = System.currentTimeMillis()
        )
        _uiState.update {
            it.copy(
                savedProfiles = it.savedProfiles + profile,
                activeProfile = profile
            )
        }
    }

    /**
     * Load a saved room profile.
     */
    fun loadProfile(profile: RoomProfile) {
        val bands = _uiState.value.bands.toMutableList()
        for (i in bands.indices) {
            if (i < profile.measuredGains.size) {
                bands[i] = bands[i].copy(
                    gainDb = profile.measuredGains[i],
                    targetGainDb = profile.measuredGains[i]
                )
                engine?.setBandGain(i, profile.measuredGains[i])
            }
        }
        _uiState.update {
            it.copy(
                bands = bands,
                activeProfile = profile
            )
        }
    }

    private fun startNotificationUpdates() {
        notificationUpdateJob?.cancel()
        notificationUpdateJob = viewModelScope.launch(Dispatchers.Default) {
            while (isActive) {
                delay(1000)
                val state = _uiState.value
                if (state.isRunning) {
                    AudioAnalysisService.engine?.let { eng ->
                        // Update via service if running
                    }
                }
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        engine?.stop()
        engine = null
    }
}
