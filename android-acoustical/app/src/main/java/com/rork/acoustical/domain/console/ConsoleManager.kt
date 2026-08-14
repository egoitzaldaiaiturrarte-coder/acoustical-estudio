package com.rork.acoustical.domain.console

import android.util.Log
import com.rork.acoustical.domain.model.EqBand
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Manages the connection to a professional audio console and synchronizes
 * EQ corrections computed by the audio engine in real-time.
 *
 * Responsibilities:
 * - Connect/disconnect via OSC to X32, M32, A&H, Yamaha TF, or generic OSC consoles
 * - Push computed EQ band gains to the console at configurable intervals
 * - Pull current EQ state from the console for display
 * - Track sync statistics (commands sent, latency, errors)
 * - Support multiple phones coordinating via mesh network
 */
class ConsoleManager {

    companion object {
        private const val TAG = "ConsoleManager"
    }

    private var oscClient: OscClient? = null
    private var config: ConsoleConfig = ConsoleConfig.Default
    private var scope: CoroutineScope? = null
    private var syncJob: Job? = null

    @Volatile
    var connectionState: ConsoleConnectionState = ConsoleConnectionState.DISCONNECTED
        private set

    var onConnectionStateChanged: ((ConsoleConnectionState) -> Unit)? = null
    var onSyncStatus: ((ConsoleSyncStatus) -> Unit)? = null
    var onConsoleEqReceived: ((List<EqBand>) -> Unit)? = null

    private var commandsSent: Int = 0
    private var commandsFailed: Int = 0
    private var lastSyncMs: Long = 0L
    private var lastError: String? = null

    // Cached band corrections from the audio engine
    @Volatile
    private var pendingCorrections: List<EqBand> = emptyList()

    /**
     * Connect to the console using the given configuration.
     */
    fun connect(consoleConfig: ConsoleConfig) {
        config = consoleConfig
        updateState(ConsoleConnectionState.CONNECTING)

        when (consoleConfig.protocol) {
            ConsoleProtocol.OSC -> connectOsc(consoleConfig)
            ConsoleProtocol.USB_AUDIO -> {
                // USB audio handled by UsbAudioSource; console manager just tracks state
                updateState(ConsoleConnectionState.CONNECTED)
            }
            ConsoleProtocol.MIDI -> {
                updateState(ConsoleConnectionState.NOT_SUPPORTED)
                Log.w(TAG, "MIDI protocol not yet implemented")
            }
            ConsoleProtocol.MANUAL -> {
                updateState(ConsoleConnectionState.CONNECTED)
                Log.i(TAG, "Manual mode — no console connection, analysis only")
            }
        }
    }

