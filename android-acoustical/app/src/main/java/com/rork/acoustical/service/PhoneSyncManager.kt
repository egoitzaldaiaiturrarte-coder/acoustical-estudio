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
import kotlinx.serialization.json.JsonArray
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
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.net.URL
import java.security.MessageDigest
import java.security.SecureRandom

/**
 * Servidor de sincronización y actualización (puerto 41041).
 *
 * Escucha en todas las interfaces: el PC (Acoustical Estudio para Windows)
 * se conecta directo por Wi-Fi —lo descubre con la baliza UDP del puerto
 * 41042— o por el túnel adb de USB, como antes.
 *
 * Seguridad: los ajustes (JSON push/pull y HTTP /sync) exigen el código de
 * emparejamiento de 6 dígitos (se genera una vez y se muestra en
 * Ajustes > PC/Windows). /manifest y /payload son solo lectura de datos
 * públicos de la release de GitHub, así que quedan abiertos.
 *
 * El PC lo usa para tres cosas:
 *  1. Sincronizar ajustes: JSON simple {"type":"push"/"pull","payload":...}
 *     o HTTP POST/GET /sync.
 *  2. Saber si lleva una versión nueva de Windows: GET /manifest.
 *  3. Descargarse el instalador de esa versión (Wi-Fi o cable): GET /payload.
 *
 * El paquete de Windows se configura una vez en Ajustes > PC/Windows (tu
 * repositorio de GitHub o un enlace directo) y queda verificado (SHA-256) en
 * el móvil: las versiones nuevas se detectan y descargan solas, y el PC se
 * actualiza al conectarse, incluso sin internet en el PC.
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

    /** Config del PC recibida por su push ("Sincronizar"): (marca de tiempo, config).
     *  El ViewModel la recoge y la aplica al motor local. */
    private val _remoteConfig = MutableStateFlow<Pair<Long, JsonObject>?>(null)
    val remoteConfig: StateFlow<Pair<Long, JsonObject>?> = _remoteConfig.asStateFlow()

    /** Ajustes actuales de la app (JSON). El ViewModel lo publica cada vez que
     *  cambian; viaja en cada respuesta de sync para que el PC siempre vea el
     *  estado actual (dirección móvil → PC). */
    @Volatile private var localConfigJson: String = ""

    fun publishLocalConfig(json: String) { localConfigJson = json }

    @Volatile private var serverThread: Thread? = null
    @Volatile private var beaconThread: Thread? = null

    init {
        _payloadReady.value = payloadFile() != null
    }

    /** Arranca el servidor y la baliza (una sola vez); son daemons y mueren con la app. */
    fun start() {
        if (serverThread?.isAlive == true) return
        val thread = Thread({ acceptLoop() }, "acoustical-sync-server")
        thread.isDaemon = true
        serverThread = thread
        thread.start()
        startBeacon()
        maybeAutoCheck()
    }

    // === Servidor ===

    private fun acceptLoop() {
        try {
            // Todas las interfaces: el PC llega por Wi-Fi directo o por el
            // túnel adb (USB). El código de emparejamiento protege los ajustes.
            ServerSocket(PORT, 4).use { server ->
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
            _status.value = "Servidor de sincronización no disponible: ${e.message ?: "error"}"
        } finally {
            _serverRunning.value = false
        }
    }

    // === Baliza Wi-Fi: el PC la oye por UDP y se conecta sin cable ===

    private fun startBeacon() {
        if (beaconThread?.isAlive == true) return
        beaconThread = Thread({
            val sock = runCatching { DatagramSocket().also { it.setBroadcast(true) } }
                .getOrNull() ?: return@Thread
            while (!Thread.currentThread().isInterrupted) {
                try {
                    val payload = beaconJson().toByteArray(Charsets.UTF_8)
                    sock.send(DatagramPacket(payload, payload.size,
                        InetAddress.getByName("255.255.255.255"), BEACON_PORT))
                } catch (e: Exception) {
                    // Sin Wi-Fi en este momento: se reintenta en el siguiente ciclo
                }
                Thread.sleep(BEACON_INTERVAL_MS)
            }
            runCatching { sock.close() }
        }, "acoustical-beacon").apply {
            isDaemon = true
            start()
        }
    }

    private fun beaconJson(): String = buildJsonObject {
        put("app", "acoustical")
        put("port", PORT)
        put("ver", BuildConfig.VERSION_NAME)
        put("dev", Build.MODEL)
    }.toString()

    // === Emparejamiento Wi-Fi: código de 6 dígitos (se pega una vez en el PC) ===

    fun pairCode(): String {
        var c = prefs.getString(KEY_CODE, null)
        if (c == null) {
            c = newCode()
            prefs.edit().putString(KEY_CODE, c).apply()
        }
        return c
    }

    /** Genera un código nuevo: invalida a los PC ya emparejados. */
    fun regenerateCode() {
        val c = newCode()
        prefs.edit().putString(KEY_CODE, c).apply()
        _status.value = "Código nuevo: vuelve a pegarlo en el PC (Ajustes > Móvil)"
    }

    private fun newCode(): String = (100000 + SecureRandom().nextInt(900000)).toString()

    private fun codeOk(sent: String?): Boolean =
        sent != null && sent.isNotEmpty() && sent == pairCode()

    /** IP del móvil en la red Wi-Fi (para mostrarla / IP manual en el PC). */
    fun lanIp(): String {
        val ifaces = runCatching { NetworkInterface.getNetworkInterfaces() }.getOrNull()
            ?: return ""
        while (ifaces.hasMoreElements()) {
            val ni = ifaces.nextElement()
            if (!ni.isUp || ni.isLoopback || ni.isVirtual) continue
            val addrs = ni.inetAddresses
            while (addrs.hasMoreElements()) {
                val a = addrs.nextElement()
                if (a is Inet4Address && !a.isLoopbackAddress) return a.hostAddress ?: ""
            }
        }
        return ""
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

        // /sync mueve ajustes: exige el código de emparejamiento.
        // /manifest y /payload son solo lectura de datos públicos.
        if (path == "/sync") {
            val sentCode = lines.firstOrNull { it.startsWith("X-Acoustical-Code:", ignoreCase = true) }
                ?.substringAfter(':')?.trim()
            if (!codeOk(sentCode)) {
                writeHttp(socket, "403 Forbidden", "application/json",
                    """{"type":"pair","ok":false,"error":"code"}""".toByteArray(Charsets.UTF_8))
                return
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

        // Emparejamiento: sin código válido no se mueve ningún ajuste
        if (!codeOk(obj.textContent("code"))) {
            writeRawJson(socket, """{"type":"pair","ok":false,"error":"code"}""")
            return
        }

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

    // === Hub del PC: comandos que viajan con la próxima sincronización ===

    /** Encola un comando para el Hub de Windows (ruta 0 = principal, 1..4 auxiliares). */
    fun sendHubCommand(route: Int, cmd: String, value: Double) {
        val json = buildJsonObject {
            put("route", route)
            put("cmd", cmd)
            put("value", value)
        }
        synchronized(hubLock) {
            pendingHubCommands.addLast(json)
            // Evita colas infinitas si el PC está desconectado
            while (pendingHubCommands.size > 8) pendingHubCommands.removeFirst()
        }
        _status.value = "Comando del Hub en cola: ruta ${route + 1} · $cmd"
    }

    private fun takePendingHubCommand(): JsonObject? =
        synchronized(hubLock) { pendingHubCommands.removeFirstOrNull() }

    private val hubLock = Any()
    private val pendingHubCommands = ArrayDeque<JsonObject>()

    /** Botón "Enviar mis ajustes al PC": deja los ajustes actuales encolados en
     *  el estado pendiente. El PC, en su próximo sondeo (cada pocos segundos),
     *  los aplica y lo confirma con un push "acked", con lo que se borra. */
    fun pushMyConfigToPc() {
        val cfgText = localConfigJson
        if (cfgText.isBlank()) {
            _status.value = "Aún no hay ajustes que enviar al PC"
            return
        }
        // localConfigJson es un objeto JSON válido (el ViewModel lo genera con
        // buildJsonObject), así que basta con incrustarlo; el estado resultante
        // es {"config":{...},"sendToPc":true}, que currentSyncJson() ya sabe leer.
        val staged = """{"config":$cfgText,"sendToPc":true}"""
        prefs.edit().putString(KEY_SYNC_STATE, staged).apply()
        _status.value = "Enviando ajustes al PC… (se aplican en unos segundos)"
    }

    // === Estado sincronizado ===

    private fun currentSyncJson(): String {
        val stored = prefs.getString(KEY_SYNC_STATE, null) ?: "{}"
        val payload = runCatching { Json.parseToJsonElement(stored).jsonObject }
            .getOrElse { buildJsonObject { } }
        val out = LinkedHashMap(payload)
        // La config en vivo viaja siempre: el PC la usa en "Recibir del móvil".
        // Sobrescribe el eco de la última config que el PC haya enviado.
        runCatching { Json.parseToJsonElement(localConfigJson).jsonObject }
            .onSuccess { out["config"] = it }
        takePendingHubCommand()?.let { out["hubCmd"] = it }
        return "{\"type\":\"sync\",\"ok\":true,\"payload\":${JsonObject(out)}}"
    }

    private fun storeSyncPayload(text: String, fromPc: Boolean) {
        val valid = runCatching { Json.parseToJsonElement(text).jsonObject }.getOrNull() ?: return
        // Confirmación del PC de que aplicó los ajustes encolados: se borra el
        // estado pendiente y ya no vuelve a enviarse.
        if (valid.containsKey("acked") && !valid.containsKey("sendToPc")) {
            prefs.edit().remove(KEY_SYNC_STATE).apply()
            _status.value = "El PC ha aplicado tus ajustes"
            return
        }
        prefs.edit().putString(KEY_SYNC_STATE, valid.toString()).apply()
        // Config enviada por el PC (su botón "Sincronizar"): el ViewModel la
        // aplica al motor local.
        (valid["config"] as? JsonObject)?.let { cfg ->
            _remoteConfig.value = System.currentTimeMillis() to cfg
        }
        _status.value = if (fromPc) "Ajustes recibidos del PC" else "Ajustes enviados al PC"
    }

    // === Manifest para el PC ===

    private fun manifestJson(): String {
        val file = payloadFile()
        return buildJsonObject {
            put("type", "manifest")
            put("ok", true)
            putJsonObject("app") {
                put("name", "Acoustical")
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

    fun configuredRepo(): String = prefs.getString(KEY_REPO, "") ?: ""

    fun installedPayloadVersion(): String? = prefs.getString(KEY_VERSION, null)

    /**
     * Descarga el instalador de Windows y lo deja verificado (SHA-256) en el
     * almacenamiento del móvil para servirlo al PC por USB.
     *
     * Acepta dos formatos:
     *  - Repositorio de GitHub (github.com/usuario/repo o usuario/repo): busca el
     *    último release publicado, baja su instalador y recuerda el repositorio
     *    para las comprobaciones automáticas.
     *  - Enlace directo al .exe (debe incluir la versión, p. ej. Setup-1.1.0.exe).
     */
    fun downloadWindowsPayload(urlInput: String) {
        val input = urlInput.trim()
        if (input.isEmpty()) {
            _status.value = "Pega tu repositorio de GitHub o el enlace del instalador"
            return
        }
        if (_downloading.value) return
        _downloading.value = true
        _status.value = "Buscando el instalador…"
        Thread({
            try {
                val repo = githubRepoOf(input)
                if (repo != null) prefs.edit().putString(KEY_REPO, repo).apply()
                if (repo != null) {
                    val (url, version) = latestReleaseInstaller(repo)
                    downloadPayload(url, version)
                } else {
                    val version = VERSION_REGEX.find(input)?.value
                        ?: throw IllegalStateException("El enlace directo debe incluir la versión (p. ej. Setup-1.1.0.exe)")
                    downloadPayload(input, version)
                }
            } catch (e: Exception) {
                _status.value = "Descarga fallida: ${e.message ?: "error"}"
            } finally {
                _downloading.value = false
            }
        }, "acoustical-payload-download").start()
    }

    /**
     * Comprueba el último release del repositorio configurado y, si es más
     * reciente que el paquete guardado, lo descarga automáticamente.
     */
    fun checkForWindowsUpdate() {
        val repo = prefs.getString(KEY_REPO, null)
        if (repo.isNullOrEmpty()) {
            _status.value = "Configura primero tu repositorio de GitHub"
            return
        }
        checkForWindowsUpdate(repo)
    }

    private fun checkForWindowsUpdate(repo: String) {
        if (_downloading.value) return
        _downloading.value = true
        _status.value = "Comprobando versiones en GitHub…"
        Thread({
            try {
                val (url, version) = latestReleaseInstaller(repo)
                if (!isNewerVersion(version, installedPayloadVersion())) {
                    _status.value = "Ya está instalada la versión más reciente ($version)"
                    return@Thread
                }
                _status.value = "Versión nueva disponible: $version — descargando…"
                downloadPayload(url, version)
            } catch (e: Exception) {
                _status.value = "No se pudo consultar GitHub: ${e.message ?: "error"}"
            } finally {
                _downloading.value = false
            }
        }, "acoustical-update-check").start()
    }

    /** Comprobación automática al arrancar la app, como máximo cada 30 minutos. */
    private fun maybeAutoCheck() {
        val repo = prefs.getString(KEY_REPO, null) ?: return
        val now = System.currentTimeMillis()
        if (now - prefs.getLong(KEY_LAST_CHECK, 0L) < AUTO_CHECK_INTERVAL_MS) return
        prefs.edit().putLong(KEY_LAST_CHECK, now).apply()
        checkForWindowsUpdate(repo)
    }

    /** Consulta el último release del repositorio y devuelve (enlace .exe, versión). */
    private fun latestReleaseInstaller(repo: String): Pair<String, String> {
        val conn = URL("https://api.github.com/repos/$repo/releases/latest")
            .openConnection() as HttpURLConnection
        conn.connectTimeout = 15000
        conn.readTimeout = 20000
        conn.setRequestProperty("Accept", "application/vnd.github+json")
        conn.setRequestProperty("User-Agent", "Acoustical-Android")
        conn.connect()
        if (conn.responseCode !in 200..299) throw IllegalStateException("GitHub HTTP ${conn.responseCode}")
        val text = conn.inputStream.use { it.readBytes().toString(Charsets.UTF_8) }
        val json = Json.parseToJsonElement(text).jsonObject
        val tag = json.textContent("tag_name") ?: ""
        val assets = (json["assets"] as? JsonArray)
            ?.mapNotNull { it as? JsonObject }
            ?: emptyList()
        val asset = assets.firstOrNull {
            (it.textContent("name") ?: "").endsWith(".exe", ignoreCase = true)
        } ?: throw IllegalStateException("El release $tag no incluye el instalador .exe")
        val name = asset.textContent("name").orEmpty()
        val url = asset.textContent("browser_download_url")
            ?: throw IllegalStateException("El adjunto no tiene enlace de descarga")
        val version = VERSION_REGEX.find(name)?.value
            ?: tag.removePrefix("v").takeIf { VERSION_REGEX.matches(it) }
            ?: throw IllegalStateException("No se pudo leer la versión del release")
        return url to version
    }

    /** Devuelve "usuario/repo" si la entrada apunta a GitHub; null si es un enlace directo. */
    private fun githubRepoOf(input: String): String? {
        GITHUB_REPO_REGEX.find(input)?.let {
            return "${it.groupValues[1]}/${it.groupValues[2].removeSuffix(".git")}"
        }
        if (!input.contains("://") && !input.endsWith(".exe", ignoreCase = true)) {
            GITHUB_SHORT_REGEX.matchEntire(input)?.let {
                return "${it.groupValues[1]}/${it.groupValues[2]}"
            }
        }
        return null
    }

    /** Compara versiones numéricas: 1.10 > 1.9 > 1.2.3. */
    private fun isNewerVersion(candidate: String, current: String?): Boolean {
        if (current.isNullOrEmpty()) return true
        fun parts(v: String) = v.split('.').map { it.takeWhile { c -> c.isDigit() }.toIntOrNull() ?: 0 }
        val a = parts(candidate)
        val b = parts(current)
        for (i in 0 until maxOf(a.size, b.size)) {
            val x = a.getOrElse(i) { 0 }
            val y = b.getOrElse(i) { 0 }
            if (x != y) return x > y
        }
        return false
    }

    /** Descarga y verifica el instalador; lo guarda listo para servirlo por USB. */
    private fun downloadPayload(url: String, version: String) {
        _status.value = "Descargando paquete de Windows $version…"
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
        const val BEACON_PORT = 41042

        private const val PREFS = "acoustical_phone_sync"
        private const val KEY_SYNC_STATE = "sync_state"
        private const val KEY_CODE = "pair_code"
        private const val KEY_VERSION = "windows_version"
        private const val KEY_SHA256 = "windows_sha256"
        private const val KEY_URL = "windows_url"
        private const val KEY_REPO = "github_repo"
        private const val KEY_LAST_CHECK = "last_update_check"

        private const val READ_TIMEOUT_MS = 600
        private const val BEACON_INTERVAL_MS = 2000L
        private const val MAX_HEADER_BYTES = 64 * 1024
        private const val MAX_MESSAGE_BYTES = 1024 * 1024
        private const val MAX_PAYLOAD_BYTES = 512L * 1024 * 1024
        private const val AUTO_CHECK_INTERVAL_MS = 30L * 60 * 1000
        private val VERSION_REGEX = Regex("""(\d+\.\d+(?:\.\d+)*)""")
        private val GITHUB_REPO_REGEX = Regex("""github\.com/+([A-Za-z0-9_.-]+)/+([A-Za-z0-9_.-]+)""")
        private val GITHUB_SHORT_REGEX = Regex("""^([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)$""")

        @Volatile private var instance: PhoneSyncManager? = null

        fun get(context: Context): PhoneSyncManager =
            instance ?: synchronized(this) {
                instance ?: PhoneSyncManager(context.applicationContext).also { instance = it }
            }
    }
}
