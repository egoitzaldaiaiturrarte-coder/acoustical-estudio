package com.rork.acoustical.domain.model

import kotlinx.serialization.Serializable
import kotlin.math.roundToInt

/**
 * Audio bit depth options for capture and processing.
 */
@Serializable
enum class BitDepth(val bits: Int, val label: String) {
    INT16(16, "16-bit"),
    INT24(24, "24-bit"),
    FLOAT32(32, "32-bit float")
}

/**
 * Type of device in the controller ecosystem.
 */
@Serializable
enum class DeviceType(val label: String, val icon: String) {
    PHONE("Móvil", "phone"),
    TABLET("Tablet", "tablet"),
    CONSOLE("Consola digital", "console"),
    BLUETOOTH_SPEAKER("Altavoz BT", "speaker"),
    USB_AUDIO("Interface USB", "usb"),
    COMPUTER("Ordenador", "computer"),
    PA_SYSTEM("Sistema PA", "pa"),
    MONITOR("Monitor", "monitor"),
    IN_EARS("In-Ear", "inear")
}

/**
 * How a device connects to the session.
 */
@Serializable
enum class DeviceConnection(val label: String, val helpText: String) {
    WIFI_OSC(
        "WiFi / OSC",
        "Conecta la consola por WiFi usando OSC. Introduce la IP de la consola y el puerto (X32: 10023, A&H: 53000). " +
            "Más estable: cable Ethernet al router, consola con IP fija."
    ),
    WIFI_MESH(
        "WiFi Mesh",
        "Otros móviles con AcoustiCal se autodescubren en la red WiFi local. " +
            "Sin configuración manual: todos en la misma red WiFi y pulsar 'Unirse a sesión'."
    ),
    BLUETOOTH(
        "Bluetooth",
        "Empareja el altavoz o interface desde Ajustes de Android > Bluetooth. " +
            "Después selecciónalo aquí. Para estabilidad: mantén el móvil cerca del dispositivo BT."
    ),
    USB(
        "USB Audio",
        "Conecta la interface o consola por cable USB. Android debe soportar USB Audio Class. " +
            "Usa un cable USB-OTG de calidad. La latencia más baja disponible."
    ),
    MIDI(
        "MIDI CC",
        "Para DAWs y controladores hardware. Conecta por USB-MIDI o Bluetooth MIDI. " +
            "Configura el canal MIDI y los CC numbers en tu DAW."
    )
}

/**
 * Role of this device in the session.
 */
@Serializable
enum class DeviceRole(val label: String) {
    HOST("Host / Operador"),
    LISTENER("Listener"),
    CONTROLLER("Controlador"),
    MUSICIAN_MONITOR("Músico / Monitor")
}

/**
 * Connection state of a device.
 */
@Serializable
enum class DeviceState(val label: String) {
    DISCONNECTED("Desconectado"),
    CONNECTING("Conectando..."),
    CONNECTED("Conectado"),
    ERROR("Error"),
    PAUSED("En pausa")
}

/**
 * A device in the audio controller session.
 */
@Serializable
data class WorkDevice(
    val id: String,
    val name: String,
    val type: DeviceType,
    val connection: DeviceConnection,
    val role: DeviceRole = DeviceRole.LISTENER,
    val state: DeviceState = DeviceState.DISCONNECTED,
    val ipAddress: String = "",
    val port: Int = 0,
    val latencyMs: Float = 0f,
    val isActive: Boolean = false,
    val splAtDevice: Float = 0f,
    val isThisDevice: Boolean = false,
    val addedAtMs: Long = 0L
)

/**
 * Type of virtual work environment for spatial positioning.
 */
@Serializable
enum class WorkEnvironmentType(val label: String, val defaultWidth: Float, val defaultDepth: Float, val defaultHeight: Float) {
    ROOM("Sala", 8f, 10f, 3f),
    STUDIO("Estudio", 6f, 7f, 2.8f),
    STAGE("Escenario", 12f, 8f, 5f),
    OUTDOOR("Exterior", 20f, 30f, 0f);

    val hasWalls: Boolean get() = this != OUTDOOR
}

/**
 * Dimensions of the virtual workspace.
 */
@Serializable
data class EnvironmentDimensions(
    val width: Float = 8f,
    val depth: Float = 10f,
    val height: Float = 3f
)

