package com.rork.acoustical.domain.console

import kotlinx.serialization.Serializable

/**
 * Supported console connection protocols.
 */
@Serializable
enum class ConsoleProtocol(val label: String) {
    OSC("OSC / WiFi"),
    USB_AUDIO("USB Audio"),
    MIDI("MIDI CC"),
    MANUAL("Manual (sólo análisis)")
}

/**
 * Connection state of a console link.
 */
@Serializable
enum class ConsoleConnectionState(val label: String) {
    DISCONNECTED("Desconectado"),
    CONNECTING("Conectando..."),
    CONNECTED("Conectado"),
    ERROR("Error"),
    NOT_SUPPORTED("No soportado")
}

/**
 * Type of professional console we're talking to.
 * Determines OSC address mapping and EQ parameter format.
 */
@Serializable
enum class ConsoleType(val label: String, val manufacturer: String) {
    BEHRINGER_X32("Behringer X32", "Behringer"),
    MIDAS_M32("Midas M32", "Midas"),
    ALLEN_HEATH_SQ("A&H SQ Series", "Allen & Heath"),
    ALLEN_HEATH_QU("A&H QU Series", "Allen & Heath"),
    YAMAHA_TF("Yamaha TF", "Yamaha"),
    GENERIC_OSC("OSC Genérico", "Genérico"),
    USB_DIRECT("USB Audio Directo", "USB"),
    MANUAL("Manual", "—")
}

/**
 * Target channel on the console for EQ correction.
 */
@Serializable
data class ConsoleChannel(
    val bus: Int = 1,
    val channelType: ChannelType = ChannelType.INPUT,
    val eqBandCount: Int = 4,
    val label: String = "CH 1"
) {
    @Serializable
    enum class ChannelType(val label: String) {
        INPUT("Input"),
        BUS("Bus"),
        MAIN("Main LR"),
        MATRIX("Matrix")
    }
}

/**
 * Full configuration for a console connection.
 */
@Serializable
data class ConsoleConfig(
    val type: ConsoleType = ConsoleType.MANUAL,
    val protocol: ConsoleProtocol = ConsoleProtocol.MANUAL,
    val ipAddress: String = "192.168.1.100",
    val oscPort: Int = 10023,
    val listenPort: Int = 0,
    val channel: ConsoleChannel = ConsoleChannel(),
    val autoCorrectEnabled: Boolean = false,
    val correctionIntervalMs: Long = 500,
    val maxCorrectionDb: Float = 6f,
    val pushGainsToConsole: Boolean = true,
    val pullGainsFromConsole: Boolean = false
) {
    companion object {
        val Default = ConsoleConfig()
    }
}

/**
 * A peer device in the mesh network (other phones running AcoustiCal).
 */
@Serializable
data class MeshPeer(
    val id: String,
    val ipAddress: String,
    val deviceName: String,
    val role: MeshRole = MeshRole.LISTENER,
    val lastSeenMs: Long = 0L,
    val currentSpl: Float = 0f,
    val isActive: Boolean = false
) {
    @Serializable
    enum class MeshRole(val label: String) {
        MASTER("Master"),
        LISTENER("Listener"),
        CORRECTOR("Corrector")
    }
}

/**
 * Aggregated measurement from multiple mesh peers.
 */
data class MeshAggregate(
    val peers: List<MeshPeer>,
    val averageSpl: Float,
    val maxSpl: Float,
    val minSpl: Float,
    val consensusCorrections: List<Float>,
    val timestampMs: Long
)

/**
 * A correction command to send to the console.
 */
data class CorrectionCommand(
    val channel: ConsoleChannel,
    val bandIndex: Int,
    val frequencyHz: Float,
    val gainDb: Float,
    val q: Float = 1.41f,
    val timestampMs: Long = System.currentTimeMillis()
)

/**
 * Status of the last console sync cycle.
 */
data class ConsoleSyncStatus(
    val state: ConsoleConnectionState,
    val lastSyncMs: Long = 0L,
    val commandsSent: Int = 0,
    val commandsFailed: Int = 0,
    val latencyMs: Float = 0f,
    val errorMessage: String? = null
)
