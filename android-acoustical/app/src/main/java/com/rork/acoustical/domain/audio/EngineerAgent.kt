package com.rork.acoustical.domain.audio

import com.rork.acoustical.domain.model.AgentAdvice
import com.rork.acoustical.domain.model.AgentMode
import com.rork.acoustical.domain.model.AgentSeverity
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.OutputTarget

/**
 * Rule-based engineer agent acting as sound engineer, electronics engineer
 * and producer.
 *
 * Protection rules (pre-flight checks) ALWAYS run, even in OFF mode:
 * nothing is allowed to "sound" without the agent first verifying that the
 * signal path is actually going to produce sound.
 *
 * - OFF: only critical protections.
 * - ON_DEMAND: advice only when explicitly consulted.
 * - ASSISTANT: continuous guidance, protects from mistakes.
 * - MASTER: same as assistant plus educational explanations of each decision.
 */
class EngineerAgent {

    /**
     * Snapshot of the current mixing context used to generate advice.
     */
    data class Snapshot(
        val isRunning: Boolean = false,
        val currentSpl: Float = 0f,
        val safeSplLimit: Float = 85f,
        val targetSpl: Float = 75f,
        val rt60Ms: Float = 0f,
        val outputs: List<OutputTarget> = emptyList(),
        val currentDelayMs: Float = 25f,
        val recommendedDelayMs: Float = 25f,
        val panIsCentered: Boolean = true
    )

    /**
     * Pre-flight check before unmuting an output.
     * Verifies the signal will actually be heard ("se comprueba que va a sonar").
     * Returns null when everything is safe, or the reason to block/warn.
     */
    fun preFlightUnmute(output: OutputTarget): AgentAdvice? {
        if (output.connectionState == DeviceState.DISCONNECTED) {
            return AgentAdvice(
                severity = AgentSeverity.BLOCK,
                title = "No va a sonar",
                message = "\"${output.name}\" está desconectada. Actívala solo cuando la conexión esté lista.",
                suggestion = "Comprueba el enlace Bluetooth/OSC/USB de la salida antes de des-silenciar.",
                explanation = "Una salida desconectada no recibe señal: des-silenciarla solo crea falsa seguridad en el fader."
            )
        }
        if (output.volume <= 0.02f) {
            return AgentAdvice(
                severity = AgentSeverity.WARN,
                title = "Volumen a cero",
                message = "\"${output.name}\" tiene el volumen al 0%. Sonará, pero inaudible.",
                suggestion = "Sube el volumen de la salida por encima del 10% antes o después de des-silenciar.",
                explanation = "El mute actúa antes del fader: con volumen 0 el resultado audible es el mismo silencio."
            )
        }
        if (output.isSolo) {
            return AgentAdvice(
                severity = AgentSeverity.INFO,
                title = "Solo activo",
                message = "\"${output.name}\" está en solo: el resto de salidas quedan atenuadas.",
                suggestion = "Desactiva el solo si quieres escuchar todo el conjunto."
            )
        }
        return null
    }

    /**
     * Check a gain change for hearing-safety and headroom.
     */
    fun preFlightGainChange(output: OutputTarget, newGainDb: Float, currentSpl: Float, safeLimit: Float): AgentAdvice? {
        if (newGainDb > 10f) {
            return AgentAdvice(
                severity = AgentSeverity.WARN,
                title = "Ganancia alta",
                message = "%+.1f dB en \"${output.name}\" puede saturar el bus.".format(newGainDb),
                suggestion = "Mantén la corrección por debajo de +10 dB y reparte el exceso en otras bandas o salidas.",
                explanation = "El headroom digital se mide desde 0 dBFS hacia abajo: pasada esa cifra el recorte es duro y no se puede deshacer."
            )
        }
        val projected = currentSpl + newGainDb
        if (projected > safeLimit) {
            return AgentAdvice(
                severity = AgentSeverity.WARN,
                title = "Riesgo auditivo",
                message = "Con ese ajuste el SPL proyectado sería %.0f dB, sobre el límite seguro de %.0f dB.".format(projected, safeLimit),
                suggestion = "Reparte el exceso entre varias salidas o baja el SPL objetivo.",
                explanation = "A partir de 85 dB, cada +3 dB reduce a la mitad el tiempo de exposición seguro (85 dB → 8h, 88 dB → 4h)."
            )
        }
        return null
    }