/**
 * Reference source for measurement comparison.
 */
@Serializable
enum class ReferenceSource(val label: String, val description: String) {
    AUDIO_FILE("Archivo de audio", "Pink noise, sweep o pista de referencia cargada desde almacenamiento"),
    CONSOLE_LOOP("Loop de consola", "La consola genera una señal de prueba internamente (X32: RTA generator)"),
    PRESET_SEQUENCE("Secuencia predefinida", "Secuencia de test automática con tonos y sweeps calibrados"),
    DAW_AUX("Envío DAW/Consola", "Canal auxiliar desde la DAW o consola enviado a AcoustiCal para comparar")
}

/**
 * Output target where corrected audio is sent.
 */
@Serializable
data class OutputTarget(
    val id: String,
    val name: String,
    val deviceType: DeviceType,
    val channel: String = "Main LR",
    val bus: Int = 1,
    val isActive: Boolean = true,
    val delayMs: Float = 0f,
    val gainDb: Float = 0f,
    val isMuted: Boolean = false,
    val isSolo: Boolean = false,
    val volume: Float = 0.75f,
    val spl: Float = 0f,
    val connectionState: DeviceState = DeviceState.DISCONNECTED,
    val bluetoothAddress: String = "",
    val isLocalDevice: Boolean = false
) {
    val effectiveGainDb: Float get() = if (isMuted) -60f else gainDb
    val volumePercent: Int get() = (volume * 100f).toInt()
}

/**
 * Stereo linking mode.
 */
@Serializable
enum class StereoMode(val label: String) {
    LINKED("Estéreo linkado"),
    FREE("Estéreo libre (L/R independientes)"),
    MONO("Mono")
}

/**
 * App mode: controller operator or musician personal monitor.
 */
@Serializable
enum class AppMode(val label: String) {
    CONTROLLER("Controlador (operador)"),
    MUSICIAN("Músico (monitor personal)")
}

/**
 * Quality of auto-check probes.
 */
@Serializable
enum class ProbeQuality(val label: String, val fftSize: FftSize, val bandCount: BandCount) {
    FAST("Rápida", FftSize.SIZE_1024, BandCount.BANDS_10),
    NORMAL("Normal", FftSize.SIZE_2048, BandCount.BANDS_16),
    HIGH("Alta", FftSize.SIZE_4096, BandCount.BANDS_31)
}

/**
 * Configuration for automatic measurement probes.
 */
@Serializable
data class AutoCheckConfig(
    val enabled: Boolean = false,
    val intervalSeconds: Int = 60,
    val quality: ProbeQuality = ProbeQuality.NORMAL,
    val bitDepth: BitDepth = BitDepth.INT24
) {
    val intervalMs: Long get() = intervalSeconds * 1000L
}

/**
 * Spatial position from joystick and faders.
 * x: left/right (-1 to 1), y: forward/backward (-1 to 1)
 * z: depth/distance (0 to 1, 0=close, 1=far)
 * size: element size (0 to 1, 0=small, 1=large)
 */
@Serializable
data class SpatialPosition(
    val x: Float = 0f,
    val y: Float = 0f,
    val z: Float = 0.5f,
    val size: Float = 0.5f
) {
    val isCenter: Boolean get() = kotlin.math.abs(x) < 0.05f && kotlin.math.abs(y) < 0.05f

    fun positionLabel(env: WorkEnvironmentType): String {
        if (isCenter) return "Centro"
        val metersX = x * env.defaultWidth / 2f
        val metersY = y * env.defaultDepth / 2f
        val xLabel = if (metersX > 0.3f) "Der %.1fm".format(metersX) else if (metersX < -0.3f) "Izq %.1fm".format(-metersX) else ""
        val yLabel = if (metersY > 0.3f) "Frente %.1fm".format(metersY) else if (metersY < -0.3f) "Atrás %.1fm".format(-metersY) else ""
        return listOf(xLabel, yLabel).filter { it.isNotEmpty() }.joinToString(" · ")
    }

    val sizeLabel: String get() = when {
        size < 0.25f -> "Pequeño"
        size < 0.5f -> "Mediano"
        size < 0.75f -> "Grande"
        else -> "Muy grande"
    }

    val distanceLabel: String get() = "%.1fm".format(z * 15f)
}

/**
 * SPL compensation calculated from spatial position.
 * Maintains target SPL at listening position regardless of element placement.
 */
