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
    // internal (no private) para que src/test pueda hacer round-trips del
    // codec en JVM sin socket: la API pública del cliente es send*/subscribe.

    internal fun encodeOscMessage(address: String, args: List<OscArgument>): ByteArray {
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

    /**
     * Serializa [s] como string OSC 1.0: bytes UTF-8 + terminador NUL,
     * rellenado a múltiplos de 4.
     *
     * El NUL cuenta DENTRO del último bloque: una string de longitud múltiplo
     * de 4 (o la vacía) necesita un bloque extra solo con el terminador.
     * La versión anterior (a) no emitía NUL en esos casos y el parser del
     * receptor se des-sincronizaba leyendo más bytes de los reales, y (b)
     * usaba US_ASCII, que corrompía cualquier carácter no-ASCII (el spec
     * usa UTF-8: "calibración", emoji, etc.).
     */
    private fun paddedString(s: String): ByteArray {
        val bytes = s.toByteArray(Charsets.UTF_8)
        val withTerminator = bytes.size + 1
        val padding = (4 - withTerminator % 4) % 4
        return bytes + byteArrayOf(0) + ByteArray(padding)
    }

    // --- OSC Decoding ---
    // internal (no private) para que src/test pueda hacer round-trips del
    // codec en JVM sin socket.

    internal fun decodeOscMessage(data: ByteArray): OscMessage? {
        try {
            var offset = 0

            // Read address
            val (address, addrLen) = readPaddedString(data, offset)
            offset += addrLen

            if (offset >= data.size) return OscMessage(address, emptyList())

            // Read type tag
            val (typeTag, tagLen) = readPaddedString(data, offset)
            offset += tagLen

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
                        val (str, strLen) = readPaddedString(data, offset)
                        args.add(OscArgument.StringArg(str))
                        offset += strLen
                    }
                }
            }

            return OscMessage(address, args)
        } catch (e: Exception) {
            Log.w(TAG, "OSC decode failed", e)
            return null
        }
    }

    /**
     * Lee una string OSC 1.0 desde [offset] y devuelve el par
     * (texto decodificado UTF-8, nº de bytes que ocupó en el stream).
     *
     * El terminador NUL vive DENTRO del último bloque de 4 bytes: una string
     * de N bytes ocupa redondeado(N+1, a múltiplo de 4), así que las de
     * longitud múltiplo de 4 llevan su NUL en un bloque extra. El límite del
     * bloque se calcula desde la posición del NUL (el payload ya delimita la
     * longitud; no se asume relleno "hasta donde el parser quiera"):
     *  - con NUL: el bloque acaba en el siguiente múltiplo de 4 (la
     *    alineación de bloques es global en el mensaje, offset siempre
     *    múltiplo de 4);
     *  - sin NUL (emisor malformado/truncado): la string llega al fin del
     *    buffer y el parseo se detiene ahí en vez de desbocarse.
     *
     * Nota: un NUL EMBEFIDO a mitad de la string (no el terminador) la
     * trunca al primer cero — limitación del spec OSC 1.0 —, pero el parseo
     * se limita a bloques de 4 alineados: no hay excepción ni desbocado más
     * allá del buffer, y los mensajes bien formados no se ven afectados.
     */
    private fun readPaddedString(data: ByteArray, offset: Int): Pair<String, Int> {
        var i = offset
        var terminator = -1
        while (i < data.size) {
            if (data[i].toInt() == 0) {
                terminator = i
                break
            }
            i++
        }
        if (terminator < 0) {
            // El buffer acabó antes del NUL (truncado): leer hasta el fin.
            val text = String(data, offset, data.size - offset, Charsets.UTF_8)
            return text to (data.size - offset)
        }
        val text = String(data, offset, terminator - offset, Charsets.UTF_8)
        val blockEnd = minOf(((terminator + 1) + 3) / 4 * 4, data.size)
        return text to (blockEnd - offset)
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