    /**
     * Generate contextual advice for the current snapshot and agent mode.
     */
    fun advise(snapshot: Snapshot, mode: AgentMode): List<AgentAdvice> {
        val advices = mutableListOf<AgentAdvice>()

        // Critical protections (always)
        if (snapshot.currentSpl >= snapshot.safeSplLimit && snapshot.currentSpl > 0f) {
            advices += AgentAdvice(
                severity = AgentSeverity.BLOCK,
                title = "SPL sobre el límite",
                message = "%.0f dB medidos — límite seguro %.0f dB.".format(snapshot.currentSpl, snapshot.safeSplLimit),
                suggestion = "Baja el máster o aleja al músico del PA antes de continuar.",
                explanation = "Ley de inversión del cuadrado: duplicar la distancia resta ~6 dB en el punto de escucha."
            )
        }

        if (mode == AgentMode.OFF) return advices

        if (!snapshot.isRunning) {
            advices += AgentAdvice(
                severity = AgentSeverity.INFO,
                title = "Motor parado",
                message = "El análisis acústico no está activo.",
                suggestion = "Arranca el motor para que el agente pueda medir y corregir en tiempo real."
            )
        }

        if (snapshot.rt60Ms > 1800f) {
            advices += AgentAdvice(
                severity = AgentSeverity.WARN,
                title = "Sala muy reverberante",
                message = "RT60 ≈ %.1fs: la voz pierde inteligibilidad.".format(snapshot.rt60Ms / 1000f),
                suggestion = "Aplica más absorción digital: baja la corrección de medios-graves y sube el suavizado.",
                explanation = "El RT60 es el tiempo que tarda el sonido en caer 60 dB. Ideal para voz: 0.4–0.9s."
            )
        }

        val silentActive = snapshot.outputs.filter { it.isActive && !it.isMuted && it.volume <= 0.02f }
        if (silentActive.isNotEmpty()) {
            advices += AgentAdvice(
                severity = AgentSeverity.WARN,
                title = "Salidas activas pero mudas",
                message = silentActive.joinToString(", ") { it.name } + " tienen volumen 0%.",
                suggestion = "Súbelas desde el Centro de Control o siléncialas con mute para no confundir estados."
            )
        }

        val disconnectedActive = snapshot.outputs.filter { it.isActive && it.connectionState == DeviceState.DISCONNECTED }
        if (disconnectedActive.isNotEmpty()) {
            advices += AgentAdvice(
                severity = AgentSeverity.WARN,
                title = "Enlaces caídos",
                message = disconnectedActive.joinToString(", ") { it.name } + " están desconectadas.",
                suggestion = "Revisa Bluetooth o la IP de la consola desde Salidas."
            )
        }

        val delayMismatch = kotlin.math.abs(snapshot.currentDelayMs - snapshot.recommendedDelayMs)
        if (delayMismatch > 5f && snapshot.recommendedDelayMs > 0f) {
            advices += AgentAdvice(
                severity = AgentSeverity.INFO,
                title = "Delay sin ajustar",
                message = "Delay actual %.1f ms, recomendado %.1f ms.".format(snapshot.currentDelayMs, snapshot.recommendedDelayMs),
                suggestion = "Aplica la automatización de distancias para sincronizar el PA con los monitores.",
                explanation = "El sonido viaja a ~343 m/s: 1 metro de error = 2.9 ms de desfase, audible como comb-filter."
            )
        }

        if (!snapshot.panIsCentered) {
            advices += AgentAdvice(
                severity = AgentSeverity.INFO,
                title = "Paneo activo",
                message = "La matriz L/Mid/R/Lados está repartida.",
                suggestion = "Recuerda que los cuatro envíos se auto-corrigen: lo que quitas del centro va a los lados automáticamente."
            )
        }

        if (mode == AgentMode.MASTER) {
            advices += AgentAdvice(
                severity = AgentSeverity.INFO,
                title = "Clase del día",
                message = masterLesson(snapshot),
                suggestion = "Pregúntame tocando \"Consultar\" cuando quieras profundizar en cualquier ajuste."
            )
        }

        return advices
    }

    private fun masterLesson(snapshot: Snapshot): String {
        return when {
            snapshot.currentSpl > snapshot.targetSpl + 5f ->
                "Estás %.0f dB por encima del objetivo. La ganancia se percibe logarítmicamente: +10 dB suena \"el doble\", no 10 veces más.".format(snapshot.currentSpl - snapshot.targetSpl)
            snapshot.rt60Ms > 1200f ->
                "Con RT60 alto, baja los agudos de corrección: la cola reverberante llena esos huecos por sí sola y corregir de más crea fatiga."
            else ->
                "Regla de oro del productor: mide antes, ajusta después. Cada movimiento de fader cambia tres cosas a la vez (nivel, fase y psychoacústica) — por eso este agente verifica siempre que va a sonar antes de tocar nada."
        }
    }
}
