package com.rork.acoustical.domain.console

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Manages USB Audio Class devices connected to the Android device in USB host mode.
 *
 * This enables direct digital audio capture from professional consoles and audio
 * interfaces that expose a USB Audio Class interface (e.g., X32 USB card, 
 * Focusrite Scarlett, Behringer UMC series, etc.).
 *
 * The captured audio replaces the built-in microphone input, giving a clean
 * digital signal path from the console to AcoustiCal for analysis.
 */
class UsbAudioSource(private val context: Context) {

    companion object {
        private const val TAG = "UsbAudioSource"
        private const val ACTION_USB_PERMISSION = "com.rork.acoustical.USB_PERMISSION"

        // USB Audio Class subclass codes
        private const val AUDIO_CLASS = 1
        private const val AUDIO_CONTROL = 1
        private const val AUDIO_STREAMING = 2
    }

    private val usbManager: UsbManager =
        context.getSystemService(Context.USB_SERVICE) as UsbManager

    private var usbDevice: UsbDevice? = null
    private var usbConnection: UsbDeviceConnection? = null
    private var audioInterface: UsbInterface? = null
    private var permissionReceiver: BroadcastReceiver? = null

    @Volatile
    var isDeviceAttached: Boolean = false
        private set

    @Volatile
    var deviceName: String = ""
        private set

    @Volatile
    var vendorName: String = ""
        private set

    @Volatile
    var productName: String = ""
        private set

    @Volatile
    var supportedSampleRates: List<Int> = listOf(44100, 48000, 96000)
        private set

    @Volatile
    var channelCount: Int = 2
        private set

    var onDeviceAttached: ((UsbDeviceInfo) -> Unit)? = null
    var onDeviceDetached: (() -> Unit)? = null

    data class UsbDeviceInfo(
        val name: String,
        val vendor: String,
        val product: String,
        val sampleRates: List<Int>,
        val channels: Int
    )

    /**
     * Scan for connected USB audio devices.
     */
    fun scanForAudioDevices(): List<UsbDeviceInfo> {
        val devices = mutableListOf<UsbDeviceInfo>()
        for ((_, device) in usbManager.deviceList) {
            if (isAudioDevice(device)) {
                val info = extractDeviceInfo(device)
                devices.add(info)
            }
        }
        return devices
    }

    /**
     * Request USB permission for the given device and connect.
     */
    fun requestConnect(device: UsbDevice) {
        if (usbManager.hasPermission(device)) {
            connectToDevice(device)
        } else {
            registerPermissionReceiver()
            val intent = Intent(ACTION_USB_PERMISSION)
            val flags = PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
            val pendingIntent = PendingIntent.getBroadcast(context, 0, intent, flags)
            usbManager.requestPermission(device, pendingIntent)
        }
    }

    /**
     * Connect to a USB audio device that we have permission for.
     */
    private fun connectToDevice(device: UsbDevice) {
        try {
            // Find the audio streaming interface
            val audioIf = findAudioInterface(device)
            if (audioIf == null) {
                Log.e(TAG, "No audio streaming interface found on ${device.deviceName}")
                return
            }

            audioInterface = audioIf
            usbDevice = device
            deviceName = device.deviceName
            vendorName = device.deviceName
            productName = device.productName ?: "Unknown"

            val connection = usbManager.openDevice(device)
            if (connection == null) {
                Log.e(TAG, "Failed to open USB connection")
                return
            }
            usbConnection = connection
            connection.claimInterface(audioIf, true)

            isDeviceAttached = true
            Log.i(TAG, "USB audio device connected: $productName ($vendorName)")

            // Try to detect capabilities
            detectCapabilities(device, connection, audioIf)

            onDeviceAttached?.invoke(
                UsbDeviceInfo(
                    name = deviceName,
                    vendor = vendorName,
                    product = productName,
                    sampleRates = supportedSampleRates,
                    channels = channelCount
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to connect to USB device", e)
        }
    }

    /**
     * Disconnect from the USB device.
     */
    fun disconnect() {
        try {
            audioInterface?.let { usbConnection?.releaseInterface(it) }
            usbConnection?.close()
        } catch (e: Exception) {
            Log.w(TAG, "Error during USB disconnect", e)
        }
        usbConnection = null
        audioInterface = null
        usbDevice = null
        isDeviceAttached = false
        deviceName = ""
        vendorName = ""
        productName = ""
        unregisterPermissionReceiver()
        onDeviceDetached?.invoke()
    }

    /**
     * Check if the connected device supports a given sample rate.
     */
    fun supportsSampleRate(hz: Int): Boolean {
        return supportedSampleRates.contains(hz)
    }

    /**
     * Create an AudioRecord configured for USB audio input.
     * On Android 21+, AudioRecord can use USB audio devices directly via
     * the system's automatic USB audio routing when the device is connected.
     * The source should be MediaRecorder.AudioSource.MIC or UNPROCESSED.
     */
    fun createAudioRecord(sampleRate: Int, channelConfig: Int, format: Int, bufferSize: Int): AudioRecord? {
        if (!isDeviceAttached) return null

        // When a USB audio device is connected and has permission, Android's
        // AudioManager routes its input to the AudioRecord with source MIC.
        // The actual device selection happens at the system level.
        return try {
            AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                channelConfig,
                format,
                bufferSize
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create AudioRecord for USB device", e)
            null
        }
    }

    // --- Internal helpers ---

    private fun isAudioDevice(device: UsbDevice): Boolean {
        // Check all interfaces for USB Audio Class
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == UsbConstants.USB_CLASS_AUDIO) {
                return true
            }
        }
        return false
    }

