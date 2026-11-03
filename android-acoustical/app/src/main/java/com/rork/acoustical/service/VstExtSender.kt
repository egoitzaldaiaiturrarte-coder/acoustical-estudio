package com.rork.acoustical.service

import android.content.SharedPreferences
import com.rork.acoustical.domain.audio.AudioEngine
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/**
 * VstExtSender — v1.11.1: el móvil como EXTENSIÓN del VST.
 *
 * Contraparte (lado móvil) de ExtLink.h (lado VST, windows-acoustical):
 * el VST "Acoustical Dynamic EQ" instalado en el DAW escucha en UDP 41047
 * el análisis que el motor de ESTA app calcula (el MISMO core C del
 * plugin, vía JNI): las bandas medidas, el SPL y el RT60. El VST lo
 * alimenta a su motor como segunda captura (setSecondaryCaptureLevels):
 * cada banda toma el nivel más fuerte entre la captura propia del VST y la
 * del móvil, y el corrector converge sobre la referencia del VST. El móvil
 * puede ir en la PA, en el coche o a 30 metros con su propio micro, y
 * seguir alimentando la corrección que suena en el DAW.
 *
 * Protocolo (un solo sentido: móvil -> VST):
 *   Datagrama JSON a <ip-del-vst>:41047:
 *     {"app":"acoustical","type":"ext","code":"NNNNNN",
 *      "bands":[ -6.2, -1.1, ... ],"spl":78.4,"peakSpl":80.0,
 *      "rt60":310,"ts":1699999999}
 *   "code" = el MISMO código de emparejamiento de 6 dígitos que el link
 *   PC/Windows (PhoneSyncManager.pairCode, Ajustes > PC/Windows): el VST
 *   solo acepta datagramas cuyo code coincide con su parámetro "Código
 *   móvil" (0 = enlace desactivado).
 *
 * Destino (dos vías, en este orden):
 *   1. IP manual (campo de Ajustes > Enlace VST): la más flexible (el PC
 *      del DAW puede estar en otra red, p. ej. por Tailscale).
 *   2. Autodetección: el VST emite una baliza UDP en 41046
 *      ({"app":"acoustical","vst":true,"id":"NNNN"}) a la multidifusión de
 *      su LAN y al loopback; esta clase escucha 41046 y guarda la IP
 *      fuente de las balizas frescas (< 10 s) como destino.
 *
 * Cadencia: el ViewModel conecta [onAnalysis] al callback de análisis del
 * motor (cada tick, según el intervalo configurado): sin temporizadores
 * propios. El envío es una write UDP no bloqueante sobre un socket
 * persistente (se crea una sola vez, no por cada datagrama): el hilo de
 * análisis nunca se detiene por el enlace.
 */
