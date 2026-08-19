package com.rork.acoustical.domain.audio

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothA2dp
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.media.AudioManager
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Real Bluetooth audio device manager.
 * Handles device discovery, pairing, A2DP connection tracking, and volume control.
 * Gracefully handles environments without Bluetooth (cloud emulator).
 */
class BluetoothAudioManager(private val context: Context) {

    companion object {
        private const val TAG = "BtAudioManager"
    }

    /**
     * Discovered or bonded Bluetooth device info.
     */
    data class BtDevice(
        val name: String,
        val address: String,
        val isBonded: Boolean = false,
        val isConnected: Boolean = false,
        val rssi: Int = 0,
        val isA2dp: Boolean = false
    )

    private val _devices = MutableStateFlow<List<BtDevice>>(emptyList())
    val devices: StateFlow<List<BtDevice>> = _devices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val _isBluetoothEnabled = MutableStateFlow(false)
    val isBluetoothEnabled: StateFlow<Boolean> = _isBluetoothEnabled.asStateFlow()

    private val _isBluetoothAvailable = MutableStateFlow(false)
    val isBluetoothAvailable: StateFlow<Boolean> = _isBluetoothAvailable.asStateFlow()

    private val bluetoothManager: BluetoothManager? =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val adapter: BluetoothAdapter? = bluetoothManager?.adapter
    private val audioManager: AudioManager =
        context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    private var a2dpProxy: BluetoothA2dp? = null
    private val deviceMap = mutableMapOf<String, BtDevice>()
    private var receiverRegistered = false