data class SplCompensation(
    val gainAdjustDb: Float = 0f,
    val delayAdjustMs: Float = 0f,
    val isActive: Boolean = true
) {
    companion object {
        /**
         * Calculate SPL compensation from spatial position.
         * Moving back (z increases) requires more gain to maintain pressure.
         * Making element bigger requires less gain (larger source = more efficient).
         */
        fun calculate(pos: SpatialPosition, targetSpl: Float, env: WorkEnvironmentType): SplCompensation =
            calculate(pos.x, pos.z, pos.size)

        /** z = 1 in meters, and the NEUTRAL distance (z = 0.5, the default). */
        const val MAX_METERS = 15f
        const val NEUTRAL_METERS = 7.5f

        /**
         * The EXACT Kotlin mirror of `acoustical_spatial_compensate()` in the
         * shared C core (the law the Windows app and the VST apply to their
         * real output), so it also works in pure-JVM tests.
         *
         * v1.10.1: relative to the NEUTRAL position (centered, z = 0.5 →
         * 7.5 m, medium size), which yields exactly 0 dB / 0 ms. The old
         * absolute law (1 m reference) gave +17.5 dB / 21.9 ms at the default
         * position; the distance is floored at 1 m (near field) instead of
         * the old 0.5 m threshold that jumped 6 dB. Inputs are clamped and a
         * non-finite input yields the neutral 0 dB / 0 ms.
         */
        fun calculate(x: Float, z: Float, size: Float): SplCompensation {
            if (!x.isFinite() || !z.isFinite() || !size.isFinite()) {
                return SplCompensation(gainAdjustDb = 0f, delayAdjustMs = 0f, isActive = true)
            }
            val cx = x.coerceIn(-1f, 1f)
            val cz = z.coerceIn(0f, 1f)
            val cs = size.coerceIn(0f, 1f)
            val distanceMeters = cz * MAX_METERS
            // Inverse square law relative to the neutral distance: +6 dB per
            // doubling of distance beyond it, −6 dB per halving.
            val distanceGain = 20f * kotlin.math.log10(maxOf(distanceMeters, 1f) / NEUTRAL_METERS)
            // Size compensation: larger elements are more efficient, need less gain
            val sizeGain = (0.5f - cs) * 6f
            // Lateral position: small gain adjustment for off-axis
            val lateralGain = kotlin.math.abs(cx) * 1.5f
            // Propagation delay beyond the neutral distance (speed of sound
            // ~343 m/s); closer than the neutral = no delay (it cannot be
            // negative).
            val extra = distanceMeters - NEUTRAL_METERS
            val delayMs = if (extra > 0f) extra / 343f * 1000f else 0f

            return SplCompensation(
                gainAdjustDb = distanceGain + sizeGain + lateralGain,
                delayAdjustMs = delayMs,
                isActive = true
            )
        }
    }
}

/**
 * A session for multi-device collaboration.
 */
@Serializable
data class WorkSession(
    val name: String,
    val pin: String,
    val isHost: Boolean = false,
    val devices: List<WorkDevice> = emptyList(),
    val createdAtMs: Long = 0L,
    val isActive: Boolean = false
)

/**
 * Complete work configuration.
 */
@Serializable
data class WorkConfig(
    val name: String = "Trabajo 1",
    val environment: WorkEnvironmentType = WorkEnvironmentType.ROOM,
    val dimensions: EnvironmentDimensions = EnvironmentDimensions(),
    val referenceSource: ReferenceSource = ReferenceSource.PRESET_SEQUENCE,
    val outputs: List<OutputTarget> = emptyList(),
    val stereoMode: StereoMode = StereoMode.LINKED,
    val appMode: AppMode = AppMode.CONTROLLER,
    val autoCheck: AutoCheckConfig = AutoCheckConfig(),
    val bitDepth: BitDepth = BitDepth.INT24,
    val sampleRate: SampleRate = SampleRate.SR_48000
)

/**
 * Type of input source in the routing view.
 */
enum class InputType(val label: String) {
    MIC("Micrófono del móvil"),
    APP_CAPTURE("Audio interno de apps"),
    EXTERNAL("Entrada externa (otro móvil)"),
    USB("USB Audio"),
    CONSOLE_IN("Consola In"),
    FILE_REFERENCE("Archivo / Referencia")
}

