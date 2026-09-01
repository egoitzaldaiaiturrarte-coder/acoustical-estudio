package com.rork.acoustical.ui.screens

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.Smartphone
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.audio.AudioOutputInfo
import com.rork.acoustical.domain.audio.BluetoothAudioManager
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.DeviceType
import com.rork.acoustical.domain.model.OutputTarget
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import com.rork.acoustical.ui.theme.TextPrimary
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OutputMixScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    var showAddSheet by remember { mutableStateOf(false) }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(AbyssBlack, SurfaceTeal.copy(alpha = 0.5f))
                )
            )
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp)
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Salidas",
                    style = MaterialTheme.typography.headlineSmall,
                    color = TextPrimary,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "${state.outputs.count { it.isActive }} activas · ${state.outputs.size} total",
                    style = MaterialTheme.typography.labelMedium,
                    color = CyanGlow
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            if (state.outputs.isEmpty()) {
                EmptyOutputsState(
                    onAdd = { showAddSheet = true }
                )
            } else {
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(state.outputs, key = { it.id }) { output ->
                        OutputChannelCard(
                            output = output,
                            onVolumeChange = { viewModel.setOutputVolume(output.id, it) },
                            onMuteToggle = { viewModel.toggleOutputMute(output.id) },
                            onSoloToggle = { viewModel.toggleOutputSolo(output.id) },
                            onRemove = if (!output.isLocalDevice) {
                                { viewModel.removeOutput(output.id) }
                            } else null
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            Button(
                onClick = { showAddSheet = true },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(50.dp),
                shape = RoundedCornerShape(12.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyanPrimary.copy(alpha = 0.15f),
                    contentColor = CyanGlow
                )
            ) {
                Icon(Icons.Filled.Add, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Añadir salida", fontWeight = FontWeight.SemiBold)
            }

            Spacer(modifier = Modifier.height(16.dp))
        }

        if (showAddSheet) {
            ModalBottomSheet(
                onDismissRequest = { showAddSheet = false },
                sheetState = sheetState,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                AddOutputPanel(
                    btDevices = state.bluetoothDevices,
                    isScanning = state.isBtScanning,
                    isBtEnabled = state.isBluetoothEnabled,
                    isBtAvailable = state.isBluetoothAvailable,
                    audioOutputs = state.audioOutputDevices,
                    localOutputExists = state.outputs.any { it.isLocalDevice },
                    onScan = { viewModel.startBluetoothScan() },
                    onStopScan = { viewModel.stopBluetoothScan() },
                    onPair = { viewModel.pairBluetoothDevice(it) },
                    onAddBt = { viewModel.addBluetoothOutput(it) },
                    onAddLocal = { viewModel.addLocalOutput() },
                    onAddConsole = {
                        showAddSheet = false
                        navController.navigate("add_device")
                    }
                )
            }
        }
    }
}

@Composable
private fun EmptyOutputsState(onAdd: () -> Unit) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Icon(
                imageVector = Icons.Filled.VolumeUp,
                contentDescription = null,
                tint = CyanPrimary.copy(alpha = 0.3f),
                modifier = Modifier.size(48.dp)
            )
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                "Sin salidas configuradas",
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                "Añade altavoces, consolas o dispositivos BT",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.outline
            )
            Spacer(modifier = Modifier.height(16.dp))
            OutlinedButton(
                onClick = onAdd,
                shape = RoundedCornerShape(12.dp)
            ) {
                Text("Añadir primera salida")
            }
        }
    }
}

