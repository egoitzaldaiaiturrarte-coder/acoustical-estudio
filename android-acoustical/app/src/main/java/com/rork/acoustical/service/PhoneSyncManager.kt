package com.rork.acoustical.service

import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import com.rork.acoustical.BuildConfig
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.net.URL
import java.security.MessageDigest

/**
 * Servidor de sincronización y actualización por USB (puerto 41041).
 *
 * Solo escucha en 127.0.0.1: la única forma de llegar hasta él es el
 * reenvío de puertos de adb (cable USB), nunca la red Wi-Fi del móvil.
 *
 * El PC (Acoustical Estudio para Windows) lo usa para tres cosas:
 *  1. Sincronizar ajustes: JSON simple {"type":"push"/"pull","payload":...}
 *     o HTTP POST/GET /sync.
 *  2. Saber si lleva una versión nueva de Windows: GET /manifest.
 *  3. Descargarse el instalador de esa versión por el propio cable: GET /payload.
 *
 * El paquete de Windows se descarga una vez desde Ajustes > PC/Windows y
 * queda verificado (SHA-256) en el móvil; así el PC se actualiza solo,
 * incluso sin internet.
 */
class PhoneSyncManager private constructor(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private val payloadDir = File(context.filesDir, "windows")

    private val _serverRunning = MutableStateFlow(false)
    val serverRunning: StateFlow<Boolean> = _serverRunning.asStateFlow()

    private val _payloadReady = MutableStateFlow(false)
    val payloadReady: StateFlow<Boolean> = _payloadReady.asStateFlow()

    private val _downloading = MutableStateFlow(false)
    val downloading: StateFlow<Boolean> = _downloading.asStateFlow()

    private val _status = MutableStateFlow("")
    val status: StateFlow<String> = _status.asStateFlow()

    @Volatile private var serverThread: Thread? = null

    init {
        _payloadReady.value = payloadFile() != null
    }

    /** Arranca el servidor (una sola vez); es daemon y muere con la app. */
    fun start() {
        if (serverThread?.isAlive == true) return
        val thread = Thread({ acceptLoop() }, "acoustical-sync-server")
        thread.isDaemon = true
        serverThread = thread
        thread.start()
    }

    // === Servidor ===

    private fun acceptLoop() {
        try {
            // Solo loopback: accesible únicamente por adb forward (USB) o apps locales
            ServerSocket(PORT, 4, InetAddress.getByName("127.0.0.1")).use { server ->
                _serverRunning.value = true
                while (!Thread.currentThread().isInterrupted) {
                    val client = try {
                        server.accept()
                    } catch (e: Exception) {
                        break
                    }
                    try {
                        handle(client)
                    } catch (e: Exception) {
                        // una conexión fallida nunca debe tumbar el servidor
                    } finally {
                        runCatching { client.close() }
                    }
                }
            }
        } catch (e: Exception) {
            _status.value = "Servidor USB no disponible: ${e.message ?: "error"}"
        } finally {
            _serverRunning.value = false
        }
    }

    private fun handle(socket: Socket) {
        socket.soTimeout = READ_TIMEOUT_MS
        val input = socket.getInputStream().buffered()
        val first = ByteArray(8192)
        val firstLen = readFirstChunk(input, first)
        if (firstLen <= 0) return
        val head = String(first, 0, minOf(firstLen, 16), Charsets.US_ASCII)
        if (head.startsWith("GET ") || head.startsWith("POST ")) {
            handleHttp(socket, input, first, firstLen)
        } else {
            handleLegacyJson(socket, input, first, firstLen)
        }
    }

    /** Lee al menos un byte y luego drena lo que llegue antes del timeout. */
    private fun readFirstChunk(input: InputStream, buffer: ByteArray): Int {
        var total = 0
        try {
            while (total == 0) {
                val n = input.read(buffer, 0, buffer.size)
                if (n < 0) return 0
                total += n
            }
            while (total < buffer.size) {
                val n = input.read(buffer, total, buffer.size - total)
                if (n < 0) break
                total += n
            }
        } catch (e: SocketTimeoutException) {
            // sin más datos en el timeout: con lo recibido basta
        }
        return total
    }

    // === HTTP (manifest / sync / payload) ===

    private fun handleHttp(socket: Socket, input: InputStream, first: ByteArray, firstLen: Int) {
        var bytes = first.copyOf(firstLen)
        var headerEnd = indexOfHeaderEnd(bytes)
        while (headerEnd < 0) {
            val chunk = ByteArray(8192)
            val n = try {
                input.read(chunk)
            } catch (e: SocketTimeoutException) {
                return
            }
            if (n < 0) return
            bytes += chunk.copyOf(n)
            if (bytes.size > MAX_HEADER_BYTES) return
            headerEnd = indexOfHeaderEnd(bytes)
        }
        val headerText = String(bytes, 0, headerEnd, Charsets.ISO_8859_1)
        val lines = headerText.split("\r\n")
        val parts = (lines.firstOrNull() ?: "").split(" ")
        if (parts.size < 2) return
        val method = parts[0].uppercase()
        val path = parts[1].substringBefore('?')

        var body = bytes.copyOfRange(headerEnd + 4, bytes.size)
        if (method == "POST") {
            val contentLength = lines.firstOrNull { it.startsWith("Content-Length:", ignoreCase = true) }
                ?.substringAfter(':')?.trim()?.toIntOrNull() ?: body.size
            while (body.size < contentLength) {
                val chunk = ByteArray(minOf(65536, contentLength - body.size))
                val n = try {
                    input.read(chunk)
                } catch (e: SocketTimeoutException) {
                    break
                }
                if (n < 0) break
                body += chunk.copyOf(n)
            }
        }

        when {
            method == "GET" && path == "/manifest" -> respondJson(socket, manifestJson())
            method == "GET" && path == "/sync" -> respondJson(socket, currentSyncJson())
            method == "POST" && path == "/sync" -> {
                storeSyncPayload(String(body, Charsets.UTF_8), fromPc = true)
                respondJson(socket, currentSyncJson())
            }
            method == "GET" && path == "/payload" -> respondPayload(socket)
            else -> respondError(socket)
        }
    }

    private fun indexOfHeaderEnd(bytes: ByteArray): Int {
        for (i in 0 until bytes.size - 3) {
            if (bytes[i] == 13.toByte() && bytes[i + 1] == 10.toByte() &&
                bytes[i + 2] == 13.toByte() && bytes[i + 3] == 10.toByte()
            ) return i
        }
        return -1
    }

    // === Protocolo JSON simple (compatibilidad con el cliente Windows) ===

    private fun handleLegacyJson(socket: Socket, input: InputStream, first: ByteArray, firstLen: Int) {
        val acc = ByteArrayOutputStream()
        acc.write(first, 0, firstLen)
        try {
            val buffer = ByteArray(8192)
            while (acc.size() < MAX_MESSAGE_BYTES) {
                // El cliente no cierra la escritura: el timeout marca el fin del mensaje
                val n = input.read(buffer)
                if (n < 0) break
                acc.write(buffer, 0, n)
            }
        } catch (e: SocketTimeoutException) {
            // mensaje completo
        }
        val obj = runCatching {
            Json.parseToJsonElement(String(acc.toByteArray(), Charsets.UTF_8).trim()).jsonObject
        }.getOrNull() ?: return

        when (obj.textContent("type")) {
            "push" -> {
                (obj["payload"] as? JsonObject)?.let { storeSyncPayload(it.toString(), fromPc = true) }
                writeRawJson(socket, currentSyncJson())
            }
            "pull" -> writeRawJson(socket, currentSyncJson())
        }
    }

    private fun JsonObject.textContent(key: String): String? =
        (this[key] as? JsonPrimitive)?.takeIf { it !is JsonNull }?.content

    // === Respuestas ===

    private fun respondJson(socket: Socket, json: String) {
        writeHttp(socket, "200 OK", "application/json", json.toByteArray(Charsets.UTF_8))
    }

    private fun respondError(socket: Socket) {
        writeHttp(socket, "404 Not Found", "application/json", "{\"ok\":false}".toByteArray())
    }

    private fun writeHttp(socket: Socket, status: String, contentType: String, body: ByteArray) {
        val header = ("HTTP/1.0 $status\r\n" +
            "Content-Type: $contentType\r\n" +
            "Content-Length: ${body.size}\r\n" +
            "Connection: close\r\n\r\n").toByteArray(Charsets.ISO_8859_1)
        val out = socket.getOutputStream()
        out.write(header)
        out.write(body)
        out.flush()
    }

    /** Respuesta JSON pura, sin cabeceras HTTP (protocolo antiguo). */
    private fun writeRawJson(socket: Socket, json: String) {
        val out = socket.getOutputStream()
        out.write(json.toByteArray(Charsets.UTF_8))
        out.flush()
    }

    private fun respondPayload(socket: Socket) {
        val file = payloadFile()
        if (file == null) {
            respondError(socket)
            return
        }
        val header = ("HTTP/1.0 200 OK\r\n" +
            "Content-Type: application/octet-stream\r\n" +
            "Content-Length: ${file.length()}\r\n" +
            "Connection: close\r\n\r\n").toByteArray(Charsets.ISO_8859_1)
        val out = socket.getOutputStream()
        out.write(header)
        out.flush()
        file.inputStream().use { input -> input.copyTo(out, 65536) }
        out.flush()
    }

    // === Estado sincronizado ===

    private fun currentSyncJson(): String {
        val stored = prefs.getString(KEY_SYNC_STATE, null) ?: "{}"
        return "{\"type\":\"sync\",\"ok\":true,\"payload\":$stored}"
    }

    private fun storeSyncPayload(text: String, fromPc: Boolean) {
        val valid = runCatching { Json.parseToJsonElement(text).jsonObject }.getOrNull() ?: return
        prefs.edit().putString(KEY_SYNC_STATE, valid.toString()).apply()
        _status.value = if (fromPc) "Ajustes recibidos del PC" else "Ajustes enviados al PC"
    }

    // === Manifest para el PC ===

    private fun manifestJson(): String {
        val file = payloadFile()
        return buildJsonObject {
            put("type", "manifest")
            put("ok", true)
            putJsonObject("app") {
                put("name", "AcoustiCal")
                put("versionCode", BuildConfig.VERSION_CODE)
                put("versionName", BuildConfig.VERSION_NAME)
            }
            putJsonObject("windows") {
                val version = prefs.getString(KEY_VERSION, null)
                val sha = prefs.getString(KEY_SHA256, null)
                put("version", version?.let { JsonPrimitive(it) } ?: JsonNull)
                put("sha256", sha?.let { JsonPrimitive(it) } ?: JsonNull)
                put("size", if (sha != null && file != null) file.length() else 0L)
                put("hasPayload", sha != null && file != null)
                put("url", prefs.getString(KEY_URL, null)?.let { JsonPrimitive(it) } ?: JsonNull)
            }
        }.toString()
    }

    // === Paquete de Windows ===

    fun windowsPayloadUrl(): String = prefs.getString(KEY_URL, "") ?: ""

    fun installedPayloadVersion(): String? = prefs.getString(KEY_VERSION, null)

    /**
     * Descarga el instalador de Windows y lo deja verificado (SHA-256) en el
     * almacenamiento del móvil para servirlo al PC por USB. El enlace debe
     * incluir la versión (p. ej. .../AcousticalEstudioSetup-1.1.0.exe).
     */
    fun downloadWindowsPayload(urlInput: String) {
        val url = urlInput.trim()
        if (url.isEmpty()) {
            _status.value = "Pega primero el enlace del instalador de Windows"
            return
        }
        val version = VERSION_REGEX.find(url)?.value
        if (version == null) {
            _status.value = "El enlace debe incluir la versión (p. ej. Setup-1.1.0.exe)"
            return
        }
        if (_downloading.value) return
        _downloading.value = true
        _status.value = "Descargando paquete de Windows $version…"
        Thread({
            try {
                payloadDir.mkdirs()
                val tmp = File(payloadDir, "setup.tmp")
                val conn = URL(url).openConnection() as HttpURLConnection
                conn.connectTimeout = 15000
                conn.readTimeout = 30000
                conn.instanceFollowRedirects = true
                conn.connect()
                if (conn.responseCode !in 200..299) {
                    throw IllegalStateException("HTTP ${conn.responseCode}")
                }
                val digest = MessageDigest.getInstance("SHA-256")
                var size = 0L
                conn.inputStream.use { input ->
                    FileOutputStream(tmp).use { out ->
                        val buffer = ByteArray(65536)
                        while (true) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            out.write(buffer, 0, n)
                            digest.update(buffer, 0, n)
                            size += n
                            if (size > MAX_PAYLOAD_BYTES) throw IllegalStateException("Paquete demasiado grande")
                        }
                    }
                }
                val sha = digest.digest().joinToString("") { "%02x".format(it) }
                val dest = File(payloadDir, "AcousticalEstudioSetup.exe")
                if (dest.exists()) dest.delete()
                if (!tmp.renameTo(dest)) throw IllegalStateException("No se pudo guardar el paquete")
                prefs.edit()
                    .putString(KEY_VERSION, version)
                    .putString(KEY_SHA256, sha)
                    .putString(KEY_URL, url)
                    .apply()
                _payloadReady.value = true
                _status.value = "Listo: al conectar el PC por USB se instalará la versión $version"
            } catch (e: Exception) {
                _status.value = "Descarga fallida: ${e.message ?: "error"}"
            } finally {
                _downloading.value = false
            }
        }, "acoustical-payload-download").start()
    }

    fun clearWindowsPayload() {
        payloadFile()?.delete()
        prefs.edit().remove(KEY_VERSION).remove(KEY_SHA256).remove(KEY_URL).apply()
        _payloadReady.value = false
        _status.value = "Paquete de Windows eliminado"
    }

    private fun payloadFile(): File? {
        if (prefs.getString(KEY_SHA256, null) == null) return null
        val file = File(payloadDir, "AcousticalEstudioSetup.exe")
        return if (file.isFile) file else null
    }

    companion object {
        const val PORT = 41041

        private const val PREFS = "acoustical_phone_sync"
        private const val KEY_SYNC_STATE = "sync_state"
        private const val KEY_VERSION = "windows_version"
        private const val KEY_SHA256 = "windows_sha256"
        private const val KEY_URL = "windows_url"

        private const val READ_TIMEOUT_MS = 600
        private const val MAX_HEADER_BYTES = 64 * 1024
        private const val MAX_MESSAGE_BYTES = 1024 * 1024
        private const val MAX_PAYLOAD_BYTES = 512L * 1024 * 1024
        private val VERSION_REGEX = Regex("""(\d+\.\d+(?:\.\d+)*)""")

        @Volatile private var instance: PhoneSyncManager? = null

        fun get(context: Context): PhoneSyncManager =
            instance ?: synchronized(this) {
                instance ?: PhoneSyncManager(context.applicationContext).also { instance = it }
            }
    }
}