    private fun connectOsc(consoleConfig: ConsoleConfig) {
        try {
            oscClient = OscClient().also { client ->
                client.connect(
                    ip = consoleConfig.ipAddress,
                    port = consoleConfig.oscPort,
                    listenPort = consoleConfig.listenPort
                )

                client.onMessageReceived = { message ->
                    handleIncomingOsc(message)
                }

                if (client.isConnected) {
                    updateState(ConsoleConnectionState.CONNECTED)
                    Log.i(TAG, "Connected to ${consoleConfig.type.label} at ${consoleConfig.ipAddress}:${consoleConfig.oscPort}")

                    // Subscribe to EQ parameter changes if pulling from console
                    if (consoleConfig.pullGainsFromConsole) {
                        subscribeToEqParameters()
                    }

                    // Start auto-sync loop if pushing corrections
                    if (consoleConfig.pushGainsToConsole || consoleConfig.autoCorrectEnabled) {
                        startSyncLoop()
                    }
                } else {
                    updateState(ConsoleConnectionState.ERROR)
                    lastError = "No se pudo conectar a ${consoleConfig.ipAddress}:${consoleConfig.oscPort}"
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "OSC connection failed", e)
            updateState(ConsoleConnectionState.ERROR)
            lastError = e.message ?: "Error desconocido"
        }
    }

    /**
     * Disconnect from the console.
     */
    fun disconnect() {
        syncJob?.cancel()
        syncJob = null
        oscClient?.disconnect()
        oscClient = null
        commandsSent = 0
        commandsFailed = 0
        lastError = null
        updateState(ConsoleConnectionState.DISCONNECTED)
    }

    /**
     * Update the current band corrections from the audio engine.
     * These will be pushed to the console on the next sync cycle.
     */
    fun updateCorrections(bands: List<EqBand>) {
        pendingCorrections = bands
    }

    /**
     * Manually push all EQ band gains to the console immediately.
     */
    fun pushCorrectionsNow(bands: List<EqBand>): Boolean {
        if (connectionState != ConsoleConnectionState.CONNECTED) return false
        pendingCorrections = bands
        return syncCycle()
    }

    /**
     * Reset all EQ bands to 0 dB on the console.
     */
    fun resetConsoleEq(): Boolean {
        if (connectionState != ConsoleConnectionState.CONNECTED) return false
        val client = oscClient ?: return false
        val bandCount = ConsoleProfiles.eqBandCountFor(config.type)
        var allOk = true

        for (i in 0 until bandCount) {
            val gainAddr = ConsoleProfiles.eqGainAddress(config.type, config.channel, i)
            val gainValue = ConsoleProfiles.gainToConsoleValue(config.type, 0f)
            if (!client.sendFloat(gainAddr, gainValue)) allOk = false
        }

        return allOk
    }

    fun is_connected(): Boolean = connectionState == ConsoleConnectionState.CONNECTED

    fun getConfig(): ConsoleConfig = config

    // --- Internal sync loop ---

    private fun startSyncLoop() {
        scope?.cancel()
        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val interval = config.correctionIntervalMs

        syncJob = scope?.launch {
            while (isActive && connectionState == ConsoleConnectionState.CONNECTED) {
                if (config.autoCorrectEnabled || config.pushGainsToConsole) {
                    syncCycle()
                }
                delay(interval)
            }
        }
    }

    /**
     * Push pending corrections to the console.
     * Returns true if all commands succeeded.
     */
    private fun syncCycle(): Boolean {
        val client = oscClient ?: return false
        val corrections = pendingCorrections
        if (corrections.isEmpty()) return true

        val startMs = System.currentTimeMillis()
        val maxBands = minOf(corrections.size, ConsoleProfiles.eqBandCountFor(config.type))
        var sent = 0
        var failed = 0

        for (i in 0 until maxBands) {
            val band = corrections[i]
            val clampedGain = band.gainDb.coerceIn(-config.maxCorrectionDb, config.maxCorrectionDb)

            // Send gain
            val gainAddr = ConsoleProfiles.eqGainAddress(config.type, config.channel, i)
            val gainValue = ConsoleProfiles.gainToConsoleValue(config.type, clampedGain)

            if (client.sendFloat(gainAddr, gainValue)) {
                sent++
            } else {
                failed++
            }

            // Send frequency (only if it differs from default)
            if (band.centerFreq > 0) {
                val freqAddr = ConsoleProfiles.eqFrequencyAddress(config.type, config.channel, i)
                val freqValue = ConsoleProfiles.freqToConsoleValue(config.type, band.centerFreq)
                client.sendFloat(freqAddr, freqValue)
            }

            // Send Q
            val qAddr = ConsoleProfiles.eqQAddress(config.type, config.channel, i)
            val qValue = ConsoleProfiles.qToConsoleValue(config.type, band.q)
            client.sendFloat(qAddr, qValue)
        }

        commandsSent += sent
        commandsFailed += failed
        lastSyncMs = System.currentTimeMillis()
        val latency = (lastSyncMs - startMs).toFloat()

        onSyncStatus?.invoke(
            ConsoleSyncStatus(
                state = connectionState,
                lastSyncMs = lastSyncMs,
                commandsSent = commandsSent,
                commandsFailed = commandsFailed,
                latencyMs = latency,
                errorMessage = lastError
            )
        )

        return failed == 0
    }

    private fun subscribeToEqParameters() {
        val client = oscClient ?: return
        val bandCount = ConsoleProfiles.eqBandCountFor(config.type)

        for (i in 0 until bandCount) {
            val gainAddr = ConsoleProfiles.eqGainAddress(config.type, config.channel, i)
            client.subscribe(gainAddr)
            val freqAddr = ConsoleProfiles.eqFrequencyAddress(config.type, config.channel, i)
            client.subscribe(freqAddr)
        }
    }

    /**
     * Handle incoming OSC messages from the console (parameter feedback).
     */
    private fun handleIncomingOsc(message: OscMessage) {
        Log.d(TAG, "OSC recv: ${message.address} args=${message.args.size}")

        // Parse EQ parameter changes from console
        // Address format: /ch/XX/eq/N/gain, /ch/XX/eq/N/freq, /ch/XX/eq/N/q
        val parts = message.address.split("/")
        if (parts.size >= 5 && parts[3] == "eq") {
            // This is an EQ parameter response
            // In a full implementation, we'd parse and update the band list
            // For now, log it
            Log.d(TAG, "Console EQ feedback: ${message.address} = ${message.args}")
        }
    }

    private fun updateState(state: ConsoleConnectionState) {
        connectionState = state
        onConnectionStateChanged?.invoke(state)
    }
}