@Composable
private fun OutputChannelCard(
    output: OutputTarget,
    onVolumeChange: (Float) -> Unit,
    onMuteToggle: () -> Unit,
    onSoloToggle: () -> Unit,
    onRemove: (() -> Unit)?
) {
    val animatedVolume by animateFloatAsState(
        targetValue = output.volume,
        animationSpec = tween(100),
        label = "vol"
    )
    val statusColor by animateColorAsState(
        targetValue = when {
            output.connectionState == DeviceState.CONNECTED -> LimeActive
            output.connectionState == DeviceState.CONNECTING -> AmberAccent
            output.connectionState == DeviceState.ERROR -> CoralAlert
            else -> MaterialTheme.colorScheme.outline
        },
        animationSpec = tween(200),
        label = "status"
    )

    GlassCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.fillMaxWidth()) {
            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(36.dp)
                            .clip(CircleShape)
                            .background(SurfaceElevated),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = deviceIcon(output.deviceType),
                            contentDescription = null,
                            tint = if (output.isActive) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(20.dp)
                        )
                    }
                    Column {
                        Text(
                            text = output.name,
                            style = MaterialTheme.typography.titleSmall,
                            color = TextPrimary,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            text = "${output.deviceType.label} · ${output.channel}",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    if (output.isLocalDevice) {
                        StatusPill(text = "Local", color = CyanPrimary)
                    } else {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .clip(CircleShape)
                                .background(statusColor)
                        )
                        if (output.connectionState == DeviceState.CONNECTED) {
                            Text(
                                text = "%.2f ms".format(output.delayMs),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (onRemove != null) {
                            IconButton(onClick = onRemove, modifier = Modifier.size(28.dp)) {
                                Icon(
                                    Icons.Filled.Close,
                                    contentDescription = "Eliminar",
                                    tint = MaterialTheme.colorScheme.outline,
                                    modifier = Modifier.size(16.dp)
                                )
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Volume slider
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Slider(
                    value = animatedVolume,
                    onValueChange = onVolumeChange,
                    modifier = Modifier.weight(1f),
                    colors = SliderDefaults.colors(
                        thumbColor = if (output.isMuted) CoralAlert else CyanGlow,
                        activeTrackColor = if (output.isMuted) CoralAlert.copy(alpha = 0.5f) else CyanPrimary,
                        inactiveTrackColor = SurfaceElevated
                    )
                )
                Text(
                    text = if (output.isMuted) "MUTE" else "%d%%".format(output.volumePercent),
                    style = MaterialTheme.typography.labelMedium,
                    color = if (output.isMuted) CoralAlert else CyanGlow,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.width(48.dp)
                )
            }

            Spacer(modifier = Modifier.height(4.dp))

            // Gain + SPL + Mute/Solo
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "%+.1f dB".format(output.effectiveGainDb),
                    style = MaterialTheme.typography.labelSmall,
                    color = if (output.isMuted) CoralAlert else AmberAccent
                )
                if (output.spl > 0f) {
                    Text(
                        text = "%.0f dB SPL".format(output.spl),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    ToggleChip(
                        text = "M",
                        isActive = output.isMuted,
                        activeColor = CoralAlert,
                        onClick = onMuteToggle
                    )
                    ToggleChip(
                        text = "S",
                        isActive = output.isSolo,
                        activeColor = AmberAccent,
                        onClick = onSoloToggle
                    )
                }
            }
        }
    }
}

@Composable
private fun ToggleChip(
    text: String,
    isActive: Boolean,
    activeColor: Color,
    onClick: () -> Unit
) {
    val bgColor = if (isActive) activeColor.copy(alpha = 0.2f) else SurfaceElevated.copy(alpha = 0.5f)
    val fgColor = if (isActive) activeColor else MaterialTheme.colorScheme.onSurfaceVariant

    Box(
        modifier = Modifier
            .size(28.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(bgColor),
        contentAlignment = Alignment.Center
    ) {
        androidx.compose.material3.TextButton(
            onClick = onClick,
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(0.dp)
        ) {
            Text(
                text,
                style = MaterialTheme.typography.labelSmall,
                color = fgColor,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

@Composable
private fun AddOutputPanel(
    btDevices: List<BluetoothAudioManager.BtDevice>,
    isScanning: Boolean,
    isBtEnabled: Boolean,
    isBtAvailable: Boolean,
    audioOutputs: List<AudioOutputInfo>,
    localOutputExists: Boolean,
    onScan: () -> Unit,
    onStopScan: () -> Unit,
    onPair: (String) -> Unit,
    onAddBt: (String) -> Unit,
    onAddLocal: () -> Unit,
    onAddConsole: () -> Unit
) {
    LazyColumn(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 24.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Text(
                "Añadir salida",
                style = MaterialTheme.typography.titleMedium,
                color = CyanGlow,
                fontWeight = FontWeight.SemiBold
            )
        }

        // Local output
        item {
            Text("Salida local", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (localOutputExists) {
                Text("Altavoz del móvil ya añadido", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.outline)
            } else {
                OutlinedButton(onClick = onAddLocal, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(12.dp)) {
                    Icon(Icons.Filled.Smartphone, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Altavoz del móvil")
                }
            }
        }

        // Bluetooth
        item {
            Text("Bluetooth", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(modifier = Modifier.height(4.dp))
            if (!isBtAvailable) {
                Text("Bluetooth no disponible en este dispositivo", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.outline)
            } else if (!isBtEnabled) {
                Text("Activa Bluetooth en Ajustes del sistema", style = MaterialTheme.typography.labelSmall, color = AmberAccent)
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(
                        onClick = if (isScanning) onStopScan else onScan,
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Icon(
                            if (isScanning) Icons.Filled.Stop else Icons.Filled.BluetoothSearching,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(if (isScanning) "Detener" else "Escanear")
                    }
                }
                if (isScanning) {
                    Text("Buscando dispositivos...", style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                }
            }
        }

        // BT device list
        if (btDevices.isNotEmpty()) {
            items(btDevices, key = { it.address }) { device ->
                GlassCard(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Icon(
                                Icons.Filled.Bluetooth,
                                contentDescription = null,
                                tint = if (device.isConnected) LimeActive else if (device.isBonded) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(20.dp)
                            )
                            Column {
                                Text(device.name, style = MaterialTheme.typography.bodyMedium, color = TextPrimary, fontWeight = FontWeight.Medium)
                                val statusText = when {
                                    device.isConnected -> "Conectado"
                                    device.isBonded -> "Emparejado"
                                    else -> "No emparejado"
                                }
                                Text(
                                    "$statusText · ${device.rssi} dBm",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                        if (device.isConnected) {
                            OutlinedButton(onClick = { onAddBt(device.address) }, shape = RoundedCornerShape(8.dp)) {
                                Text("Añadir")
                            }
                        } else if (device.isBonded) {
                            OutlinedButton(onClick = { onAddBt(device.address) }, shape = RoundedCornerShape(8.dp)) {
                                Text("Conectar")
                            }
                        } else {
                            OutlinedButton(onClick = { onPair(device.address) }, shape = RoundedCornerShape(8.dp)) {
                                Text("Emparejar")
                            }
                        }
                    }
                }
            }
        }

        // Console
        item {
            Text("Consola / Interface", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            OutlinedButton(onClick = onAddConsole, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(12.dp)) {
                Icon(Icons.Filled.Tune, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(8.dp))
                Text("Añadir consola (OSC)")
            }
        }

        // System audio outputs
        if (audioOutputs.isNotEmpty()) {
            item {
                Text("Salidas del sistema", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            items(audioOutputs) { output ->
                Text(
                    "${output.name} — ${output.typeLabel}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 8.dp)
                )
            }
        }

        item { Spacer(modifier = Modifier.height(24.dp)) }
    }
}

private fun deviceIcon(type: DeviceType): ImageVector = when (type) {
    DeviceType.PHONE -> Icons.Filled.Smartphone
    DeviceType.TABLET -> Icons.Filled.Smartphone
    DeviceType.CONSOLE -> Icons.Filled.Tune
    DeviceType.BLUETOOTH_SPEAKER -> Icons.Filled.Bluetooth
    DeviceType.USB_AUDIO -> Icons.Filled.Usb
    DeviceType.COMPUTER -> Icons.Filled.Computer
    DeviceType.PA_SYSTEM -> Icons.Filled.GraphicEq
    DeviceType.MONITOR -> Icons.Filled.Speaker
    DeviceType.IN_EARS -> Icons.Filled.Headphones
}
