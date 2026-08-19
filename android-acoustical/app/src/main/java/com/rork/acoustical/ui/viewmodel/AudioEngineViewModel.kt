package com.rork.acoustical.ui.viewmodel

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rork.acoustical.domain.audio.AudioEngine
import com.rork.acoustical.domain.audio.LocationProvider
import com.rork.acoustical.domain.audio.AudioOutputInfo
import com.rork.acoustical.domain.audio.BluetoothAudioManager
import com.rork.acoustical.domain.audio.Rt60Estimator
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
import com.rork.acoustical.domain.model.AudioConfig
import com.rork.acoustical.domain.model.AutoCheckConfig
import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.BitDepth
import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.domain.model.MusicianState
import com.rork.acoustical.domain.model.OutputTarget
import com.rork.acoustical.domain.model.ProbeQuality
import com.rork.acoustical.domain.model.ReferenceSource
import com.rork.acoustical.domain.model.RoomProfile
import com.rork.acoustical.domain.model.RoutingConnection
import com.rork.acoustical.domain.model.RoutingNode
import com.rork.acoustical.domain.model.RoutingNodeType
import com.rork.acoustical.domain.model.SampleRate
import com.rork.acoustical.domain.model.ScenarioPreset
import com.rork.acoustical.domain.model.SpatialPosition
import com.rork.acoustical.domain.model.SplCalibration
import com.rork.acoustical.domain.model.SplCompensation
import com.rork.acoustical.domain.model.StereoMode
import com.rork.acoustical.domain.model.SpectrumFrame
import com.rork.acoustical.domain.model.WorkConfig
import com.rork.acoustical.domain.model.WorkDevice
import com.rork.acoustical.domain.model.WorkEnvironmentType
import com.rork.acoustical.domain.model.WorkSession
import com.rork.acoustical.domain.model.DeviceConnection
import com.rork.acoustical.domain.model.DeviceRole
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.DeviceType
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
        val routingNodes: List<RoutingNode> = emptyList(),
        val routingConnections: List<RoutingConnection> = emptyList(),
        val outputs: List<OutputTarget> = emptyList(),
        val bluetoothDevices: List<BluetoothAudioManager.BtDevice> = emptyList(),
        val isBtScanning: Boolean = false,
        val isBluetoothEnabled: Boolean = false,
        val isBluetoothAvailable: Boolean = false,
        val audioOutputDevices: List<AudioOutputInfo> = emptyList(),
        val musicianState: MusicianState = MusicianState()
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

    init {
        initRoutingNodes()
        engine = AudioEngine().also { eng ->
            eng.configure(_uiState.value.config)
            eng.onAnalysisUpdate = { result ->
                val rt60 = rt60Estimator
                rt60?.feedFrame(result.measuredSpectrum.magnitudesDb)
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
                        hasNoiseProfile = result.noiseSpectrum != null,
                        rt60Ms = rt60?.currentRt60Ms ?: 0f,
                        musicianState = state.musicianState.copy(
                            isOverLimit = result.spl >= state.musicianState.safeSplLimit
                        )
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

        // Initialize outputs with local device
        initLocalOutput()

        // Refresh system audio outputs
        refreshAudioOutputs()
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
        updateRoutingNodes()
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
        updateRoutingNodes()
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
        updateRoutingNodes()
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
            updateRoutingNodes()
        }
    }

    fun removeDevice(deviceId: String) {
        _uiState.update { state ->
            state.copy(session = state.session.copy(devices = state.session.devices.filterNot { it.id == deviceId }))
        }
        updateRoutingNodes()
    }

    fun toggleDeviceActive(deviceId: String) {
        _uiState.update { state ->
            val updated = state.session.devices.map { d ->
                if (d.id == deviceId) d.copy(isActive = !d.isActive) else d
            }
            state.copy(session = state.session.copy(devices = updated))
        }
        updateRoutingNodes()
    }

    // === Work Configuration ===

    fun setWorkEnvironment(env: WorkEnvironmentType) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(environment = env)) }
        updateRoutingNodes()
    }

    fun setReferenceSource(ref: ReferenceSource) {
        _uiState.update { it.copy(workConfig = it.workConfig.copy(referenceSource = ref)) }
        updateRoutingNodes()
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
                // Run a probe measurement
                val spl = _uiState.value.currentSpl
                val rt60 = _uiState.value.rt60Ms
                _uiState.update { state ->
                    // Update routing nodes with fresh probe data
                    state.copy(
                        routingNodes = state.routingNodes.map { node ->
                            if (node.type == RoutingNodeType.PROCESS) node.copy(spl = spl) else node
                        }
                    )
                }
            }
        }
    }

    private fun stopAutoCheck() {
        autoCheckJob?.cancel()
        autoCheckJob = null
    }

    // === Routing Map ===

    private fun initRoutingNodes() {
        val nodes = listOf(
            RoutingNode("in_mic", "Micrófono", RoutingNodeType.INPUT, true, spl = 0f, subtitle = "Captura local"),
            RoutingNode("in_usb", "USB Audio", RoutingNodeType.INPUT, false, subtitle = "Interface USB"),
            RoutingNode("in_console", "Consola In", RoutingNodeType.INPUT, false, subtitle = "Entrada OSC"),
            RoutingNode("proc_eq", "EQ Dinámico", RoutingNodeType.PROCESS, true, subtitle = "Corrección espectral"),
            RoutingNode("proc_pressure", "Corrector presión", RoutingNodeType.PROCESS, true, subtitle = "Compensación SPL"),
            RoutingNode("proc_noise", "Noise Profiler", RoutingNodeType.PROCESS, true, subtitle = "Sustracción de ruido"),
            RoutingNode("proc_rt60", "RT60", RoutingNodeType.PROCESS, false, subtitle = "Reverberación"),
            RoutingNode("out_console", "Consola", RoutingNodeType.OUTPUT, false, subtitle = "Main LR"),
            RoutingNode("out_bt", "Altavoces BT", RoutingNodeType.OUTPUT, false, subtitle = "Bluetooth"),
            RoutingNode("out_monitor", "Monitores", RoutingNodeType.OUTPUT, true, subtitle = "Salida local"),
            RoutingNode("out_remote", "Móviles remotos", RoutingNodeType.OUTPUT, false, subtitle = "Mesh")
        )
        val connections = listOf(
            RoutingConnection("in_mic", "proc_eq"),
            RoutingConnection("in_usb", "proc_eq"),
            RoutingConnection("in_console", "proc_eq"),
            RoutingConnection("proc_eq", "proc_pressure"),
            RoutingConnection("proc_pressure", "proc_noise"),
            RoutingConnection("proc_noise", "out_console"),
            RoutingConnection("proc_noise", "out_bt"),
            RoutingConnection("proc_noise", "out_monitor"),
            RoutingConnection("proc_noise", "out_remote")
        )
        _uiState.update { it.copy(routingNodes = nodes, routingConnections = connections) }
    }

    private fun updateRoutingNodes() {
        val devices = _uiState.value.session.devices
        val outputs = _uiState.value.outputs
        _uiState.update { state ->
            state.copy(
                routingNodes = state.routingNodes.map { node ->
                    when (node.id) {
                        "in_mic" -> node.copy(isActive = true)
                        "in_usb" -> node.copy(isActive = devices.any { it.type == DeviceType.USB_AUDIO && it.isActive })
                        "in_console" -> node.copy(isActive = devices.any { it.type == DeviceType.CONSOLE && it.isActive })
                        "out_console" -> node.copy(isActive = devices.any { it.type == DeviceType.CONSOLE && it.isActive } || outputs.any { it.deviceType == DeviceType.CONSOLE && it.isActive })
                        "out_bt" -> node.copy(isActive = devices.any { it.type == DeviceType.BLUETOOTH_SPEAKER && it.isActive } || outputs.any { it.deviceType == DeviceType.BLUETOOTH_SPEAKER && it.isActive })
                        "out_monitor" -> node.copy(isActive = outputs.any { it.isLocalDevice && it.isActive })
                        "out_remote" -> node.copy(isActive = devices.any { it.type == DeviceType.PHONE && !it.isThisDevice && it.isActive })
                        else -> node
                    }
                }
            )
        }
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
        updateRoutingNodes()
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
        updateRoutingNodes()
    }

    fun removeOutput(id: String) {
        _uiState.update { it.copy(outputs = it.outputs.filterNot { o -> o.id == id }) }
        updateRoutingNodes()
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
    fun setAudioDelayMs(delayMs: Float) = updateConfig { it.copy(audioDelayMs = delayMs) }
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
        geoJob?.cancel()
        autoCheckJob?.cancel()
        musicianTrackingJob?.cancel()
        btAudioManager?.cleanup()
        btAudioManager = null
        locationProvider = null
        rt60Estimator = null
    }
}