    private fun findAudioInterface(device: UsbDevice): UsbInterface? {
        // Find the audio streaming interface
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == UsbConstants.USB_CLASS_AUDIO &&
                iface.interfaceSubclass == AUDIO_STREAMING) {
                return iface
            }
        }
        // Fallback: any audio class interface
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == UsbConstants.USB_CLASS_AUDIO) {
                return iface
            }
        }
        return null
    }

    private fun extractDeviceInfo(device: UsbDevice): UsbDeviceInfo {
        val vendor = device.deviceName
        val product = device.deviceName
        return UsbDeviceInfo(
            name = device.deviceName,
            vendor = vendor,
            product = product,
            sampleRates = listOf(44100, 48000, 96000),
            channels = 2
        )
    }

    private fun detectCapabilities(
        device: UsbDevice,
        connection: UsbDeviceConnection,
        iface: UsbInterface
    ) {
        // Query USB Audio Class descriptors for sample rates and channels
        // This is a simplified implementation — a full UAC parser would read
        // the class-specific descriptors (UAC1: CS_INTERFACE, UAC2: UAC2_CS_INTERFACE)
        try {
            // Read the raw descriptors
            val descriptorBytes = ByteArray(1024)
            val rawDescriptor = connection.rawDescriptors

            if (rawDescriptor != null && rawDescriptor.isNotEmpty()) {
                // Parse UAC descriptors to find supported sample rates
                val rates = parseSupportedSampleRates(rawDescriptor)
                if (rates.isNotEmpty()) {
                    supportedSampleRates = rates
                }
            }

            // Channel count from the interface's endpoint count
            // (simplified: assume stereo for most USB audio interfaces)
            channelCount = 2
        } catch (e: Exception) {
            Log.w(TAG, "Could not detect full USB audio capabilities, using defaults", e)
            supportedSampleRates = listOf(44100, 48000, 96000)
            channelCount = 2
        }
    }

    /**
     * Parse supported sample rates from USB raw descriptors.
     * Looks for UAC format descriptor subtype 0x02 (FORMAT_TYPE_I).
     */
    private fun parseSupportedSampleRates(rawDescriptor: ByteArray): List<Int> {
        val rates = mutableListOf<Int>()
        try {
            var i = 0
            while (i < rawDescriptor.size - 7) {
                val bLength = rawDescriptor[i].toInt() and 0xFF
                if (bLength < 2 || i + bLength > rawDescriptor.size) {
                    i++
                    continue
                }

                val bDescriptorType = rawDescriptor[i + 1].toInt() and 0xFF
                val bDescriptorSubtype = rawDescriptor[i + 2].toInt() and 0xFF

                // CS_INTERFACE = 0x24, FORMAT_TYPE_I = 0x02
                if (bDescriptorType == 0x24 && bDescriptorSubtype == 0x02) {
                    // At offset 5: bSubFrameSize, bBitResolution
                    // At offset 7: bSamFreqType (number of sample rates)
                    if (i + 8 < rawDescriptor.size) {
                        val numFreqs = rawDescriptor[i + 7].toInt() and 0xFF
                        if (numFreqs == 0) {
                            // Continuous range — read min and max 3-byte values
                            if (i + 11 < rawDescriptor.size) {
                                val minFreq = read24Bit(rawDescriptor, i + 8)
                                val maxFreq = read24Bit(rawDescriptor, i + 11)
                                rates.add(minFreq)
                                if (maxFreq != minFreq) rates.add(maxFreq)
                            }
                        } else {
                            for (j in 0 until numFreqs) {
                                val offset = i + 8 + j * 3
                                if (offset + 2 < rawDescriptor.size) {
                                    rates.add(read24Bit(rawDescriptor, offset))
                                }
                            }
                        }
                    }
                    if (rates.isNotEmpty()) break
                }
                i += bLength
            }
        } catch (e: Exception) {
            Log.w(TAG, "Error parsing sample rates", e)
        }
        return rates.distinct().sorted()
    }

    private fun read24Bit(data: ByteArray, offset: Int): Int {
        val b0 = data[offset].toInt() and 0xFF
        val b1 = data[offset + 1].toInt() and 0xFF
        val b2 = data[offset + 2].toInt() and 0xFF
        return b0 or (b1 shl 8) or (b2 shl 16)
    }

    private fun registerPermissionReceiver() {
        if (permissionReceiver != null) return
        permissionReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (ACTION_USB_PERMISSION == intent.action) {
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    @Suppress("DEPRECATION")
                    val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
                    if (granted && device != null) {
                        connectToDevice(device)
                    } else {
                        Log.w(TAG, "USB permission denied")
                    }
                    unregisterPermissionReceiver()
                }
            }
        }
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        context.registerReceiver(permissionReceiver, filter)
    }

    private fun unregisterPermissionReceiver() {
        permissionReceiver?.let {
            try {
                context.unregisterReceiver(it)
            } catch (e: Exception) {
                // Already unregistered
            }
        }
        permissionReceiver = null
    }
}
