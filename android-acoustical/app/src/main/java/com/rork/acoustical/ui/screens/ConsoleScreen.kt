package com.rork.acoustical.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Cable
import androidx.compose.material.icons.filled.CellTower
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Hub
import androidx.compose.material.icons.filled.SettingsInputAntenna
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.console.ConsoleChannel
import com.rork.acoustical.domain.console.ConsoleConfig
import com.rork.acoustical.domain.console.ConsoleConnectionState
import com.rork.acoustical.domain.console.ConsoleProtocol
import com.rork.acoustical.domain.console.ConsoleType
import com.rork.acoustical.domain.console.MeshPeer
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.ProgressIndicator
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

/**
 * Console integration screen — connect to professional audio consoles,
 * configure OSC/USB settings, manage multi-device mesh network.
 */
@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class, androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
fun ConsoleScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val consoleConfig = state.consoleConfig
    val consoleState = state.consoleConnectionState
    val meshPeers = state.meshPeers
    val meshRunning = state.meshIsRunning
    val meshIsMaster = state.meshIsMaster
    val syncStatus = state.consoleSyncStatus

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        // Title
        Text(
            text = "Consola Profesional",
            style = MaterialTheme.typography.headlineSmall,
            color = MaterialTheme.colorScheme.onSurface,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))
        Text(
            text = "Conecta tu móvil a la mesa de mezclas para análisis y corrección en vivo.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Connection status
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = when (consoleConfig.protocol) {
                                ConsoleProtocol.OSC -> Icons.Filled.CellTower
                                ConsoleProtocol.USB_AUDIO -> Icons.Filled.Usb
                                ConsoleProtocol.MIDI -> Icons.Filled.Cable
                                ConsoleProtocol.MANUAL -> Icons.Filled.AudioFile
                            },
                            contentDescription = null,
                            tint = when (consoleState) {
                                ConsoleConnectionState.CONNECTED -> LimeActive
                                ConsoleConnectionState.CONNECTING -> AmberAccent
                                ConsoleConnectionState.ERROR -> CoralAlert
                                else -> MaterialTheme.colorScheme.onSurfaceVariant
                            },
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(10.dp))
                        Text(
                            text = consoleConfig.type.label,
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    StatusPill(
                        text = consoleState.label,
                        color = when (consoleState) {
                            ConsoleConnectionState.CONNECTED -> LimeActive
                            ConsoleConnectionState.CONNECTING -> AmberAccent
                            ConsoleConnectionState.ERROR -> CoralAlert
                            ConsoleConnectionState.NOT_SUPPORTED -> MaterialTheme.colorScheme.outline
                            ConsoleConnectionState.DISCONNECTED -> MaterialTheme.colorScheme.outline
                        }
                    )
                }

                // Sync stats
                if (consoleState == ConsoleConnectionState.CONNECTED && syncStatus != null) {
                    Spacer(modifier = Modifier.height(12.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceEvenly
                    ) {
                        SyncStat("Enviados", "${syncStatus.commandsSent}")
                        SyncStat("Fallidos", "${syncStatus.commandsFailed}")
                        SyncStat("Latencia", "%.0f ms".format(syncStatus.latencyMs))
                    }
                }

                if (consoleState == ConsoleConnectionState.ERROR && syncStatus?.errorMessage != null) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = syncStatus.errorMessage,
                        style = MaterialTheme.typography.bodySmall,
                        color = CoralAlert
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    if (consoleState == ConsoleConnectionState.CONNECTED) {
                        Button(
                            onClick = { viewModel.disconnectConsole() },
                            modifier = Modifier.weight(1f).height(44.dp),
                            shape = RoundedCornerShape(12.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = CoralAlert,
                                contentColor = Color.Black
                            )
                        ) {
                            Text("Desconectar")
                        }
                    } else {
                        Button(
                            onClick = { viewModel.connectConsole() },
                            modifier = Modifier.weight(1f).height(44.dp),
                            shape = RoundedCornerShape(12.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = CyanPrimary,
                                contentColor = Color.Black
                            )
                        ) {
                            Text("Conectar")
                        }
                    }
                    OutlinedButton(
                        onClick = { viewModel.resetConsoleEq() },
                        modifier = Modifier.weight(1f).height(44.dp),
                        shape = RoundedCornerShape(12.dp),
                        enabled = consoleState == ConsoleConnectionState.CONNECTED
                    ) {
                        Text("Reset EQ", color = CyanPrimary)
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Protocol selection
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "Protocolo de conexión",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(12.dp))

                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ConsoleProtocol.entries.forEach { protocol ->
                        ProtocolChip(
                            label = protocol.label,
                            icon = when (protocol) {
                                ConsoleProtocol.OSC -> Icons.Filled.CellTower
                                ConsoleProtocol.USB_AUDIO -> Icons.Filled.Usb
                                ConsoleProtocol.MIDI -> Icons.Filled.Cable
                                ConsoleProtocol.MANUAL -> Icons.Filled.AudioFile
                            },
                            selected = consoleConfig.protocol == protocol,
                            onClick = { viewModel.setConsoleProtocol(protocol) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Console type selector (OSC mode)
                if (consoleConfig.protocol == ConsoleProtocol.OSC) {
                    Text(
                        text = "Tipo de consola",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    ConsoleTypeSelector(
                        selected = consoleConfig.type,
                        onSelect = { viewModel.setConsoleType(it) }
                    )

                    Spacer(modifier = Modifier.height(16.dp))

                    // IP address
                    OutlinedTextField(
                        value = consoleConfig.ipAddress,
                        onValueChange = { viewModel.setConsoleIp(it) },
                        label = { Text("Dirección IP") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(12.dp)
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    // OSC Port
                    OutlinedTextField(
                        value = consoleConfig.oscPort.toString(),
                        onValueChange = { it.toIntOrNull()?.let { p -> viewModel.setConsolePort(p) } },
                        label = { Text("Puerto OSC") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(12.dp),
                        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                            keyboardType = KeyboardType.Number
                        )
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    // Channel config
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        OutlinedTextField(
                            value = consoleConfig.channel.bus.toString(),
                            onValueChange = { it.toIntOrNull()?.let { b -> viewModel.setConsoleChannel(b) } },
                            label = { Text("Canal") },
                            modifier = Modifier.weight(1f),
                            singleLine = true,
                            shape = RoundedCornerShape(12.dp),
                            keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                                keyboardType = KeyboardType.Number
                            )
                        )
                        val typeExpanded = remember { mutableStateOf(false) }
                        ExposedDropdownMenuBox(
                            expanded = typeExpanded.value,
                            onExpandedChange = { typeExpanded.value = it },
                            modifier = Modifier.weight(1f)
                        ) {
                            OutlinedTextField(
                                value = consoleConfig.channel.channelType.label,
                                onValueChange = {},
                                readOnly = true,
                                label = { Text("Tipo") },
                                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = typeExpanded.value) },
                                modifier = Modifier.menuAnchor().fillMaxWidth(),
                                singleLine = true,
                                shape = RoundedCornerShape(12.dp)
                            )
                            ExposedDropdownMenu(
                                expanded = typeExpanded.value,
                                onDismissRequest = { typeExpanded.value = false }
                            ) {
                                ConsoleChannel.ChannelType.entries.forEach { type ->
                                    DropdownMenuItem(
                                        text = { Text(type.label) },
                                        onClick = {
                                            viewModel.setConsoleChannelType(type)
                                            typeExpanded.value = false
                                        }
                                    )
                                }
                            }
                        }
                    }

                    // EQ bands available on this console
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Bandas EQ: ${com.rork.acoustical.domain.console.ConsoleProfiles.eqBandCountFor(consoleConfig.type)} paramétricas",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }

                // USB Audio info
                if (consoleConfig.protocol == ConsoleProtocol.USB_AUDIO) {
                    Spacer(modifier = Modifier.height(12.dp))
                    val usbDevices = state.usbAudioDevices
                    Text(
                        text = "Dispositivos USB detectados: ${usbDevices.size}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    if (usbDevices.isNotEmpty()) {
                        Spacer(modifier = Modifier.height(8.dp))
                        usbDevices.forEach { device ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(10.dp))
                                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f))
                                    .padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    imageVector = Icons.Filled.Usb,
                                    contentDescription = null,
                                    tint = CyanPrimary,
                                    modifier = Modifier.size(20.dp)
                                )
                                Spacer(modifier = Modifier.width(10.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = device.product,
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurface
                                    )
                                    Text(
                                        text = device.vendor,
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                                Text(
                                    text = device.sampleRates.map { "${it / 1000}k" }.joinToString(", "),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = CyanGlow
                                )
                            }
                            Spacer(modifier = Modifier.height(8.dp))
                        }
                    } else {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "Conecta una interfaz USB (X32 USB card, Scarlett, UMC...) al móvil con un cable OTG.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Auto-correction settings
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "Corrección automática",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "Envía correcciones de EQ a la consola en tiempo real.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(12.dp))

                ToggleRow(
                    label = "Auto-corrección",
                    checked = consoleConfig.autoCorrectEnabled,
                    onCheckedChange = { viewModel.setAutoCorrectEnabled(it) }
                )
                Spacer(modifier = Modifier.height(8.dp))
                ToggleRow(
                    label = "Enviar ganancias a consola",
                    checked = consoleConfig.pushGainsToConsole,
                    onCheckedChange = { viewModel.setPushGainsEnabled(it) }
                )
                Spacer(modifier = Modifier.height(8.dp))
                ToggleRow(
                    label = "Leer EQ de la consola",
                    checked = consoleConfig.pullGainsFromConsole,
                    onCheckedChange = { viewModel.setPullGainsEnabled(it) }
                )

                Spacer(modifier = Modifier.height(12.dp))
                Text(
                    text = "Corrección máxima: ±%.0f dB".format(consoleConfig.maxCorrectionDb),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(4.dp))
                androidx.compose.material3.Slider(
                    value = consoleConfig.maxCorrectionDb,
                    onValueChange = { viewModel.setMaxCorrectionDb(it) },
                    valueRange = 1f..15f,
                    steps = 13
                )

                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "Intervalo de sync: %d ms".format(consoleConfig.correctionIntervalMs),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(4.dp))
                androidx.compose.material3.Slider(
                    value = consoleConfig.correctionIntervalMs.toFloat(),
                    onValueChange = { viewModel.setCorrectionInterval(it.toLong()) },
                    valueRange = 100f..2000f,
                    steps = 18
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Multi-device mesh network
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Filled.Hub,
                            contentDescription = null,
                            tint = if (meshRunning) LimeActive else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(22.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Red multi-dispositivo",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    if (meshRunning) {
                        StatusPill(
                            text = if (meshIsMaster) "Master" else "Listener",
                            color = if (meshIsMaster) AmberAccent else CyanGlow
                        )
                    }
                }

                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "Varios móviles meden simultáneamente desde distintas posiciones. El master agrega y corrige.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                Spacer(modifier = Modifier.height(12.dp))

                if (!meshRunning) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Button(
                            onClick = { viewModel.startMeshMaster() },
                            modifier = Modifier.weight(1f).height(44.dp),
                            shape = RoundedCornerShape(12.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = AmberAccent,
                                contentColor = Color.Black
                            )
                        ) {
                            Icon(Icons.Filled.Devices, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("Iniciar Master")
                        }
                        OutlinedButton(
                            onClick = { viewModel.startMeshListener() },
                            modifier = Modifier.weight(1f).height(44.dp),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Icon(Icons.Filled.SettingsInputAntenna, contentDescription = null, modifier = Modifier.size(18.dp), tint = CyanPrimary)
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("Listener", color = CyanPrimary)
                        }
                    }
                } else {
                    Button(
                        onClick = { viewModel.stopMesh() },
                        modifier = Modifier.fillMaxWidth().height(44.dp),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CoralAlert,
                            contentColor = Color.Black
                        )
                    ) {
                        Text("Detener red")
                    }
                }

                // Peer list
                if (meshPeers.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        text = "Dispositivos conectados (${meshPeers.size + 1})",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(8.dp))

                    // Self entry
                    PeerRow(
                        name = "${android.os.Build.MODEL} (yo)",
                        ip = "local",
                        spl = state.currentSpl,
                        role = if (meshIsMaster) "Master" else "Listener",
                        isSelf = true
                    )

                    meshPeers.forEach { peer ->
                        Spacer(modifier = Modifier.height(6.dp))
                        PeerRow(
                            name = peer.deviceName,
                            ip = peer.ipAddress,
                            spl = peer.currentSpl,
                            role = peer.role.label,
                            isSelf = false
                        )
                    }
                }

                // Aggregate stats
                if (meshRunning && meshIsMaster && meshPeers.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(12.dp))
                    val allSpls = listOf(state.currentSpl) + meshPeers.map { it.currentSpl }
                    val validSpls = allSpls.filter { it > 0f }
                    if (validSpls.isNotEmpty()) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceEvenly
                        ) {
                            MeshStat("PROM", "%.1f".format(validSpls.average()))
                            MeshStat("MÁX", "%.1f".format(validSpls.max()))
                            MeshStat("MÍN", "%.1f".format(validSpls.min()))
                            MeshStat("Nº", "${validSpls.size}")
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}