class VstExtSender(
    private val codeProvider: () -> String,
    private val prefs: SharedPreferences
) {
    companion object {
        const val VST_EXT_PORT = 41047
        const val BEACON_PORT = 41046  // la baliza del VST (InstanceLink)
        const val KEY_TARGET_IP = "target_ip"
        const val KEY_ENABLED = "enabled"
        const val BEACON_FRESH_MS = 10_000L
    }

    private val enabled = AtomicBoolean(prefs.getBoolean(KEY_ENABLED, false))
    private val manualTarget = AtomicReference(prefs.getString(KEY_TARGET_IP, "").orEmpty())

    // Detección automática (baliza 41046 del VST en la LAN).
    @Volatile private var autoIp: String = ""
    @Volatile private var autoSeenMs: Long = 0L
    @Volatile private var discoveryRunning = false
    private var discoveryThread: Thread? = null

    // Socket de envío (no ligado: solo se envía desde él). Se crea una vez
    // y se reutiliza: no se abre/cierra por cada datagrama.
    @Volatile private var sendSock: DatagramSocket? = null

    /** (UI) Activa/desactiva el enlace. */
    fun setEnabled(on: Boolean) {
        enabled.set(on)
        prefs.edit().putBoolean(KEY_ENABLED, on).apply()
        if (on) startDiscovery()
    }

    /** (UI) IP manual del destino (el PC donde corre el DAW con el VST). */
    fun setTargetIp(ip: String) {
        val v = ip.trim()
        manualTarget.set(v)
        prefs.edit().putString(KEY_TARGET_IP, v).apply()
    }

    fun targetIp(): String {
        val manual = manualTarget.get()
        if (manual.isNotEmpty()) return manual
        return if (System.currentTimeMillis() - autoSeenMs < BEACON_FRESH_MS) autoIp else ""
    }

    /** (UI) Código actual a mostrar (se pega en el VST, parámetro "Código móvil"). */
    fun currentCode(): String = runCatching { codeProvider() }.getOrNull().orEmpty()

    /** (UI) Estado para el texto de Ajustes. */
    fun status(): String {
        if (!enabled.get()) return "Desactivado"
        val ip = targetIp()
        return when {
            ip.isEmpty() ->
                "Activado: esperando un destino (baliza del VST en la LAN, o pon una IP manual)"
            ip == manualTarget.get() -> "Enviando a $ip:$VST_EXT_PORT (IP manual)"
            else -> "Enviando a $ip:$VST_EXT_PORT (baliza del VST, vista hace " +
                ((System.currentTimeMillis() - autoSeenMs) / 1000).toInt() + " s)"
        }
    }

    /** (Hilo del motor, en cada análisis) — nunca bloquea. */
    fun onAnalysis(r: AudioEngine.AnalysisResult, rt60Ms: Float) {
        if (!enabled.get()) return
        val code = currentCode()
        if (code.isEmpty() || !code.all { it.isDigit() }) return
        val ip = targetIp()
        if (ip.isEmpty()) return
        val bands = r.measuredLevels
        if (bands.isEmpty()) return
        val sock = sendSock ?: synchronized(this) {
            sendSock ?: runCatching { DatagramSocket() }.getOrNull().also { sendSock = it }
        }
        if (sock == null) return
        val json = buildJson(code, bands, r.spl, r.peakSpl, rt60Ms)
        runCatching {
            sock.send(
                DatagramPacket(
                    json.toByteArray(Charsets.UTF_8), json.length,
                    InetAddress.getByName(ip), VST_EXT_PORT
                )
            )
        }
    }

    // === Detección por baliza (41046) ===

    private fun startDiscovery() {
        if (discoveryRunning) return
        discoveryRunning = true
        discoveryThread = Thread({
            val sock = runCatching { DatagramSocket(BEACON_PORT) }.getOrNull()
            if (sock == null) { discoveryRunning = false; return@Thread }
            runCatching { sock.soTimeout = 500 }
            while (discoveryRunning) {
                val buf = ByteArray(512)
                val p = DatagramPacket(buf, buf.size)
                try {
                    sock.receive(p)
                } catch (_: SocketTimeoutException) {
                    continue
                }
                val txt = String(buf, 0, p.length, Charsets.UTF_8)
                // El beacon del VST: {"app":"acoustical","vst":true,"id":"NNNN"}
                if (txt.contains("\"app\":\"acoustical\"") && txt.contains("\"vst\":true")) {
                    autoIp = p.address.hostAddress ?: ""
                    autoSeenMs = System.currentTimeMillis()
                }
            }
            runCatching { sock.close() }
            discoveryRunning = false
        }, "vstext-disc").apply { isDaemon = true; start() }
    }

    /** (Al tirar el ViewModel) */
    fun dispose() {
        discoveryRunning = false
        runCatching { discoveryThread?.interrupt() }
        discoveryThread = null
        runCatching { sendSock?.close() }
        sendSock = null
    }

    // === El datagrama ===
    // El parser del VST (ExtLink.h, C++17 puro, testeado en la CI) espera
    // este esquema plano; los floats se formatean con Kotlin (el strtod del
    // VST entiende tanto decimales como exponenciales).
    private fun buildJson(
        code: String,
        bands: FloatArray,
        spl: Float,
        peakSpl: Float,
        rt60Ms: Float
    ): String {
        val sb = StringBuilder(160 + bands.size * 8)
        sb.append("{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"")
            .append(code)
            .append("\",\"bands\":[")
        for (i in bands.indices) {
            if (i > 0) sb.append(',')
            sb.append(bands[i])
        }
        sb.append("],\"spl\":").append(spl)
        sb.append(",\"peakSpl\":").append(peakSpl)
        sb.append(",\"rt60\":").append(rt60Ms)
        sb.append(",\"ts\":").append(System.currentTimeMillis())
        sb.append("}")
        return sb.toString()
    }
}