/**
 * An input source of the signal chain: mic, USB interface, console in or
 * reference file. Tappable from the routing screen to activate and trim.
 */
data class InputTarget(
    val id: String,
    val type: InputType,
    val name: String,
    val isActive: Boolean = false,
    val gainDb: Float = 0f,
    val subtitle: String = ""
)

/**
 * Result of an auto-check probe measurement.
 */
data class ProbeResult(
    val id: String,
    val timestampMs: Long,
    val spl: Float,
    val rt60Ms: Float,
    val correctionApplied: Float,
    val bandGains: List<Float> = emptyList(),
    val noiseFloorDb: Float = 0f
)

/**
 * Channel of the equalizer when L and R are unlinked.
 */
enum class EqChannel(val label: String) {
    LEFT("L"),
    RIGHT("R")
}

/**
 * Spatial state for stereo-free mode (independent L/R positions).
 */
data class StereoSpatialState(
    val left: SpatialPosition = SpatialPosition(),
    val right: SpatialPosition = SpatialPosition(),
    val mode: StereoMode = StereoMode.LINKED
) {
    val effective: SpatialPosition get() = when (mode) {
        StereoMode.LINKED, StereoMode.MONO -> left
        StereoMode.FREE -> left // Caller decides which side
    }
}

/**
 * Zones of the linked pan matrix L / Mid / R / Lados.
 * All four are really one send: adjusting one auto-corrects the others.
 */
@Serializable
enum class PanZone(val label: String, val short: String) {
    LEFT("Izquierda", "L"),
    MID("Centro", "Mid"),
    RIGHT("Derecha", "R"),
    SIDES("Lados", "Lados")
}

/**
 * Linked pan matrix: four zones that always sum to 1.
 * Raising one zone lowers the others proportionally,
 * so reverbs can go to the sides without leaving the center (or vice versa).
 */
@Serializable
data class PanMatrix(
    val left: Float = 0f,
    val mid: Float = 1f,
    val right: Float = 0f,
    val sides: Float = 0f
) {
    fun weight(zone: PanZone): Float = when (zone) {
        PanZone.LEFT -> left
        PanZone.MID -> mid
        PanZone.RIGHT -> right
        PanZone.SIDES -> sides
    }

    val total: Float get() = left + mid + right + sides
    val isCentered: Boolean get() = mid >= 0.98f && left < 0.02f && right < 0.02f && sides < 0.02f

    /**
     * Adjust one zone by [delta]; the other zones absorb the change
     * proportionally to their current weights, keeping the sum at 1.
     */
    fun adjust(zone: PanZone, delta: Float): PanMatrix {
        val current = weight(zone)
        val target = (current + delta).coerceIn(0f, 1f)
        val diff = target - current
        if (kotlin.math.abs(diff) < 0.0005f) return this

        val others = PanZone.entries.filter { it != zone }
        val othersSum = (1f - current).coerceAtLeast(0f)

        var newLeft = left
        var newMid = mid
        var newRight = right
        var newSides = sides

        others.forEach { other ->
            val share = if (othersSum > 0.0001f) weight(other) / othersSum else 1f / others.size
            val reduction = diff * share
            when (other) {
                PanZone.LEFT -> newLeft = (left - reduction).coerceIn(0f, 1f)
                PanZone.MID -> newMid = (mid - reduction).coerceIn(0f, 1f)
                PanZone.RIGHT -> newRight = (right - reduction).coerceIn(0f, 1f)
                PanZone.SIDES -> newSides = (sides - reduction).coerceIn(0f, 1f)
            }
        }
        when (zone) {
            PanZone.LEFT -> newLeft = target
            PanZone.MID -> newMid = target
            PanZone.RIGHT -> newRight = target
            PanZone.SIDES -> newSides = target
        }

        // Final normalization pass to keep the sum exactly 1
        val matrix = PanMatrix(newLeft, newMid, newRight, newSides)
        val sum = matrix.total
        return if (sum > 0.0001f) {
            matrix.copy(
                left = matrix.left / sum,
                mid = matrix.mid / sum,
                right = matrix.right / sum,
                sides = matrix.sides / sum
            )
        } else Centered
    }

    fun percent(zone: PanZone): Int = (weight(zone) * 100f).roundToInt()

    companion object {
        val Centered = PanMatrix(mid = 1f)
    }
}