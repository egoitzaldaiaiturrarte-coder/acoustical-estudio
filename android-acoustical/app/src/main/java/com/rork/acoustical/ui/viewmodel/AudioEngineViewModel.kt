package com.rork.acoustical.ui.viewmodel

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rork.acoustical.domain.audio.AudioEngine
import com.rork.acoustical.domain.console.ConsoleChannel
import com.rork.acoustical.domain.console.ConsoleConfig
import com.rork.acoustical.domain.console.ConsoleConnectionState
import com.rork.acoustical.domain.console.ConsoleManager
import com.rork.acoustical.domain.console.ConsoleProtocol
import com.rork.acoustical.domain.console.ConsoleSyncStatus
import com.rork.acoustical.domain.console.ConsoleType
import com.rork.acoustical.domain.console.MeshNetworkManager
import com.rork.acoustical.domain.console.MeshPeer
import com.rork.acoustical.domain.console.UsbAudioSource
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
 * Manages the audio engine lifecycle, configuration, console integration,
 * mesh networking, and UI state.
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
        val targetSplReached: Boolean = false,
        val isNoiseCapturing: Boolean = false,
        val noiseCaptureProgress: Float = 0f,
        val hasNoiseProfile: Boolean = false,
        val noiseSpectrum: SpectrumFrame? = null,
        val noiseSubtractionEnabled: Boolean = true,
        // Console integration
        val consoleConfig: ConsoleConfig = ConsoleConfig.Default,
        val consoleConnectionState: ConsoleConnectionState = ConsoleConnectionState.DISCONNECTED,
        val consoleSyncStatus: ConsoleSyncStatus? = null,
        val usbAudioDevices: List<UsbAudioSource.UsbDeviceInfo> = emptyList(),
        // Mesh network
        val meshIsRunning: Boolean = false,
        val meshIsMaster: Boolean = false,
        val meshPeers: List<MeshPeer> = emptyList()
    )

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private var engine: AudioEngine? = null
    private var notificationUpdateJob: Job? = null
    private var consoleManager: ConsoleManager? = null
    private var meshManager: MeshNetworkManager? = null
    private var usbAudioSource: UsbAudioSource? = null

    init {
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
                        targetSplReached = result.spl >= state.config.targetSpl,
                        noiseSpectrum = result.noiseSpectrum,
                        hasNoiseProfile = result.noiseSpectrum != null
                    )
                }
                // Push corrections to console if connected
                consoleManager?.updateCorrections(result.bands)
                // Update mesh with local SPL
                meshManager?.updateLocalSpl(result.spl)
                meshManager?.updateLocalCorrections(result.bands.map { it.gainDb })
            }
            eng.onNoiseCaptureProgress = { progress ->
                _uiState.update { it.copy(noiseCaptureProgress = progress) }
            }
            eng.onNoiseCaptureComplete = {
                _uiState.update {
                    it.copy(
                        isNoiseCapturing = false,
                        noiseCaptureProgress = 1f,
                        hasNoiseProfile = true
                    )
                }
            }
        }

        // Initialize console manager
        consoleManager = ConsoleManager().also { cm ->
            cm.onConnectionStateChanged = { state ->
                _uiState.update { it.copy(consoleConnectionState = state) }
            }
            cm.onSyncStatus = { status ->
                _uiState.update { it.copy(consoleSyncStatus = status) }
            }
        }

        // Initialize mesh network manager
        meshManager = MeshNetworkManager(application).also { mesh ->
            mesh.onPeersChanged = { peers ->
                _uiState.update { it.copy(meshPeers = peers) }
            }
            mesh.onAggregateReceived = { aggregate ->
                // If we're a listener and receive aggregate from master, update our display
                _uiState.update {
                    it.copy(
                        meshPeers = aggregate.peers.filter { peer -> peer.id != mesh.deviceId }
                    )
                }
            }
        }

        // Initialize USB audio source
        usbAudioSource = UsbAudioSource(application)
        refreshUsbDevices()
    }

    // === Engine Control ===

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

        engine?.start()
        _uiState.update { it.copy(isRunning = true) }
        startNotificationUpdates()
    }

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
                correctionIntensity = 0f,
                isNoiseCapturing = false,
                noiseCaptureProgress = 0f
            )
        }
        notificationUpdateJob?.cancel()
    }

    fun toggleEngine() {
        if (_uiState.value.isRunning) stopEngine() else startEngine()
    }

    // === Audio Config ===

    fun updateConfig(transform: (AudioConfig) -> AudioConfig) {
        val newConfig = transform(_uiState.value.config)
        _uiState.update { it.copy(config = newConfig) }

        val wasRunning = _uiState.value.isRunning
        if (wasRunning) engine?.stop()
        engine?.configure(newConfig)
        if (wasRunning) engine?.start()
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
    fun setNoiseSubtractionEnabled(enabled: Boolean) = updateConfig {
        it.copy(noiseSubtractionEnabled = enabled)
    }.also {
        _uiState.update { state -> state.copy(noiseSubtractionEnabled = enabled) }
    }

    // === Reference & EQ ===

    fun captureReference() {
        engine?.captureReference()
        _uiState.update { it.copy(isReferenceCaptured = true) }
    }

    fun clearReference() {
        engine?.clearReference()
        _uiState.update { it.copy(isReferenceCaptured = false) }
    }

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

    fun resetBands() {
        engine?.resetBands()
        _uiState.update { state ->
            state.copy(bands = state.bands.map { it.copy(gainDb = 0f, targetGainDb = 0f) })
        }
    }

    // === Calibration ===

    fun startCalibration() {
        _uiState.update { it.copy(isCalibrating = true, calibrationProgress = 0f) }
        viewModelScope.launch(Dispatchers.Default) {
            val steps = 100
            for (i in 1..steps) {
                if (!isActive) break
                delay(30)
                _uiState.update { it.copy(calibrationProgress = i / steps.toFloat()) }
            }
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

    // === Noise Profiler ===

    fun startNoiseCapture() {
        if (!_uiState.value.isRunning) return
        engine?.startNoiseCapture()
        _uiState.update {
            it.copy(
                isNoiseCapturing = true,
                noiseCaptureProgress = 0f,
                hasNoiseProfile = false
            )
        }
    }

    fun cancelNoiseCapture() {
        engine?.cancelNoiseCapture()
        _uiState.update {
            it.copy(
                isNoiseCapturing = false,
                noiseCaptureProgress = 0f
            )
        }
    }

    fun clearNoiseProfile() {
        engine?.clearNoiseProfile()
        _uiState.update {
            it.copy(
                hasNoiseProfile = false,
                noiseSpectrum = null,
                noiseCaptureProgress = 0f
            )
        }
    }

    // === Room Profiles ===

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

    // === Console Integration ===

    fun connectConsole() {
        val config = _uiState.value.consoleConfig
        consoleManager?.connect(config)
    }

    fun disconnectConsole() {
        consoleManager?.disconnect()
    }

    fun resetConsoleEq() {
        consoleManager?.resetConsoleEq()
    }

    fun setConsoleProtocol(protocol: ConsoleProtocol) {
        _uiState.update { it.copy(consoleConfig = it.consoleConfig.copy(protocol = protocol)) }
    }

    fun setConsoleType(type: ConsoleType) {
        _uiState.update { it.copy(consoleConfig = it.consoleConfig.copy(type = type)) }
    }

    fun setConsoleIp(ip: String) {
        _uiState.update { it.copy(consoleConfig = it.consoleConfig.copy(ipAddress = ip)) }
    }

    fun setConsolePort(port: Int) {
        _uiState.update { it.copy(consoleConfig = it.consoleConfig.copy(oscPort = port)) }
    }

    fun setConsoleChannel(bus: Int) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(
                channel = it.consoleConfig.channel.copy(bus = bus)
            ))
        }
    }

    fun setConsoleChannelType(type: ConsoleChannel.ChannelType) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(
                channel = it.consoleConfig.channel.copy(channelType = type)
            ))
        }
    }

    fun setAutoCorrectEnabled(enabled: Boolean) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(autoCorrectEnabled = enabled))
        }
        // Reconnect if currently connected to apply the new setting
        if (_uiState.value.consoleConnectionState == ConsoleConnectionState.CONNECTED) {
            val config = _uiState.value.consoleConfig
            consoleManager?.disconnect()
            consoleManager?.connect(config)
        }
    }

    fun setPushGainsEnabled(enabled: Boolean) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(pushGainsToConsole = enabled))
        }
    }

    fun setPullGainsEnabled(enabled: Boolean) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(pullGainsFromConsole = enabled))
        }
    }

    fun setMaxCorrectionDb(db: Float) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(maxCorrectionDb = db))
        }
    }

    fun setCorrectionInterval(ms: Long) {
        _uiState.update {
            it.copy(consoleConfig = it.consoleConfig.copy(correctionIntervalMs = ms))
        }
    }

    // === USB Audio ===

    fun refreshUsbDevices() {
        val devices = usbAudioSource?.scanForAudioDevices() ?: emptyList()
        _uiState.update { it.copy(usbAudioDevices = devices) }
    }

    // === Mesh Network ===

    fun startMeshMaster() {
        meshManager?.startAsMaster()
        _uiState.update {
            it.copy(meshIsRunning = true, meshIsMaster = true)
        }
    }

    fun startMeshListener() {
        meshManager?.startAsListener()
        _uiState.update {
            it.copy(meshIsRunning = true, meshIsMaster = false)
        }
    }

    fun stopMesh() {
        meshManager?.stop()
        _uiState.update {
            it.copy(
                meshIsRunning = false,
                meshIsMaster = false,
                meshPeers = emptyList()
            )
        }
    }

    // === Internal ===

    private fun startNotificationUpdates() {
        notificationUpdateJob?.cancel()
        notificationUpdateJob = viewModelScope.launch(Dispatchers.Default) {
            while (isActive) {
                delay(1000)
                val state = _uiState.value
                if (state.isRunning) {
                    AudioAnalysisService.engine?.let { _ ->
                        // Service notification updates could go here
                    }
                }
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        engine?.stop()
        engine = null
        consoleManager?.disconnect()
        consoleManager = null
        meshManager?.stop()
        meshManager = null
        usbAudioSource?.disconnect()
        usbAudioSource = null
    }
}