    private val receiver = object : BroadcastReceiver() {
        @SuppressLint("MissingPermission")
        override fun onReceive(ctx: Context?, intent: Intent?) {
            when (intent?.action) {
                BluetoothDevice.ACTION_FOUND -> {
                    val device = extractDevice(intent)
                    val rssi = intent.getShortExtra(BluetoothDevice.EXTRA_RSSI, Short.MIN_VALUE).toInt()
                    if (device != null) {
                        addFoundDevice(device, rssi)
                    }
                }
                BluetoothAdapter.ACTION_DISCOVERY_STARTED -> {
                    _isScanning.value = true
                }
                BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> {
                    _isScanning.value = false
                }
                BluetoothDevice.ACTION_BOND_STATE_CHANGED -> {
                    val device = extractDevice(intent)
                    if (device != null) {
                        updateBondState(device)
                    }
                }
                BluetoothDevice.ACTION_ACL_CONNECTED -> {
                    val device = extractDevice(intent)
                    if (device != null) {
                        updateConnectionState(device, connected = true)
                    }
                }
                BluetoothDevice.ACTION_ACL_DISCONNECTED -> {
                    val device = extractDevice(intent)
                    if (device != null) {
                        updateConnectionState(device, connected = false)
                    }
                }
                BluetoothAdapter.ACTION_STATE_CHANGED -> {
                    val state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)
                    _isBluetoothEnabled.value = state == BluetoothAdapter.STATE_ON
                    if (state == BluetoothAdapter.STATE_ON) {
                        loadBondedDevices()
                        setupA2dpProxy()
                    }
                }
            }
        }
    }

    init {
        _isBluetoothAvailable.value = adapter != null
        _isBluetoothEnabled.value = adapter?.isEnabled == true

        if (adapter != null) {
            registerReceivers()
            setupA2dpProxy()
            loadBondedDevices()
        } else {
            Log.i(TAG, "Bluetooth not available on this device")
        }
    }

    /**
     * Extract BluetoothDevice from Intent, handling API differences.
     */
    private fun extractDevice(intent: Intent): BluetoothDevice? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        }
    }

    private fun registerReceivers() {
        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_STARTED)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
            addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            addAction(BluetoothDevice.ACTION_ACL_CONNECTED)
            addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED)
            addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
        }
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
            } else {
                context.registerReceiver(receiver, filter)
            }
            receiverRegistered = true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to register BT receiver", e)
        }
    }

    /**
     * Check if BLUETOOTH_SCAN permission is granted (Android 12+).
     */
    fun hasScanPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.BLUETOOTH_SCAN
            ) == PackageManager.PERMISSION_GRANTED
        } else true
    }

    /**
     * Check if BLUETOOTH_CONNECT permission is granted (Android 12+).
     */
    fun hasConnectPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.BLUETOOTH_CONNECT
            ) == PackageManager.PERMISSION_GRANTED
        } else true
    }

    /**
     * Start Bluetooth device discovery.
     * Returns false if Bluetooth is not available, not enabled, or permissions missing.
     */
    @SuppressLint("MissingPermission")
    fun startScan(): Boolean {
        val a = adapter ?: return false
        if (!a.isEnabled) {
            Log.w(TAG, "Bluetooth is disabled")
            return false
        }
        if (!hasScanPermission() || !hasConnectPermission()) {
            Log.w(TAG, "Missing BT permissions")
            return false
        }
        // Cancel any ongoing discovery first
        if (a.isDiscovering) {
            a.cancelDiscovery()
        }
        deviceMap.clear()
        _devices.value = emptyList()
        return a.startDiscovery()
    }

    /**
     * Stop ongoing discovery.
     */
    @SuppressLint("MissingPermission")
    fun stopScan() {
        val a = adapter ?: return
        if (a.isDiscovering) {
            a.cancelDiscovery()
        }
        _isScanning.value = false
    }

    /**
     * Load already-bonded devices from the system.
     */
    @SuppressLint("MissingPermission")
    private fun loadBondedDevices() {
        val a = adapter ?: return
        if (!hasConnectPermission()) return

        val bonded = a.bondedDevices ?: emptySet()
        for (device in bonded) {
            val bt = BtDevice(
                name = device.name ?: "Dispositivo BT",
                address = device.address,
                isBonded = true,
                isConnected = false,
                isA2dp = false
            )
            deviceMap[device.address] = bt
        }
        updateConnectedA2dpDevices()
        publishDevices()
    }

    /**
     * Set up A2DP profile proxy for connection tracking.
     */
    @SuppressLint("MissingPermission")
    private fun setupA2dpProxy() {
        val a = adapter ?: return
        if (!hasConnectPermission()) return
        if (!a.isEnabled) return

        try {
            a.getProfileProxy(context, object : BluetoothProfile.ServiceListener {
                override fun onServiceConnected(profile: Int, proxy: BluetoothProfile) {
                    if (profile == BluetoothProfile.A2DP) {
                        a2dpProxy = proxy as? BluetoothA2dp
                        Log.i(TAG, "A2DP proxy connected")
                        updateConnectedA2dpDevices()
                    }
                }

                override fun onServiceDisconnected(profile: Int) {
                    if (profile == BluetoothProfile.A2DP) {
                        a2dpProxy = null
                        Log.i(TAG, "A2DP proxy disconnected")
                    }
                }
            }, BluetoothProfile.A2DP)
        } catch (e: SecurityException) {
            Log.e(TAG, "Failed to get A2DP proxy", e)
        }
    }

    /**
     * Update connected A2DP device states from the proxy.
     */
    @SuppressLint("MissingPermission")
    private fun updateConnectedA2dpDevices() {
        val proxy = a2dpProxy ?: return
        if (!hasConnectPermission()) return

        try {
            val connected = proxy.connectedDevices
            for (device in connected) {
                val existing = deviceMap[device.address]
                deviceMap[device.address] = (existing ?: BtDevice(
                    name = device.name ?: "Dispositivo BT",
                    address = device.address,
                    isBonded = true
                )).copy(isConnected = true, isA2dp = true)
            }
            publishDevices()
        } catch (e: SecurityException) {
            Log.e(TAG, "Failed to get connected A2DP devices", e)
        }
    }

    /**
     * Add a found device during discovery.
     */
    @SuppressLint("MissingPermission")
    private fun addFoundDevice(device: BluetoothDevice, rssi: Int) {
        val isBonded = try { device.bondState == BluetoothDevice.BOND_BONDED } catch (e: SecurityException) { false }
        val name = try { device.name } catch (e: SecurityException) { null } ?: "Dispositivo BT"

        val bt = BtDevice(
            name = name,
            address = device.address,
            isBonded = isBonded,
            isConnected = false,
            rssi = rssi,
            isA2dp = false
        )
        deviceMap[device.address] = bt
        publishDevices()
    }

    /**
     * Update bond state after pairing changes.
     */
    @SuppressLint("MissingPermission")
    private fun updateBondState(device: BluetoothDevice) {
        val isBonded = try { device.bondState == BluetoothDevice.BOND_BONDED } catch (e: SecurityException) { false }
        val existing = deviceMap[device.address]
        if (existing != null) {
            deviceMap[device.address] = existing.copy(isBonded = isBonded)
        } else {
            val name = try { device.name } catch (e: SecurityException) { null } ?: "Dispositivo BT"
            deviceMap[device.address] = BtDevice(
                name = name,
                address = device.address,
                isBonded = isBonded
            )
        }
        publishDevices()
    }

    /**
     * Update connection state for a device.
     */
    private fun updateConnectionState(device: BluetoothDevice, connected: Boolean) {
        val existing = deviceMap[device.address]
        if (existing != null) {
            deviceMap[device.address] = existing.copy(isConnected = connected)
            publishDevices()
        }
        if (connected) {
            updateConnectedA2dpDevices()
        }
    }

    /**
     * Publish deviceMap to the StateFlow.
     */
    private fun publishDevices() {
        _devices.value = deviceMap.values.toList().sortedByDescending { it.rssi }
    }

    /**
     * Pair (bond) with a Bluetooth device.
     * Returns true if pairing was initiated.
     */
    @SuppressLint("MissingPermission")
    fun pairDevice(address: String): Boolean {
        val a = adapter ?: return false
        if (!hasConnectPermission()) return false

        val device = a.getRemoteDevice(address) ?: return false
        return try {
            device.createBond()
        } catch (e: SecurityException) {
            Log.e(TAG, "Failed to pair device", e)
            false
        }
    }

    /**
     * Check if a specific device is connected via A2DP.
     */
    @SuppressLint("MissingPermission")
    fun isDeviceConnected(address: String): Boolean {
        val proxy = a2dpProxy ?: return false
        if (!hasConnectPermission()) return false
        val a = adapter ?: return false

        return try {
            val device = a.getRemoteDevice(address)
            if (device != null) {
                proxy.getConnectionState(device) == BluetoothA2dp.STATE_CONNECTED
            } else false
        } catch (e: SecurityException) {
            false
        }
    }

    /**
     * Get the connected A2DP device addresses.
     */
    @SuppressLint("MissingPermission")
    fun getConnectedA2dpAddresses(): List<String> {
        val proxy = a2dpProxy ?: return emptyList()
        if (!hasConnectPermission()) return emptyList()
        return try {
            proxy.connectedDevices.map { it.address }
        } catch (e: SecurityException) {
            emptyList()
        }
    }

    /**
     * Set the media stream volume (applies to the currently routed audio output).
     * volume: 0.0 to 1.0
     */
    fun setStreamVolume(volume: Float) {
        val maxVolume = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val targetVolume = (volume * maxVolume).toInt().coerceIn(0, maxVolume)
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, targetVolume, 0)
    }

    /**
     * Get the current media stream volume as a 0..1 float.
     */
    fun getStreamVolume(): Float {
        val maxVolume = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        if (maxVolume == 0) return 0f
        return audioManager.getStreamVolume(AudioManager.STREAM_MUSIC).toFloat() / maxVolume.toFloat()
    }

    /**
     * Get available audio output devices from the system.
     */
    fun getAudioOutputDevices(): List<AudioOutputInfo> {
        val outputs = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        return outputs.map { info ->
            AudioOutputInfo(
                name = info.productName?.toString() ?: "Salida de audio",
                typeLabel = audioDeviceTypeLabel(info.type),
                type = info.type,
                isBluetooth = info.type == android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP
            )
        }
    }

    /**
     * Clean up resources.
     */
    fun cleanup() {
        stopScan()
        if (receiverRegistered) {
            try {
                context.unregisterReceiver(receiver)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to unregister receiver", e)
            }
            receiverRegistered = false
        }
        a2dpProxy = null
    }
}

/**
 * Info about a system audio output device.
 */
data class AudioOutputInfo(
    val name: String,
    val typeLabel: String,
    val type: Int,
    val isBluetooth: Boolean = false
)

private fun audioDeviceTypeLabel(type: Int): String = when (type) {
    android.media.AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "Altavoz del móvil"
    android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "Bluetooth A2DP"
    android.media.AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Auriculares con cable"
    android.media.AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> "Auriculares"
    android.media.AudioDeviceInfo.TYPE_USB_HEADSET -> "Audio USB"
    android.media.AudioDeviceInfo.TYPE_DOCK -> "Dock"
    android.media.AudioDeviceInfo.TYPE_HDMI -> "HDMI"
    android.media.AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> "Auricular"
    else -> "Salida de audio"
}
