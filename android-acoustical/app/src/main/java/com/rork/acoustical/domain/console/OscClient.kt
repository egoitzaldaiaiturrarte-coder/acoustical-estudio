package com.rork.acoustical.domain.console

import android.util.Log
import java.io.ByteArrayOutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Minimal OSC (Open Sound Control) client over UDP.
 *
 * Supports the subset of OSC needed for pro audio consoles:
 * - Send float values to EQ band gain/frequency/Q addresses
 * - Send commands like /ch/01/eq/1/gain
 * - Receive OSC responses for parameter feedback
 *
 * Compatible with Behringer X32, Midas M32, Allen & Heath, and any
 * OSC-enabled console following the standard OSC 1.0 spec.
 */
class OscClient {

    companion object {
        private const val TAG = "OscClient"
        private const val BUFFER_SIZE = 1024
    }

    private var socket: DatagramSocket? = null
    private var targetAddress: InetAddress? = null
    private var targetPort: Int = 10023
    private var listenSocket: DatagramSocket? = null
    private var listenThread: Thread? = null

    @Volatile
    var isConnected: Boolean = false
        private set

    var onMessageReceived: ((OscMessage) -> Unit)? = null

    /**
     * Connect to a console at the given IP and port.
     */
    fun connect(ip: String, port: Int, listenPort: Int = 0) {
        try {
            socket = DatagramSocket()
            targetAddress = InetAddress.getByName(ip)
            targetPort = port

            if (listenPort > 0) {
                listenSocket = DatagramSocket(listenPort)
                startListening()
            }

            isConnected = true
            Log.i(TAG, "OSC connected to $ip:$port (listen: $listenPort)")
        } catch (e: Exception) {
            Log.e(TAG, "OSC connect failed", e)
            isConnected = false
        }
    }

    /**
     * Send an OSC message with a float argument.
     * Example: send("/ch/01/eq/1/gain", 1.5f)
     */
    fun sendFloat(address: String, value: Float): Boolean {
        val data = encodeOscMessage(address, listOf(OscArgument.FloatArg(value)))
        return sendRaw(data)
    }

    /**
     * Send an OSC message with an integer argument.
     */
    fun sendInt(address: String, value: Int): Boolean {
        val data = encodeOscMessage(address, listOf(OscArgument.IntArg(value)))
        return sendRaw(data)
    }

    /**
     * Send an OSC message with a string argument.
     */
    fun sendString(address: String, value: String): Boolean {
        val data = encodeOscMessage(address, listOf(OscArgument.StringArg(value)))
        return sendRaw(data)
    }

    /**
     * Send a multi-argument OSC message.
     */
    fun send(address: String, args: List<OscArgument>): Boolean {
        val data = encodeOscMessage(address, args)
        return sendRaw(data)
    }

    /**
     * Subscribe to console parameter changes (X32 style).
     * Sends /subscribe with the node path.
     */
    fun subscribe(nodePath: String): Boolean {
        return send("/subscribe", listOf(
            OscArgument.StringArg(nodePath),
            OscArgument.IntArg(1)
        ))
    }

    fun disconnect() {
        listenThread?.interrupt()
        listenThread = null
        listenSocket?.close()
        listenSocket = null
        socket?.close()
        socket = null
        targetAddress = null
        isConnected = false
    }

    // --- Internal ---

    private fun sendRaw(data: ByteArray): Boolean {
        if (!isConnected) return false
        val addr = targetAddress ?: return false
        val sock = socket ?: return false
        return try {
            val packet = DatagramPacket(data, data.size, addr, targetPort)
            sock.send(packet)
            true
        } catch (e: Exception) {
            Log.e(TAG, "OSC send failed", e)
            false
        }
    }

