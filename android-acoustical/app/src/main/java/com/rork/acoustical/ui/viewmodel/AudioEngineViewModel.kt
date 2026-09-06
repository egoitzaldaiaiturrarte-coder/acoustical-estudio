package com.rork.acoustical.ui.viewmodel

import android.app.Application
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rork.acoustical.domain.audio.AudioEngine
import com.rork.acoustical.domain.audio.SweepProcess
import com.rork.acoustical.domain.audio.LocationProvider
import com.rork.acoustical.domain.audio.AudioOutputInfo
import com.rork.acoustical.domain.audio.BluetoothAudioManager
import com.rork.acoustical.domain.audio.EngineerAgent
import com.rork.acoustical.domain.audio.FocusModeManager
import com.rork.acoustical.domain.audio.WalkieTalkieManager
import com.rork.acoustical.domain.audio.Rt60Estimator
import com.rork.acoustical.domain.audio.TestSignalPlayer
import com.rork.acoustical.domain.audio.TestSignalType
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
import com.rork.acoustical.domain.model.AppMode
import com.rork.acoustical.domain.model.AgentAdvice
import com.rork.acoustical.domain.model.AgentMode
import com.rork.acoustical.domain.model.AgentSeverity
import com.rork.acoustical.domain.model.AudioConfig
import com.rork.acoustical.domain.model.AutoCheckConfig
import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.BitDepth
import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.domain.model.MusicianState
import com.rork.acoustical.domain.model.OutputTarget
import com.rork.acoustical.domain.model.PanMatrix
import com.rork.acoustical.domain.model.PanZone
import com.rork.acoustical.domain.model.ProbeQuality
import com.rork.acoustical.domain.model.ReferenceSource
import com.rork.acoustical.domain.model.RoomProfile
import com.rork.acoustical.domain.model.EqChannel
import com.rork.acoustical.domain.model.InputTarget
import com.rork.acoustical.domain.model.InputType
import com.rork.acoustical.domain.model.SampleRate
import com.rork.acoustical.domain.model.ScenarioPreset
import com.rork.acoustical.domain.model.SpatialPosition
import com.rork.acoustical.domain.model.SplCalibration
import com.rork.acoustical.domain.model.SplCompensation
import com.rork.acoustical.domain.model.StereoMode
import com.rork.acoustical.domain.model.SpectrumFrame
import com.rork.acoustical.domain.model.SupportBand
import com.rork.acoustical.domain.model.WorkConfig
import com.rork.acoustical.domain.model.WorkDevice
import com.rork.acoustical.domain.model.WorkEnvironmentType
import com.rork.acoustical.domain.model.WorkSession
import com.rork.acoustical.domain.model.DeviceConnection
import com.rork.acoustical.domain.model.DeviceRole
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.DeviceType
import com.rork.acoustical.domain.model.DistanceMeasurement
import com.rork.acoustical.domain.model.DistanceStep
import com.rork.acoustical.domain.model.GpsPoint
import com.rork.acoustical.service.AudioAnalysisService
import com.rork.acoustical.service.InternalCaptureService
import com.rork.acoustical.service.ProfileStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

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
        /** Non-null when the engine failed to start (permission, mic busy…). */
        val engineError: String? = null,
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
        val meshPeers: List<MeshPeer> = emptyList(),
        // Audio delay & geo
        val audioDelayMs: Float = 25f,
        val geoAutoAdjust: Boolean = false,
        val geoInfo: LocationProvider.GeoAcousticInfo = LocationProvider.GeoAcousticInfo(),
        val geoAdjustmentApplied: Float = 0f,
        val rt60Ms: Float = 0f,
        val scenarioPreset: String = "CUSTOM",
        // Controller state
        val session: WorkSession = WorkSession(name = "", pin = ""),
        val workConfig: WorkConfig = WorkConfig(),
        val spatialPosition: SpatialPosition = SpatialPosition(),
        val splCompensation: SplCompensation = SplCompensation(),
        val inputs: List<InputTarget> = emptyList(),
        val outputs: List<OutputTarget> = emptyList(),
        val bluetoothDevices: List<BluetoothAudioManager.BtDevice> = emptyList(),
        val isBtScanning: Boolean = false,
        val isBluetoothEnabled: Boolean = false,
        val isBluetoothAvailable: Boolean = false,
        val audioOutputDevices: List<AudioOutputInfo> = emptyList(),
        val musicianState: MusicianState = MusicianState(),
        val splHistoryMeasured: List<Float> = emptyList(),
        val splHistoryCorrected: List<Float> = emptyList(),
        // Engineer agent
        val agentMode: AgentMode = AgentMode.ASSISTANT,
        val agentAdvices: List<AgentAdvice> = emptyList(),
        // Sweep modifiers affecting every action
        val isFastSweep: Boolean = false,
        val isFineSweep: Boolean = false,
        // Linked pan matrix L/Mid/R/Lados
        val panMatrix: PanMatrix = PanMatrix.Centered,
        // Emitter→receiver distance measurement
        val distanceMeasure: DistanceMeasurement = DistanceMeasurement(),
        // Focus (concert) mode & walkie-talkie
        val focusModeActive: Boolean = false,
        val walkieActive: Boolean = false,
        val walkieTargets: Set<String> = emptySet(),
        // Outputs currently playing a test signal (several can sound at once)
        val testingOutputIds: Set<String> = emptySet(),
        // EQ channels — L and R can be unlinked and trimmed separately
        val bandsL: List<EqBand> = emptyList(),
        val bandsR: List<EqBand> = emptyList(),
        val eqLinked: Boolean = true,
        val eqChannel: EqChannel = EqChannel.LEFT,
        // Processors: Auto ayuda / EQ normal / Auto-chequeo
        val autoHelpActive: Boolean = true,
        val autoHelpMixerLevel: Float = 0.8f,
        val normalSweepActive: Boolean = false,
        val normalMixerLevel: Float = 0.8f,
        val checkMixerLevel: Float = 0.8f,
        val sweepHelp: SweepStatus? = null,
        val sweepNormal: SweepStatus? = null,
        val sweepCheck: SweepStatus? = null,
        val supportBandsEq: List<SupportBand> = SupportBand.defaults(),
        val supportBandsCheck: List<SupportBand> = SupportBand.defaults(),
        // Simultaneous digital inputs
        val isAppCaptureActive: Boolean = false,
        val isExternalInputActive: Boolean = false
    ) {
        val sweepFactor: Float
            get() = when {
                isFineSweep -> 0.1f
                isFastSweep -> 4f
                else -> 1f
            }

        val sweepLabel: String
            get() = when {
                isFineSweep -> "Ajuste fino ×0.1"
                isFastSweep -> "Barrido rápido ×4"
                else -> "Paso normal"
            }
    }

    /** Live status of one automated processor (decision every 800 ms, values every 10 ms). */
    data class SweepStatus(
        val bandHz: Float,
        val gainDb: Float,
        val smoothingMs: Float
    )

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private var engine: AudioEngine? = null
    private var notificationUpdateJob: Job? = null
    private var consoleManager: ConsoleManager? = null
    private var meshManager: MeshNetworkManager? = null
    private var usbAudioSource: UsbAudioSource? = null
    private var locationProvider: LocationProvider? = null
    private var rt60Estimator: Rt60Estimator? = null
    private var geoJob: Job? = null
    private var autoCheckJob: Job? = null
    private var btAudioManager: BluetoothAudioManager? = null
    private var musicianTrackingJob: Job? = null
    private val engineerAgent = EngineerAgent()
    private var focusModeManager: FocusModeManager? = null
    private var walkieManager: WalkieTalkieManager? = null
    private var profileStore: ProfileStore? = null
    private val testSignalPlayer = TestSignalPlayer()
    // Manual per-channel EQ trims on top of the engine's correction gains
    private val eqOffsetsL = mutableListOf<Float>()
    private val eqOffsetsR = mutableListOf<Float>()
    // Throttle for the 100 Hz sweeper status updates
    private var lastSweepUiMs = 0L

    init {
        initInputs()
        engine = AudioEngine().also { eng ->
            eng.configure(_uiState.value.config)
            // Single engine shared with the foreground service — two AudioRecords
            // fighting for the mic is what left the analysis dead on device.
            AudioAnalysisService.engine = eng
            AudioAnalysisService.onStopRequested = { stopEngine() }
            eng.onStartFailed = { message ->
                _uiState.update { it.copy(isRunning = false, engineError = message) }
            }
            eng.onAnalysisUpdate = { result ->
                if (result.framesAnalyzed == 1L) {
                    Log.d("AudioEngineVM", "Primer frame analizado: ${result.bands.size} bandas, SPL %.1f".format(result.spl))
                }
                val rt60 = rt60Estimator
                rt60?.feedFrame(result.measuredSpectrum.magnitudesDb)
                _uiState.update { state ->
                    val maxHistorySize = (30000L / state.config.analysisInterval.ms).toInt().coerceAtLeast(30)
                    val avgBandGain = if (result.bands.isNotEmpty()) {
                        result.bands.map { it.gainDb }.average().toFloat()
                    } else 0f
                    val correctedSpl = result.spl + avgBandGain
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
                        hasNoiseProfile = result.noiseSpectrum != null,
                        rt60Ms = rt60?.currentRt60Ms ?: 0f,
                        musicianState = state.musicianState.copy(
                            isOverLimit = result.spl >= state.musicianState.safeSplLimit
                        ),
                        splHistoryMeasured = (state.splHistoryMeasured + result.spl).takeLast(maxHistorySize),
                        splHistoryCorrected = (state.splHistoryCorrected + correctedSpl).takeLast(maxHistorySize)
                    )
                }
                // Push corrections to console if connected
                consoleManager?.updateCorrections(result.bands)
                // Update mesh with local SPL
                meshManager?.updateLocalSpl(result.spl)
                meshManager?.updateLocalCorrections(result.bands.map { it.gainDb })
                publishChannelBands()
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
            eng.onSweepUpdate = { process, step ->
                val now = System.currentTimeMillis()
                if (now - lastSweepUiMs >= 100) {
                    lastSweepUiMs = now
                    val status = SweepStatus(
                        bandHz = step.centerFreqHz,
                        gainDb = step.gainDb,
                        smoothingMs = step.smoothingMs
                    )
                    _uiState.update { st ->
                        when (process) {
                            SweepProcess.AUTO_HELP -> st.copy(sweepHelp = status)
                            SweepProcess.EQ_NORMAL -> st.copy(sweepNormal = status)
                            SweepProcess.AUTO_CHECK -> st.copy(sweepCheck = status)
                        }
                    }
                }
            }
        }

        // Show the EQ faders immediately (flat) instead of an empty placeholder
        syncBandsFromEngine()

        // Publish the default support bands and keep the app-capture feed alive
        engine?.setSupportBands(_uiState.value.supportBandsEq, _uiState.value.supportBandsCheck)
        viewModelScope.launch(Dispatchers.Default) {
            while (isActive) {
                engine?.setAppCaptureLevels(
                    if (_uiState.value.isAppCaptureActive && InternalCaptureService.isCapturing) {
                        InternalCaptureService.latestLevels
                    } else null
                )
                delay(50)
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

        // Initialize location provider
        locationProvider = LocationProvider(application)

        // Initialize RT60 estimator
        rt60Estimator = Rt60Estimator(
            bandCount = 8,
            historySize = 64,
            sampleIntervalMs = _uiState.value.config.analysisInterval.ms
        )

        // Initialize Bluetooth audio manager
        btAudioManager = BluetoothAudioManager(application).also { btManager ->
            viewModelScope.launch {
                btManager.devices.collect { devices ->
                    _uiState.update { it.copy(bluetoothDevices = devices) }
                }
            }
            viewModelScope.launch {
                btManager.isScanning.collect { scanning ->
                    _uiState.update { it.copy(isBtScanning = scanning) }
                }
            }
            viewModelScope.launch {
                btManager.isBluetoothEnabled.collect { enabled ->
                    _uiState.update { it.copy(isBluetoothEnabled = enabled) }
                }
            }
            viewModelScope.launch {
                btManager.isBluetoothAvailable.collect { available ->
                    _uiState.update { it.copy(isBluetoothAvailable = available) }
                }
            }
        }
        _uiState.update {
            it.copy(
                isBluetoothEnabled = btAudioManager?.isBluetoothEnabled?.value == true,
                isBluetoothAvailable = btAudioManager?.isBluetoothAvailable?.value == true
            )
        }

        // Initialize focus mode + walkie-talkie link
        focusModeManager = FocusModeManager(application)
        walkieManager = WalkieTalkieManager(application).also { wm ->
            wm.onAudioChunk = { chunk ->
                val mesh = meshManager
                if (mesh != null) {
                    mesh.broadcastWalkieAudio(mesh.deviceId, chunk, _uiState.value.walkieTargets)
                }
            }
        }
        meshManager?.onWalkieAudio = { _, chunk -> walkieManager?.playChunk(chunk) }

        // Initialize outputs with local device
        initLocalOutput()

        // Refresh system audio outputs
        refreshAudioOutputs()

        // Restore persisted room profiles (they survive app restarts)
        profileStore = ProfileStore(application).also { store ->
            val profiles = store.loadProfiles()
            val activeName = store.loadActiveProfileName()
            val active = profiles.firstOrNull { it.name == activeName }
            _uiState.update { it.copy(savedProfiles = profiles, activeProfile = active) }
        }
    }

    // === Session Management ===

    fun createSession(name: String, pin: String) {
        val device = WorkDevice(
            id = "local",
            name = android.os.Build.MODEL ?: "Este dispositivo",
            type = DeviceType.PHONE,
            connection = DeviceConnection.WIFI_MESH,
            role = DeviceRole.HOST,
            state = DeviceState.CONNECTED,
            isActive = true,
            isThisDevice = true,
            addedAtMs = System.currentTimeMillis()
        )
        _uiState.update {
            it.copy(
                session = WorkSession(
                    name = name,
                    pin = pin,
                    isHost = true,
                    devices = listOf(device),
                    createdAtMs = System.currentTimeMillis(),
                    isActive = true
                )
            )
        }
        // Start mesh as master
        startMeshMaster()
        updateInputs()
    }

    fun joinSession(name: String, pin: String) {
        val device = WorkDevice(
            id = "local",
            name = android.os.Build.MODEL ?: "Este dispositivo",
            type = DeviceType.PHONE,
            connection = DeviceConnection.WIFI_MESH,
            role = DeviceRole.LISTENER,
            state = DeviceState.CONNECTING,
            isActive = true,
            isThisDevice = true,
            addedAtMs = System.currentTimeMillis()
        )
        _uiState.update {
            it.copy(
                session = WorkSession(
                    name = name,
                    pin = pin,
                    isHost = false,
                    devices = listOf(device),
                    createdAtMs = System.currentTimeMillis(),
                    isActive = true
                )
            )
        }
        // Start mesh as listener
        startMeshListener()
        updateInputs()
    }

    fun leaveSession() {
        stopMesh()
        _uiState.update {
            it.copy(
                session = WorkSession(name = "", pin = ""),
                spatialPosition = SpatialPosition(),
                splCompensation = SplCompensation()
            )
        }
        updateInputs()
    }

    fun addDevice(
        name: String,
        type: DeviceType,
        connection: DeviceConnection,
        ipAddress: String,
        port: Int
    ) {
        val device = WorkDevice(
            id = "dev_${System.currentTimeMillis()}",
            name = name,
            type = type,
            connection = connection,
            role = DeviceRole.CONTROLLER,
            state = DeviceState.CONNECTING,
            ipAddress = ipAddress,
            port = port,
            isActive = true,
            addedAtMs = System.currentTimeMillis()
        )
        _uiState.update { state ->
            state.copy(session = state.session.copy(devices = state.session.devices + device))
        }
        // Attempt connection based on type
        when (connection) {
            DeviceConnection.WIFI_OSC -> {
                _uiState.update { it.copy(consoleConfig = it.consoleConfig.copy(ipAddress = ipAddress, oscPort = port)) }
                connectConsole()
            }
            DeviceConnection.USB -> refreshUsbDevices()
            else -> { /* Mesh/Bluetooth handled separately */ }
        }
        // Simulate connection success
        viewModelScope.launch {
            delay(1500)
            _uiState.update { state ->
                val updated = state.session.devices.map { d ->
                    if (d.id == device.id) d.copy(state = DeviceState.CONNECTED, latencyMs = 15f) else d
                }
                state.copy(session = state.session.copy(devices = updated))
            }
            updateInputs()
        }
    }

    fun removeDevice(deviceId: String) {
        _uiState.update { state ->
            state.copy(session = state.session.copy(devices = state.session.devices.filterNot { it.id == deviceId }))
        }
        updateInputs()
    }

    fun toggleDeviceActive(deviceId: String) {
        _uiState.update { state ->
            val updated = state.session.devices.map { d ->
                if (d.id == deviceId) d.copy(isActive = !d.isActive) else d
            }
            state.copy(session = state.session.copy(devices = updated))
        }
        updateInputs()
    }

    // === Work Configuration ===

    fun setWorkEnvironment(env: WorkEnvironmentType) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(environment = env)) }
        updateInputs()
    }

    fun setReferenceSource(ref: ReferenceSource) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(referenceSource = ref)) }
        updateInputs()
    }

    fun setBitDepth(depth: BitDepth) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(bitDepth = depth, autoCheck = it.workConfig.autoCheck.copy(bitDepth = depth))) }
    }

    fun setWorkSampleRate(rate: SampleRate) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(sampleRate = rate)) }
        setSampleRate(rate)
    }

    fun setStereoMode(mode: StereoMode) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(stereoMode = mode)) }
    }

    fun setAppMode(mode: AppMode) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(appMode = mode)) }
    }

    // === Spatial Position & SPL Compensation ===

    fun updateSpatialPosition(pos: SpatialPosition) {
        val compensation = SplCompensation.calculate(pos, _uiState.value.config.targetSpl, _uiState.value.workConfig.environment)
        _uiState.update {
            it.copy(spatialPosition = pos, splCompensation = compensation)
        }
        // Push compensation to console if connected
        if (_uiState.value.consoleConnectionState == ConsoleConnectionState.CONNECTED) {
            consoleManager?.updateCorrections(
                _uiState.value.bands.map { band ->
                    band.copy(gainDb = band.gainDb + compensation.gainAdjustDb / band.index.coerceAtLeast(1))
                }
            )
        }
    }

    // === Auto-Check ===

    fun setAutoCheckEnabled(enabled: Boolean) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(autoCheck = it.workConfig.autoCheck.copy(enabled = enabled))) }
        if (enabled) startAutoCheck() else stopAutoCheck()
        // Its automatic correction starts from the treble
        engine?.setCheckSweepEnabled(enabled && _uiState.value.isRunning)
    }

    fun setAutoCheckInterval(seconds: Int) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(autoCheck = it.workConfig.autoCheck.copy(intervalSeconds = seconds))) }
    }

    fun setAutoCheckQuality(quality: ProbeQuality) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(autoCheck = it.workConfig.autoCheck.copy(quality = quality))) }
    }

    fun setAutoCheckBitDepth(depth: BitDepth) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(autoCheck = it.workConfig.autoCheck.copy(bitDepth = depth))) }
    }

    private fun startAutoCheck() {
        autoCheckJob?.cancel()
        autoCheckJob = viewModelScope.launch(Dispatchers.Default) {
            while (isActive) {
                val interval = _uiState.value.workConfig.autoCheck.intervalMs
                delay(interval)
                if (!_uiState.value.isRunning) continue
                if (_uiState.value.agentMode != AgentMode.OFF) refreshAgentAdvices()
                // The fast two-band correction is driven by the engine itself;
                // this loop keeps the input states fresh at the probe cadence
                updateInputs()
            }
        }
    }

    private fun stopAutoCheck() {
        autoCheckJob?.cancel()
        autoCheckJob = null
    }

    // === Inputs (Ruteos) ===

    private fun initInputs() {
        val inputs = listOf(
            InputTarget("in_mic", InputType.MIC, "Micrófono del móvil", isActive = true, subtitle = "Motor parado"),
            InputTarget("in_app", InputType.APP_CAPTURE, "Audio interno de apps", isActive = false, subtitle = "Spotify, YouTube… (captura digital)"),
            InputTarget("in_external", InputType.EXTERNAL, "Entrada externa (otro móvil)", isActive = false, subtitle = "Disponible con sesión activa"),
            InputTarget("in_usb", InputType.USB, "USB Audio", isActive = false, subtitle = "Interface USB"),
            InputTarget("in_console", InputType.CONSOLE_IN, "Consola In", isActive = false, subtitle = "Entrada OSC"),
            InputTarget("in_ref", InputType.FILE_REFERENCE, "Archivo / Referencia", isActive = true, subtitle = "Fuente de señal")
        )
        _uiState.update { it.copy(inputs = inputs) }
    }

    private fun updateInputs() {
        _uiState.update { state ->
            val devices = state.session.devices
            state.copy(
                inputs = state.inputs.map { input ->
                    when (input.id) {
                        "in_mic" -> input.copy(
                            subtitle = if (state.isRunning) "Motor activo" else "Motor parado"
                        )
                        "in_app" -> input.copy(
                            isActive = state.isAppCaptureActive,
                            subtitle = if (state.isAppCaptureActive) "Capturando audio interno" else "Toca Capturar en Procesos"
                        )
                        "in_external" -> input.copy(
                            isActive = state.isExternalInputActive && state.session.isActive,
                            subtitle = when {
                                !state.session.isActive -> "Requiere sesión activa"
                                state.isExternalInputActive -> "Recibiendo de otro móvil"
                                else -> "Disponible"
                            }
                        )
                        "in_usb" -> input.copy(
                            isActive = input.isActive && (
                                devices.any { it.type == DeviceType.USB_AUDIO && it.isActive } ||
                                    state.usbAudioDevices.isNotEmpty()
                                )
                        )
                        "in_console" -> input.copy(
                            isActive = input.isActive && (
                                devices.any { it.type == DeviceType.CONSOLE && it.isActive } ||
                                    state.consoleConnectionState == ConsoleConnectionState.CONNECTED
                                )
                        )
                        "in_ref" -> input.copy(subtitle = state.workConfig.referenceSource.label)
                        else -> input
                    }
                }
            )
        }
    }

    /** Activate or deactivate an input from the routing screen. */
    fun toggleInputActive(id: String) {
        if (id == "in_external") {
            _uiState.update { it.copy(isExternalInputActive = !it.isExternalInputActive) }
            updateInputs()
            return
        }
        _uiState.update { state ->
            state.copy(
                inputs = state.inputs.map { input ->
                    if (input.id == id) input.copy(isActive = !input.isActive) else input
                }
            )
        }
    }

    /** Trim an input's gain (−12…+12 dB). */
    fun setInputGainDb(id: String, gainDb: Float) {
        val clamped = gainDb.coerceIn(-12f, 12f)
        _uiState.update { state ->
            state.copy(
                inputs = state.inputs.map { input ->
                    if (input.id == id) input.copy(gainDb = clamped) else input
                }
            )
        }
    }

    // === Processors: Auto ayuda / EQ normal / Auto-chequeo ===

    /** Toggle the fast sweeper ("Auto ayuda"). Starts the engine if stopped. */
    fun setAutoHelpEnabled(enabled: Boolean) {
        _uiState.update { it.copy(autoHelpActive = enabled) }
        if (enabled && !_uiState.value.isRunning) {
            startEngine()
        } else {
            engine?.setAutoHelpEnabled(enabled && _uiState.value.isRunning)
        }
    }

    /** The Auto ayuda mixer level (0..1), independent from the rest. */
    fun setAutoHelpMixerLevel(level: Float) {
        val clamped = level.coerceIn(0f, 1f)
        _uiState.update { it.copy(autoHelpMixerLevel = clamped) }
        engine?.setAutoHelpMixerLevel(clamped)
    }

    /** Toggle the "EQ normal" automatic correction (starts from the bass). */
    fun setNormalSweepEnabled(enabled: Boolean) {
        _uiState.update { it.copy(normalSweepActive = enabled) }
        if (enabled && !_uiState.value.isRunning) {
            startEngine()
        } else {
            engine?.setNormalSweepEnabled(enabled && _uiState.value.isRunning)
        }
    }

    /** The EQ normal mixer level (0..1). */
    fun setNormalSweepMixerLevel(level: Float) {
        val clamped = level.coerceIn(0f, 1f)
        _uiState.update { it.copy(normalMixerLevel = clamped) }
        engine?.setNormalSweepMixerLevel(clamped)
    }

    /** The Auto-chequeo mixer level (0..1). */
    fun setCheckSweepMixerLevel(level: Float) {
        val clamped = level.coerceIn(0f, 1f)
        _uiState.update { it.copy(checkMixerLevel = clamped) }
        engine?.setCheckSweepMixerLevel(clamped)
    }

    /** Edit one of the EQ processor's four free-frequency support bands. */
    fun setSupportBandEq(index: Int, frequencyHz: Float, gainDb: Float) {
        _uiState.update { st ->
            st.copy(
                supportBandsEq = st.supportBandsEq.mapIndexed { i, band ->
                    if (i == index) band.copy(frequencyHz = frequencyHz, gainDb = gainDb) else band
                }
            )
        }
        pushSupportBands()
    }

    /** Edit one of the auto-check processor's four free-frequency support bands. */
    fun setSupportBandCheck(index: Int, frequencyHz: Float, gainDb: Float) {
        _uiState.update { st ->
            st.copy(
                supportBandsCheck = st.supportBandsCheck.mapIndexed { i, band ->
                    if (i == index) band.copy(frequencyHz = frequencyHz, gainDb = gainDb) else band
                }
            )
        }
        pushSupportBands()
    }

    private fun pushSupportBands() {
        val st = _uiState.value
        engine?.setSupportBands(st.supportBandsEq, st.supportBandsCheck)
    }

    // === Internal app-audio capture (one of the simultaneous inputs) ===

    /** The consent intent for capturing internal app audio (Android 10+). */
    fun captureRequestIntent(): Intent? {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) return null
        val manager = getApplication<Application>()
            .getSystemService(Context.MEDIA_PROJECTION_SERVICE) as? android.media.projection.MediaProjectionManager
        return manager?.createScreenCaptureIntent()
    }

    /** Called from Ruteos with the projection consent result. */
    fun onCaptureResult(resultCode: Int, data: Intent?) {
        val context = getApplication<Application>()
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q || data == null) return
        val intent = Intent(context, InternalCaptureService::class.java).apply {
            action = InternalCaptureService.ACTION_START
            putExtra(InternalCaptureService.EXTRA_RESULT_CODE, resultCode)
            putExtra(InternalCaptureService.EXTRA_DATA, data)
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            context.startForegroundService(intent)
        } else {
            context.startService(intent)
        }
        _uiState.update { it.copy(isAppCaptureActive = true) }
        updateInputs()
    }

    fun stopAppCapture() {
        val context = getApplication<Application>()
        context.startService(
            Intent(context, InternalCaptureService::class.java).apply {
                action = InternalCaptureService.ACTION_STOP
            }
        )
        _uiState.update { it.copy(isAppCaptureActive = false) }
        updateInputs()
    }

    // === Output Management ===

    private fun initLocalOutput() {
        val localOutput = OutputTarget(
            id = "local_speaker",
            name = android.os.Build.MODEL ?: "Altavoz del movil",
            deviceType = DeviceType.PHONE,
            channel = "Altavoz interno",
            isActive = true,
            volume = 0.75f,
            connectionState = DeviceState.CONNECTED,
            isLocalDevice = true
        )
        _uiState.update { it.copy(outputs = listOf(localOutput)) }
    }

    fun addLocalOutput() {
        if (_uiState.value.outputs.any { it.isLocalDevice }) return
        initLocalOutput()
        updateInputs()
    }

    fun addBluetoothOutput(address: String) {
        val btDevice = _uiState.value.bluetoothDevices.find { it.address == address } ?: return
        val output = OutputTarget(
            id = "bt_$address",
            name = btDevice.name,
            deviceType = DeviceType.BLUETOOTH_SPEAKER,
            channel = "A2DP",
            isActive = true,
            volume = 0.7f,
            connectionState = if (btDevice.isConnected) DeviceState.CONNECTED else DeviceState.CONNECTING,
            bluetoothAddress = address
        )
        _uiState.update { it.copy(outputs = it.outputs + output) }
        if (btDevice.isConnected) {
            btAudioManager?.setStreamVolume(output.volume)
        }
        updateInputs()
    }

    fun removeOutput(id: String) {
        stopTestSignal(id)
        _uiState.update { it.copy(outputs = it.outputs.filterNot { o -> o.id == id }) }
        updateInputs()
    }

    /**
     * Add a generic output of any type straight from the routing map,
     * so every node of the flow becomes actionable.
     */
    fun addOutput(name: String, deviceType: DeviceType) {
        if (deviceType == DeviceType.PHONE) {
            addLocalOutput()
            return
        }
        val output = OutputTarget(
            id = "out_${System.currentTimeMillis()}",
            name = name.ifBlank { deviceType.label },
            deviceType = deviceType,
            channel = "Main LR",
            isActive = true,
            volume = 0.75f
        )
        _uiState.update { it.copy(outputs = it.outputs + output) }
        updateInputs()
    }

    fun setOutputVolume(id: String, volume: Float) {
        val vol = volume.coerceIn(0f, 1f)
        val output = _uiState.value.outputs.find { it.id == id }
        if (output != null && (output.isLocalDevice || output.connectionState == DeviceState.CONNECTED)) {
            btAudioManager?.setStreamVolume(vol)
        }
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { o ->
                if (o.id == id) o.copy(volume = vol) else o
            })
        }
    }

    fun toggleOutputMute(id: String) {
        val output = _uiState.value.outputs.find { it.id == id } ?: return
        if (output.isMuted) {
            // Pre-flight: the agent verifies the output will actually sound
            val advice = engineerAgent.preFlightUnmute(output)
            if (advice != null) {
                _uiState.update { it.copy(agentAdvices = (listOf(advice) + it.agentAdvices).take(6)) }
                if (advice.severity == AgentSeverity.BLOCK) return
            }
        } else if (id in _uiState.value.testingOutputIds) {
            // Silencing an output stops its test signal immediately
            stopTestSignal(id)
        }
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { o ->
                if (o.id == id) o.copy(isMuted = !o.isMuted) else o
            })
        }
    }

    fun toggleOutputSolo(id: String) {
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { o ->
                if (o.id == id) o.copy(isSolo = !o.isSolo) else o
            })
        }
    }

    // === Bluetooth Management ===

    fun startBluetoothScan() {
        btAudioManager?.startScan()
    }

    fun stopBluetoothScan() {
        btAudioManager?.stopScan()
    }

    fun pairBluetoothDevice(address: String) {
        btAudioManager?.pairDevice(address)
    }

    fun refreshAudioOutputs() {
        val outputs = btAudioManager?.getAudioOutputDevices() ?: emptyList()
        _uiState.update { it.copy(audioOutputDevices = outputs) }
    }

    // === Musician Mode ===

    fun setMusicianMode(enabled: Boolean) {
        _uiState.update { it.copy(musicianState = it.musicianState.copy(isActive = enabled)) }
        if (enabled) {
            setAppMode(AppMode.MUSICIAN)
            startMusicianTracking()
        } else {
            setAppMode(AppMode.CONTROLLER)
            stopMusicianTracking()
        }
    }

    fun moreMe() {
        _uiState.update { state ->
            val newVol = (state.musicianState.personalVolume + 0.05f).coerceIn(0f, 1f)
            val newGain = state.musicianState.personalGainDb + 1.5f
            state.copy(musicianState = state.musicianState.copy(
                personalVolume = newVol,
                personalGainDb = newGain
            ))
        }
        btAudioManager?.setStreamVolume(_uiState.value.musicianState.personalVolume)
    }

    fun lessMe() {
        _uiState.update { state ->
            val newVol = (state.musicianState.personalVolume - 0.05f).coerceIn(0f, 1f)
            val newGain = state.musicianState.personalGainDb - 1.5f
            state.copy(musicianState = state.musicianState.copy(
                personalVolume = newVol,
                personalGainDb = newGain
            ))
        }
        btAudioManager?.setStreamVolume(_uiState.value.musicianState.personalVolume)
    }

    fun setPersonalVolume(volume: Float) {
        val vol = volume.coerceIn(0f, 1f)
        _uiState.update { it.copy(musicianState = it.musicianState.copy(personalVolume = vol)) }
        btAudioManager?.setStreamVolume(vol)
    }

    fun startMusicianTracking() {
        musicianTrackingJob?.cancel()
        musicianTrackingJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(5000)
                val info = locationProvider?.getCurrentLocation() ?: continue
                val distance = 5f
                val recommendedDelay = (distance / info.speedOfSound) * 1000f
                val isOverLimit = _uiState.value.currentSpl > _uiState.value.musicianState.safeSplLimit
                _uiState.update {
                    it.copy(
                        musicianState = it.musicianState.copy(
                            latitude = info.latitude,
                            longitude = info.longitude,
                            altitude = info.altitude,
                            locationLabel = info.label,
                            hasGpsFix = info.hasFix,
                            distanceToPaM = distance,
                            recommendedDelayMs = recommendedDelay,
                            isOverLimit = isOverLimit
                        )
                    )
                }
            }
        }
    }

    private fun stopMusicianTracking() {
        musicianTrackingJob?.cancel()
        musicianTrackingJob = null
    }

    // === Engineer Agent ===

    fun setAgentMode(mode: AgentMode) {
        _uiState.update { it.copy(agentMode = mode) }
        if (mode == AgentMode.OFF) {
            _uiState.update { it.copy(agentAdvices = emptyList()) }
        } else {
            refreshAgentAdvices()
        }
    }

    fun consultAgent() = refreshAgentAdvices()

    fun clearAgentAdvices() {
        _uiState.update { it.copy(agentAdvices = emptyList()) }
    }

    private fun refreshAgentAdvices() {
        val s = _uiState.value
        val snapshot = EngineerAgent.Snapshot(
            isRunning = s.isRunning,
            currentSpl = s.currentSpl,
            safeSplLimit = s.musicianState.safeSplLimit,
            targetSpl = s.config.targetSpl,
            rt60Ms = s.rt60Ms,
            outputs = s.outputs,
            currentDelayMs = s.config.audioDelayMs,
            recommendedDelayMs = s.distanceMeasure.delayMs ?: s.geoInfo.recommendedDelayMs,
            panIsCentered = s.panMatrix.isCentered
        )
        val advices = engineerAgent.advise(snapshot, s.agentMode)
        _uiState.update { it.copy(agentAdvices = advices) }
    }

    // === Sweep Modifiers (afectan a todas las acciones) ===

    fun setFastSweep(active: Boolean) {
        _uiState.update { it.copy(isFastSweep = active, isFineSweep = if (active) false else it.isFineSweep) }
    }

    fun setFineSweep(active: Boolean) {
        _uiState.update { it.copy(isFineSweep = active, isFastSweep = if (active) false else it.isFastSweep) }
    }

    // === Spatial D-Pad ===

    /**
     * Move the element in the bidimensional plane.
     * [dxSteps]/[dySteps] are -1, 0 or 1; the sweep factor scales the step.
     */
    fun nudgeSpatial(dxSteps: Int, dySteps: Int) {
        val step = 0.1f * _uiState.value.sweepFactor
        val pos = _uiState.value.spatialPosition
        updateSpatialPosition(
            pos.copy(
                x = (pos.x + dxSteps * step).coerceIn(-1f, 1f),
                y = (pos.y + dySteps * step).coerceIn(-1f, 1f)
            )
        )
    }

    // === Pan Matrix (L/Mid/R/Lados auto-corregidos) ===

    fun adjustPanZone(zone: PanZone, up: Boolean) {
        val delta = 0.05f * _uiState.value.sweepFactor * if (up) 1f else -1f
        _uiState.update { it.copy(panMatrix = it.panMatrix.adjust(zone, delta)) }
    }

    fun resetPan() {
        _uiState.update { it.copy(panMatrix = PanMatrix.Centered) }
    }

    // === Distance Measurement (emisor → receptor con GPS) ===

    /**
     * Capture the current GPS point: first tap = emitter,
     * second tap = receiver / check point.
     */
    fun captureDistancePoint() {
        viewModelScope.launch(Dispatchers.IO) {
            val info = locationProvider?.getCurrentLocation() ?: return@launch
            if (!info.hasFix) return@launch
            val point = GpsPoint(info.latitude, info.longitude, info.altitude, info.label)
            _uiState.update { state ->
                val dm = state.distanceMeasure
                when (dm.step) {
                    DistanceStep.EMITTER -> state.copy(
                        distanceMeasure = dm.copy(emitter = point, step = DistanceStep.RECEIVER)
                    )
                    DistanceStep.RECEIVER -> state.copy(
                        distanceMeasure = dm.copy(
                            receiver = point,
                            step = DistanceStep.DONE,
                            speedOfSound = info.speedOfSound
                        )
                    )
                    DistanceStep.DONE -> state.copy(
                        distanceMeasure = DistanceMeasurement(
                            emitter = point,
                            step = DistanceStep.EMITTER,
                            speedOfSound = info.speedOfSound
                        )
                    )
                }
            }
        }
    }

    fun resetDistanceMeasure() {
        _uiState.update { it.copy(distanceMeasure = DistanceMeasurement()) }
    }

    /**
     * Adjust the measured distance with decimal precision.
     * Coarse (scale button held) moves 0.5 m; fine moves 0.01 m.
     */
    fun adjustMeasuredDistance(coarse: Boolean, up: Boolean) {
        val step = if (coarse) 0.5f else 0.01f
        _uiState.update { s ->
            val base = s.distanceMeasure.effectiveDistanceM ?: 0f
            val adjusted = (base + if (up) step else -step).coerceAtLeast(0f)
            s.copy(distanceMeasure = s.distanceMeasure.copy(manualDistanceM = adjusted))
        }
    }

    /**
     * Apply GPS-derived automation: delay (d / speedOfSound) and gain
     * compensation with decimals across PA / console / BT outputs.
     */
    fun applyDistanceAutomation() {
        val dm = _uiState.value.distanceMeasure
        val delay = dm.delayMs ?: return
        updateConfig { it.copy(audioDelayMs = delay) }
        _uiState.update { state ->
            state.copy(
                audioDelayMs = delay,
                splCompensation = state.splCompensation.copy(
                    delayAdjustMs = delay,
                    gainAdjustDb = state.splCompensation.gainAdjustDb + (dm.gainCompensationDb ?: 0f)
                ),
                outputs = state.outputs.map { o ->
                    if (o.deviceType == DeviceType.PA_SYSTEM ||
                        o.deviceType == DeviceType.CONSOLE ||
                        o.deviceType == DeviceType.BLUETOOTH_SPEAKER
                    ) o.copy(delayMs = delay) else o
                }
            )
        }
        if (_uiState.value.agentMode != AgentMode.OFF) refreshAgentAdvices()
    }

    // === Output Master Controls (Centro de Control) ===

    /** Set the same volume on every output at once. */
    fun setMasterVolume(volume: Float) {
        val vol = volume.coerceIn(0f, 1f)
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { it.copy(volume = vol) })
        }
        val audible = _uiState.value.outputs.any { it.isLocalDevice || it.connectionState == DeviceState.CONNECTED }
        if (audible) btAudioManager?.setStreamVolume(vol)
    }

    fun muteAllOutputs() {
        stopTestSignal()
        _uiState.update { it.copy(outputs = it.outputs.map { o -> o.copy(isMuted = true) }) }
    }

    /**
     * Unmute all outputs — the agent pre-checks each one:
     * anything that would not sound (disconnected) stays muted and reports why.
     */
    fun unmuteAllOutputs() {
        val current = _uiState.value.outputs
        val blockedIds = current.filter { o ->
            val advice = engineerAgent.preFlightUnmute(o)
            advice != null && advice.severity == AgentSeverity.BLOCK
        }.map { it.id }.toSet()
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { o ->
                if (o.id !in blockedIds) o.copy(isMuted = false) else o
            })
        }
        if (_uiState.value.agentMode != AgentMode.OFF || blockedIds.isNotEmpty()) refreshAgentAdvices()
    }

    fun setOutputGainDb(id: String, gainDb: Float) {
        val maxGain = _uiState.value.config.maxGainDb
        val clamped = gainDb.coerceIn(-maxGain, maxGain)
        val output = _uiState.value.outputs.find { it.id == id }
        if (output != null && _uiState.value.agentMode != AgentMode.OFF) {
            val advice = engineerAgent.preFlightGainChange(
                output, clamped, _uiState.value.currentSpl, _uiState.value.musicianState.safeSplLimit
            )
            if (advice != null) {
                _uiState.update { it.copy(agentAdvices = (listOf(advice) + it.agentAdvices).take(6)) }
            }
        }
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { if (it.id == id) it.copy(gainDb = clamped) else it })
        }
    }

    /** Fine delay set: quantized to 0.01 ms so cm-level alignment is possible. */
    fun setOutputDelayMs(id: String, delayMs: Float) {
        val clamped = (delayMs.coerceIn(0f, 2000f) * 100f).roundToInt() / 100f
        _uiState.update { state ->
            state.copy(outputs = state.outputs.map { if (it.id == id) it.copy(delayMs = clamped) else it })
        }
    }

    /** Nudge one output's delay by [deltaMs] (fine steps of 0.01 ms). */
    fun nudgeOutputDelay(id: String, deltaMs: Float) {
        val output = _uiState.value.outputs.find { it.id == id } ?: return
        setOutputDelayMs(id, output.delayMs + deltaMs)
    }

    // === Test Signal (Comprobar que suena) ===

    /**
     * Play a short test signal through the given output, honouring its
     * volume, gain, delay and mute state. Multiroute: each output sounds on
     * its own physical device, so the phone speaker and a Bluetooth speaker
     * can play different signals at the same time.
     */
    fun playTestSignal(outputId: String, type: TestSignalType) {
        val output = _uiState.value.outputs.find { it.id == outputId } ?: return
        if (output.isMuted) {
            _uiState.update {
                it.copy(
                    agentAdvices = listOf(
                        AgentAdvice(
                            severity = AgentSeverity.WARN,
                            title = "Salida silenciada",
                            message = "${output.name} está en mute: no sonará hasta que la actives.",
                            suggestion = "Actívala primero y vuelve a comprobar."
                        )
                    ) + it.agentAdvices.take(5)
                )
            }
            return
        }
        if (output.isLocalDevice || output.connectionState == DeviceState.CONNECTED) {
            btAudioManager?.setStreamVolume(output.volume)
        }
        val started = testSignalPlayer.play(
            outputId = outputId,
            type = type,
            volume = output.volume,
            gainDb = output.gainDb,
            delayMs = output.delayMs,
            isMuted = output.isMuted,
            device = resolveOutputDevice(output.deviceType)
        ) {
            _uiState.update { it.copy(testingOutputIds = it.testingOutputIds - outputId) }
        }
        if (started) {
            _uiState.update { it.copy(testingOutputIds = it.testingOutputIds + outputId) }
        }
    }

    /**
     * Stop test signals. Without [outputId] stops every output at once;
     * with it, only that output's signal stops.
     */
    fun stopTestSignal(outputId: String? = null) {
        if (outputId == null) {
            testSignalPlayer.stopAll()
            _uiState.update { it.copy(testingOutputIds = emptySet()) }
        } else {
            testSignalPlayer.stop(outputId)
            _uiState.update { it.copy(testingOutputIds = it.testingOutputIds - outputId) }
        }
    }

    /**
     * Resolve the physical output device for a logical output type, so each
     * test signal is routed to its own speaker (built-in, BT, USB or wired).
     */
    private fun resolveOutputDevice(type: DeviceType): AudioDeviceInfo? {
        val audioManager = getApplication<Application>()
            .getSystemService(Context.AUDIO_SERVICE) as? AudioManager
            ?: return null
        val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        return when (type) {
            DeviceType.PHONE, DeviceType.TABLET ->
                devices.firstOrNull { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER }
            DeviceType.BLUETOOTH_SPEAKER ->
                devices.firstOrNull { it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP }
            DeviceType.USB_AUDIO ->
                devices.firstOrNull {
                    it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                        it.type == AudioDeviceInfo.TYPE_USB_HEADSET
                }
            DeviceType.IN_EARS, DeviceType.MONITOR ->
                devices.firstOrNull {
                    it.type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
                        it.type == AudioDeviceInfo.TYPE_WIRED_HEADSET
                }
            else -> null
        }
    }

    // === Focus (Concert) Mode ===

    /** Whether the user granted Do-Not-Disturb access to the app. */
    fun hasFocusAccess(): Boolean = focusModeManager?.hasPolicyAccess() ?: false

    fun toggleFocusMode() {
        val manager = focusModeManager ?: return
        val target = !_uiState.value.focusModeActive
        if (target) {
            val applied = manager.applyFocus(true)
            _uiState.update { it.copy(focusModeActive = applied) }
        } else {
            manager.applyFocus(false)
            _uiState.update { it.copy(focusModeActive = false) }
        }
    }

    // === Walkie-Talkie ===

    /** Choose which peers of the workflow can hear the operator. */
    fun toggleWalkieTarget(peerId: String) {
        _uiState.update { state ->
            val targets = if (peerId in state.walkieTargets) {
                state.walkieTargets - peerId
            } else {
                state.walkieTargets + peerId
            }
            state.copy(walkieTargets = targets)
        }
    }

    fun setWalkieActive(active: Boolean) {
        val wm = walkieManager ?: return
        if (active) {
            val started = wm.start()
            _uiState.update { it.copy(walkieActive = started) }
            if (!started) {
                val advice = AgentAdvice(
                    severity = AgentSeverity.WARN,
                    title = "Walkie sin micrófono",
                    message = "No se puede hablar sin permiso de micrófono.",
                    suggestion = "Concede el permiso desde el banner de permisos pendientes."
                )
                _uiState.update { it.copy(agentAdvices = (listOf(advice) + it.agentAdvices).take(6)) }
            }
        } else {
            wm.stop()
            _uiState.update { it.copy(walkieActive = false) }
        }
    }

    // === Engine Control ===

    fun startEngine() {
        val context = getApplication<Application>()

        // The engine is dead without the mic — fail fast with a visible reason
        val hasMicPermission = ContextCompat.checkSelfPermission(
            context, android.Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
        if (!hasMicPermission) {
            _uiState.update {
                it.copy(
                    isRunning = false,
                    engineError = "Concede el permiso de micrófono para iniciar el análisis"
                )
            }
            return
        }

        _uiState.update { it.copy(engineError = null) }
        AudioAnalysisService.engine = engine
        val intent = Intent(context, AudioAnalysisService::class.java).apply {
            action = AudioAnalysisService.ACTION_START
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            context.startForegroundService(intent)
        } else {
            context.startService(intent)
        }

        val started = engine?.start() ?: false
        Log.d("AudioEngineVM", "startEngine: started=$started")
        if (started) {
            syncBandsFromEngine()
            engine?.setSupportBands(_uiState.value.supportBandsEq, _uiState.value.supportBandsCheck)
            engine?.setAutoHelpEnabled(_uiState.value.autoHelpActive)
            engine?.setNormalSweepEnabled(_uiState.value.normalSweepActive)
            engine?.setCheckSweepEnabled(_uiState.value.workConfig.autoCheck.enabled)
            _uiState.update { it.copy(isRunning = true) }
            startNotificationUpdates()
        } else {
            // engineError was already set by the engine's callback; make sure
            // the foreground service is not left running with a dead engine
            context.startService(
                Intent(context, AudioAnalysisService::class.java).apply {
                    action = AudioAnalysisService.ACTION_STOP
                }
            )
        }
    }

    fun stopEngine() {
        if (!_uiState.value.isRunning) return
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
                noiseCaptureProgress = 0f,
                splHistoryMeasured = emptyList(),
                splHistoryCorrected = emptyList(),
                sweepHelp = null,
                sweepNormal = null,
                sweepCheck = null
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
        syncBandsFromEngine()
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
    /**
     * Global audio delay in 0.01 ms steps. Does not restart the engine —
     * the delay only affects the test signal player and the UI readouts,
     * so dragging the slider stays smooth.
     */
    fun setAudioDelayMs(delayMs: Float) {
        val rounded = (delayMs * 100f).roundToInt() / 100f
        _uiState.update { it.copy(config = it.config.copy(audioDelayMs = rounded), audioDelayMs = rounded) }
    }

    /** Nudge the global delay by [deltaMs] (fine steps of 0.01 ms). */
    fun nudgeAudioDelay(deltaMs: Float) = setAudioDelayMs(_uiState.value.config.audioDelayMs + deltaMs)
    fun setNoiseSubtractionEnabled(enabled: Boolean) = updateConfig {
        it.copy(noiseSubtractionEnabled = enabled)
    }.also {
        _uiState.update { state -> state.copy(noiseSubtractionEnabled = enabled) }
    }

    // === Scenario Presets ===

    fun applyScenarioPreset(preset: ScenarioPreset) {
        updateConfig { cfg ->
            cfg.copy(
                targetSpl = preset.targetSpl,
                maxGainDb = preset.maxGainDb,
                bandCount = preset.bandCount,
                smoothingFactor = preset.smoothingFactor,
                noiseSubtractionEnabled = preset.noiseSubtractionEnabled,
                analysisInterval = preset.analysisInterval,
                audioDelayMs = preset.recommendedDelayMs,
                scenarioPreset = preset.id
            )
        }
        _uiState.update {
            it.copy(
                audioDelayMs = preset.recommendedDelayMs,
                scenarioPreset = preset.id
            )
        }
        rt60Estimator = Rt60Estimator(
            bandCount = 8,
            historySize = 64,
            sampleIntervalMs = preset.analysisInterval.ms
        )
    }

    // === Geolocation Auto-Adjust ===

    fun setGeoAutoAdjust(enabled: Boolean) {
        _uiState.update { it.copy(geoAutoAdjust = enabled) }
        updateConfig { it.copy(geoAutoAdjust = enabled) }
        if (enabled) {
            refreshGeoLocation()
        }
    }

    fun refreshGeoLocation() {
        geoJob?.cancel()
        geoJob = viewModelScope.launch(Dispatchers.IO) {
            val info = locationProvider?.getCurrentLocation() ?: return@launch
            _uiState.update {
                it.copy(
                    geoInfo = info,
                    geoAdjustmentApplied = info.altitudeCorrectionDb
                )
            }
            if (_uiState.value.geoAutoAdjust) {
                updateConfig { cfg ->
                    cfg.copy(
                        targetSpl = info.recommendedSpl,
                        audioDelayMs = info.recommendedDelayMs
                    )
                }
                _uiState.update {
                    it.copy(audioDelayMs = info.recommendedDelayMs)
                }
            }
        }
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

    /** Recompute the per-channel band lists from the engine bands + trims. */
    private fun publishChannelBands() {
        val state = _uiState.value
        val maxGain = state.config.maxGainDb
        val base = state.bands

        fun channelBands(offsets: List<Float>): List<EqBand> = base.mapIndexed { i, band ->
            val offset = offsets.getOrNull(i) ?: 0f
            band.copy(gainDb = (band.gainDb + offset).coerceIn(-maxGain, maxGain))
        }

        _uiState.update {
            it.copy(bandsL = channelBands(eqOffsetsL), bandsR = channelBands(eqOffsetsR))
        }
    }

    /**
     * Mirror the engine's current bands into the UI state and channel lists,
     * so the EQ faders are visible (flat) even before the first analysis frame.
     */
    private fun syncBandsFromEngine() {
        val engineBands = engine?.getBands().orEmpty()
        if (engineBands.isEmpty()) return
        _uiState.update { it.copy(bands = engineBands) }
        publishChannelBands()
    }

    private fun setOffset(list: MutableList<Float>, index: Int, value: Float) {
        while (list.size <= index) list.add(0f)
        list[index] = value
    }

    /**
     * Manual EQ edit for one channel. When linked the trim applies to both
     * channels; when free, only the edited channel moves. The trim sits on
     * top of the engine's automatic correction.
     */
    fun setEqBandGain(channel: EqChannel, index: Int, gainDb: Float) {
        val state = _uiState.value
        val maxGain = state.config.maxGainDb
        val clamped = gainDb.coerceIn(-maxGain, maxGain)
        val engineGain = state.bands.getOrNull(index)?.gainDb ?: 0f
        val offset = clamped - engineGain
        if (state.eqLinked || channel == EqChannel.LEFT) setOffset(eqOffsetsL, index, offset)
        if (state.eqLinked || channel == EqChannel.RIGHT) setOffset(eqOffsetsR, index, offset)
        publishChannelBands()
    }

    /** Legacy single-list edit: behaves like a linked edit. */
    fun setBandGain(index: Int, gainDb: Float) {
        setEqBandGain(EqChannel.LEFT, index, gainDb)
    }

    fun setEqLinked(linked: Boolean) {
        _uiState.update { it.copy(eqLinked = linked) }
    }

    fun toggleEqLink() {
        _uiState.update { it.copy(eqLinked = !it.eqLinked) }
    }

    fun setEqChannel(channel: EqChannel) {
        _uiState.update { it.copy(eqChannel = channel) }
    }

    fun resetBands() {
        engine?.resetBands()
        eqOffsetsL.clear()
        eqOffsetsR.clear()
        _uiState.update { state ->
            val zeroed = state.bands.map { it.copy(gainDb = 0f, targetGainDb = 0f) }
            state.copy(
                bands = zeroed,
                bandsL = zeroed,
                bandsR = zeroed
            )
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
        persistProfiles()
    }

    fun loadProfile(profile: RoomProfile) {
        eqOffsetsL.clear()
        eqOffsetsR.clear()
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
                bandsL = bands,
                bandsR = bands,
                activeProfile = profile
            )
        }
        persistProfiles()
    }

    /** Delete a saved profile from the list and from disk. */
    fun deleteProfile(profile: RoomProfile) {
        _uiState.update {
            it.copy(
                savedProfiles = it.savedProfiles.filterNot { p ->
                    p.name == profile.name && p.createdAt == profile.createdAt
                },
                activeProfile = if (it.activeProfile == profile) null else it.activeProfile
            )
        }
        persistProfiles()
    }

    /** Serialize a profile to JSON so it can be shared with other devices. */
    fun exportProfileJson(profile: RoomProfile): String? =
        profileStore?.exportProfile(profile)

    /** Import a profile from a shared JSON payload; true when valid. */
    fun importProfileJson(raw: String): Boolean {
        val store = profileStore ?: return false
        val profile = store.importProfile(raw) ?: return false
        _uiState.update { it.copy(savedProfiles = it.savedProfiles + profile) }
        persistProfiles()
        return true
    }

    private fun persistProfiles() {
        val s = _uiState.value
        profileStore?.saveProfiles(s.savedProfiles)
        profileStore?.saveActiveProfileName(s.activeProfile?.name)
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
        AudioAnalysisService.engine = null
        AudioAnalysisService.onStopRequested = null
        testSignalPlayer.stopAll()
        consoleManager?.disconnect()
        consoleManager = null
        meshManager?.stop()
        meshManager = null
        usbAudioSource?.disconnect()
        usbAudioSource = null
        geoJob?.cancel()
        autoCheckJob?.cancel()
        musicianTrackingJob?.cancel()
        walkieManager?.stop()
        walkieManager = null
        focusModeManager = null
        btAudioManager?.cleanup()
        btAudioManager = null
        locationProvider = null
        rt60Estimator = null
    }
}