@Composable
private fun ProtocolChip(
    label: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    selected: Boolean,
    onClick: () -> Unit
) {
    val bg = if (selected) CyanPrimary.copy(alpha = 0.15f) else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
    val fg = if (selected) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(bg)
            .padding(horizontal = 12.dp, vertical = 8.dp)
            .let { it },
        verticalAlignment = Alignment.CenterVertically
    ) {
        TextButton(onClick = onClick) {
            Icon(icon, contentDescription = null, tint = fg, modifier = Modifier.size(16.dp))
            Spacer(modifier = Modifier.width(6.dp))
            Text(label, color = fg, style = MaterialTheme.typography.labelMedium)
        }
    }
}

@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
private fun ConsoleTypeSelector(
    selected: ConsoleType,
    onSelect: (ConsoleType) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = Modifier.fillMaxWidth()
    ) {
        OutlinedTextField(
            value = selected.label,
            onValueChange = {},
            readOnly = true,
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            modifier = Modifier.menuAnchor().fillMaxWidth(),
            singleLine = true,
            shape = RoundedCornerShape(12.dp)
        )
        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            ConsoleType.entries
                .filter { it != ConsoleType.USB_DIRECT && it != ConsoleType.MANUAL }
                .forEach { type ->
                    DropdownMenuItem(
                        text = {
                            Column {
                                Text(type.label)
                                Text(
                                    text = type.manufacturer,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        },
                        onClick = {
                            onSelect(type)
                            expanded = false
                        }
                    )
                }
        }
    }
}

@Composable
private fun ToggleRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface
        )
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange
        )
    }
}

@Composable
private fun SyncStat(label: String, value: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = value,
            style = MaterialTheme.typography.titleMedium,
            color = CyanGlow,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun PeerRow(
    name: String,
    ip: String,
    spl: Float,
    role: String,
    isSelf: Boolean
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(
                if (isSelf) CyanPrimary.copy(alpha = 0.08f)
                else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f)
            )
            .padding(10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(
                imageVector = Icons.Filled.Devices,
                contentDescription = null,
                tint = if (isSelf) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(16.dp)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Column {
                Text(
                    text = name,
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (isSelf) CyanGlow else MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = "$ip · $role",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Text(
            text = if (spl > 0f) "%.1f dB".format(spl) else "--",
            style = MaterialTheme.typography.titleSmall,
            color = if (isSelf) CyanGlow else MaterialTheme.colorScheme.onSurface,
            fontWeight = FontWeight.SemiBold
        )
    }
}

@Composable
private fun MeshStat(label: String, value: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = value,
            style = MaterialTheme.typography.titleMedium,
            color = AmberAccent,
            fontWeight = FontWeight.Bold
        )
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