    private fun startListening() {
        listenThread = Thread {
            val buffer = ByteArray(BUFFER_SIZE)
            while (!Thread.currentThread().isInterrupted) {
                try {
                    val packet = DatagramPacket(buffer, buffer.size)
                    listenSocket?.receive(packet)
                    val data = packet.data.copyOfRange(0, packet.length)
                    val message = decodeOscMessage(data)
                    if (message != null) {
                        onMessageReceived?.invoke(message)
                    }
                } catch (e: Exception) {
                    if (!Thread.currentThread().isInterrupted) {
                        Log.w(TAG, "OSC listen error", e)
                    }
                }
            }
        }.also { it.start() }
    }

    // --- OSC Encoding ---

    private fun encodeOscMessage(address: String, args: List<OscArgument>): ByteArray {
        val out = ByteArrayOutputStream()

        // Address pattern
        out.write(paddedString(address))

        // Type tag string
        val typeTag = "," + args.joinToString("") { it.typeChar }
        out.write(paddedString(typeTag))

        // Arguments
        for (arg in args) {
            when (arg) {
                is OscArgument.FloatArg -> {
                    val bb = ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN)
                    bb.putFloat(arg.value)
                    out.write(bb.array())
                }
                is OscArgument.IntArg -> {
                    val bb = ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN)
                    bb.putInt(arg.value)
                    out.write(bb.array())
                }
                is OscArgument.StringArg -> {
                    out.write(paddedString(arg.value))
                }
            }
        }

        return out.toByteArray()
    }

    private fun paddedString(s: String): ByteArray {
        val bytes = s.toByteArray(Charsets.US_ASCII)
        val padding = (4 - (bytes.size % 4)) % 4
        return bytes + ByteArray(padding)
    }

    // --- OSC Decoding ---

    private fun decodeOscMessage(data: ByteArray): OscMessage? {
        try {
            var offset = 0

            // Read address
            val address = readPaddedString(data, offset)
            offset += paddedLength(address)

            if (offset >= data.size) return OscMessage(address, emptyList())

            // Read type tag
            val typeTag = readPaddedString(data, offset)
            offset += paddedLength(typeTag)

            val types = typeTag.removePrefix(",")
            val args = mutableListOf<OscArgument>()

            for (type in types) {
                when (type) {
                    'f' -> {
                        if (offset + 4 > data.size) break
                        val value = ByteBuffer.wrap(data, offset, 4).order(ByteOrder.BIG_ENDIAN).float
                        args.add(OscArgument.FloatArg(value))
                        offset += 4
                    }
                    'i' -> {
                        if (offset + 4 > data.size) break
                        val value = ByteBuffer.wrap(data, offset, 4).order(ByteOrder.BIG_ENDIAN).int
                        args.add(OscArgument.IntArg(value))
                        offset += 4
                    }
                    's' -> {
                        val str = readPaddedString(data, offset)
                        args.add(OscArgument.StringArg(str))
                        offset += paddedLength(str)
                    }
                }
            }

            return OscMessage(address, args)
        } catch (e: Exception) {
            Log.w(TAG, "OSC decode failed", e)
            return null
        }
    }

    private fun readPaddedString(data: ByteArray, offset: Int): String {
        val sb = StringBuilder()
        var i = offset
        while (i < data.size && data[i].toInt() != 0) {
            sb.append(data[i].toInt().toChar())
            i++
        }
        return sb.toString()
    }

    private fun paddedLength(s: String): Int {
        val len = s.toByteArray(Charsets.US_ASCII).size + 1
        return len + ((4 - (len % 4)) % 4)
    }
}

/** A parsed OSC message. */
data class OscMessage(
    val address: String,
    val args: List<OscArgument>
)

/** Sealed hierarchy of OSC argument types. */
sealed class OscArgument {
    abstract val typeChar: String

    data class FloatArg(val value: Float) : OscArgument() {
        override val typeChar = "f"
    }
    data class IntArg(val value: Int) : OscArgument() {
        override val typeChar = "i"
    }
    data class StringArg(val value: String) : OscArgument() {
        override val typeChar = "s"
    }
}
