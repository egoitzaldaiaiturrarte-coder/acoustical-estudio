package com.rork.acoustical.ui.screens

import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.SpeakerGroup
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Tv
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material.icons.filled.Waves
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.audio.TestSignalType
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.DeviceType
import com.rork.acoustical.domain.model.OutputTarget
import com.rork.acoustical.domain.model.ReferenceSource
import com.rork.acoustical.domain.model.RoutingNode
import com.rork.acoustical.domain.model.RoutingNodeType
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanDim
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RoutingMapScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val routingNodes = state.routingNodes
    val routingConnections = state.routingConnections
    val context = LocalContext.current

    var selectedOutputId by remember { mutableStateOf<String?>(null) }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val selectedOutput = state.outputs.find { it.id == selectedOutputId }
    val masterVolume = state.outputs.firstOrNull()?.volume ?: 0.75f

    // Create-output sheet state — every node can spawn its own output
    var showCreateOutput by remember { mutableStateOf(false) }
    var createType by remember { mutableStateOf(DeviceType.PA_SYSTEM) }
    var createName by remember { mutableStateOf("") }
    val createSheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(AbyssBlack, SurfaceTeal.copy(alpha = 0.4f))
                )
            )
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp)
        ) {
            Spacer(modifier = Modifier.height(16.dp))

            Text(
                text = "Mapa de Ruteos",
                style = MaterialTheme.typography.headlineMedium,
                color = CyanGlow,
                fontWeight = FontWeight.Medium
            )
            Text(
                text = "Toca cualquier nodo para ajustarlo — o para crear su salida al instante",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(modifier = Modifier.height(12.dp))

            // Signal flow map: connections canvas + aligned node grid
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(470.dp)
                ) {
                    Canvas(modifier = Modifier.fillMaxSize()) {
                        val w = size.width

                        fun nodeCenter(node: RoutingNode): Offset {
                            val colIndex = when (node.type) {
                                RoutingNodeType.INPUT, RoutingNodeType.REFERENCE -> 0
                                RoutingNodeType.PROCESS -> 1
                                RoutingNodeType.OUTPUT -> 2
                            }
                            val rowIndex = routingNodes.filter { it.type == node.type }.indexOf(node)
                            val x = (colIndex + 0.5f) * w / 3f
                            val y = 70.dp.toPx() + rowIndex * 64.dp.toPx() + 22.dp.toPx()
                            return Offset(x, y)
                        }

                        routingConnections.forEach { conn ->
                            val fromNode = routingNodes.find { it.id == conn.fromId } ?: return@forEach
                            val toNode = routingNodes.find { it.id == conn.toId } ?: return@forEach
                            val fromPos = nodeCenter(fromNode)
                            val toPos = nodeCenter(toNode)

                            val lineColor = when {
                                !conn.isActive -> Color.White.copy(alpha = 0.1f)
                                fromNode.hasError || toNode.hasError -> AmberAccent.copy(alpha = 0.6f)
                                else -> CyanPrimary.copy(alpha = 0.5f)
                            }

                            val midX = (fromPos.x + toPos.x) / 2f
                            drawLine(
                                color = lineColor,
                                start = fromPos,
                                end = Offset(midX, fromPos.y),
                                strokeWidth = 2f,
                                cap = StrokeCap.Round
                            )
                            drawLine(
                                color = lineColor,
                                start = Offset(midX, fromPos.y),
                                end = Offset(midX, toPos.y),
                                strokeWidth = 2f,
                                cap = StrokeCap.Round
                            )
                            drawLine(
                                color = lineColor,
                                start = Offset(midX, toPos.y),
                                end = toPos,
                                strokeWidth = 2f,
                                cap = StrokeCap.Round
                            )

                            if (conn.isActive && state.workConfig.autoCheck.enabled) {
                                drawCircle(
                                    color = CyanGlow.copy(alpha = 0.8f),
                                    radius = 4f,
                                    center = Offset(midX, fromPos.y)
                                )
                            }
                        }
                    }

                    // Column headers
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 8.dp)
                    ) {
                        Text(
                            "Entradas",
                            style = MaterialTheme.typography.labelMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.weight(1f),
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                        Text(
                            "Proceso",
                            style = MaterialTheme.typography.labelMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.weight(1f),
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                        Text(
                            "Salidas",
                            style = MaterialTheme.typography.labelMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.weight(1f),
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                    }

                    // Node grid — aligned with the canvas coordinates
                    Row(modifier = Modifier.fillMaxSize()) {
                        listOf(
                            RoutingNodeType.INPUT,
                            RoutingNodeType.PROCESS,
                            RoutingNodeType.OUTPUT
                        ).forEach { type ->
                            Column(
                                modifier = Modifier
                                    .weight(1f)
                                    .padding(top = 70.dp),
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.spacedBy(20.dp)
                            ) {
                                routingNodes.filter { it.type == type }.forEach { node ->
                                    val nodeOutputs = outputsForNode(node.id, state.outputs)
                                    NodeWidget(
                                        node = node,
                                        outputs = nodeOutputs,
                                        onClick = {
                                            val first = nodeOutputs.firstOrNull()
                                            if (first != null) {
                                                selectedOutputId = first.id
                                            } else {
                                                createType = deviceTypeForNode(node.id)
                                                createName = ""
                                                showCreateOutput = true
                                            }
                                        }
                                    )
                                }
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // === Centro de Control ===
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            "Centro de Control",
                            style = MaterialTheme.typography.titleMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold
                        )
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedButton(
                                onClick = { viewModel.muteAllOutputs() },
                                modifier = Modifier.height(36.dp),
                                shape = RoundedCornerShape(10.dp)
                            ) {
                                Text("Mute todo", style = MaterialTheme.typography.labelMedium, color = CoralAlert)
                            }
                            Button(
                                onClick = { viewModel.unmuteAllOutputs() },
                                modifier = Modifier.height(36.dp),
                                shape = RoundedCornerShape(10.dp),
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = CyanPrimary.copy(alpha = 0.25f),
                                    contentColor = CyanGlow
                                )
                            ) {
                                Text("Activar todo", style = MaterialTheme.typography.labelMedium)
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    Text(
                        "Volumen general · %d%%".format((masterVolume * 100f).toInt()),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Slider(
                        value = masterVolume,
                        onValueChange = { viewModel.setMasterVolume(it) },
                        colors = SliderDefaults.colors(
                            thumbColor = CyanGlow,
                            activeTrackColor = CyanPrimary,
                            inactiveTrackColor = SurfaceElevated
                        )
                    )

                    Spacer(modifier = Modifier.height(6.dp))

                    // Add output straight from the routing screen
                    OutlinedButton(
                        onClick = {
                            createType = DeviceType.PA_SYSTEM
                            createName = ""
                            showCreateOutput = true
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(40.dp),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text(
                            "+ Añadir salida",
                            color = CyanPrimary,
                            style = MaterialTheme.typography.labelMedium
                        )
                    }

                    Spacer(modifier = Modifier.height(6.dp))

                    if (state.outputs.isEmpty()) {
                        Text(
                            "Sin salidas aún. Añádelas desde Control → Salidas: monitores, in-ears, PA, Bluetooth, altavoz del móvil...",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        state.outputs.forEach { output ->
                            OutputControlRow(
                                output = output,
                                onClick = { selectedOutputId = output.id },
                                onMuteToggle = { viewModel.toggleOutputMute(output.id) },
                                onVolumeChange = { viewModel.setOutputVolume(output.id, it) }
                            )
                            Spacer(modifier = Modifier.height(6.dp))
                        }
                    }

                    Spacer(modifier = Modifier.height(6.dp))

                    // Modo concierto (focus): DND + airplane shortcut
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                "Modo concierto",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurface,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = when {
                                    state.focusModeActive -> "Notificaciones ajenas silenciadas"
                                    viewModel.hasFocusAccess() -> "Silencia interrupciones mientras trabajas"
                                    else -> "Concede acceso a No molestar para silenciar notificaciones"
                                },
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = state.focusModeActive,
                            onCheckedChange = { want ->
                                if (want && !viewModel.hasFocusAccess()) {
                                    context.startActivity(Intent(Settings.ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS))
                                } else {
                                    viewModel.toggleFocusMode()
                                }
                            }
                        )
                    }
                    TextButton(
                        onClick = { context.startActivity(Intent(Settings.ACTION_AIRPLANE_MODE_SETTINGS)) },
                        modifier = Modifier.align(Alignment.Start)
                    ) {
                        Text(
                            "Abrir modo avión (ajuste del sistema)",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // Reference section — source selectable and captured spectrum manageable
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Referencia",
                        style = MaterialTheme.typography.titleMedium,
                        color = AmberAccent,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = "Fuente de señal de referencia",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 6.dp)
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    ReferenceSource.entries.forEach { ref ->
                        FilterChip(
                            selected = state.workConfig.referenceSource == ref,
                            onClick = { viewModel.setReferenceSource(ref) },
                            label = { Text(ref.label) },
                            modifier = Modifier.fillMaxWidth(),
                            colors = FilterChipDefaults.filterChipColors(
                                selectedContainerColor = AmberAccent.copy(alpha = 0.2f),
                                selectedLabelColor = AmberAccent
                            )
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                    }
                    Text(
                        text = state.workConfig.referenceSource.description,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    Spacer(modifier = Modifier.height(10.dp))

                    // Captured spectrum reference
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "Referencia de espectro",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurface,
                            fontWeight = FontWeight.SemiBold
                        )
                        StatusPill(
                            text = if (state.isReferenceCaptured) "Activa" else "Sin capturar",
                            color = if (state.isReferenceCaptured) LimeActive else AmberAccent
                        )
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = if (state.isReferenceCaptured) {
                            "La corrección compara la sala contra la referencia capturada."
                        } else {
                            "Sin referencia capturada: la corrección usa objetivo plano (0 dB en todas las bandas). Captura una para comparar contra la señal original."
                        },
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(modifier = Modifier.height(6.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        if (state.isReferenceCaptured) {
                            OutlinedButton(
                                onClick = { viewModel.clearReference() },
                                modifier = Modifier
                                    .weight(1f)
                                    .height(38.dp),
                                shape = RoundedCornerShape(10.dp)
                            ) {
                                Text(
                                    "Borrar",
                                    color = CoralAlert,
                                    style = MaterialTheme.typography.labelMedium
                                )
                            }
                        } else {
                            Button(
                                onClick = { viewModel.captureReference() },
                                enabled = state.isRunning,
                                modifier = Modifier
                                    .weight(1f)
                                    .height(38.dp),
                                shape = RoundedCornerShape(10.dp),
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = CyanPrimary.copy(alpha = 0.25f),
                                    contentColor = CyanGlow
                                )
                            ) {
                                Text("Capturar", style = MaterialTheme.typography.labelMedium)
                            }
                        }
                        if (!state.isRunning) {
                            Text(
                                text = "Arranca el motor para capturar",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier
                                    .weight(1f)
                                    .padding(start = 4.dp)
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "%s · %s".format(state.workConfig.bitDepth.label, state.workConfig.sampleRate.label),
                            style = MaterialTheme.typography.labelSmall,
                            color = CyanGlow
                        )
                        Text(
                            text = "Delay: %.1f ms".format(state.config.audioDelayMs),
                            style = MaterialTheme.typography.labelSmall,
                            color = CyanGlow
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // Quick access to EQ and detailed views
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = { navController.navigate("full_eq") },
                    modifier = Modifier.weight(1f).height(44.dp),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Text("EQ Completo", color = CyanPrimary, style = MaterialTheme.typography.labelLarge)
                }
                OutlinedButton(
                    onClick = { navController.navigate("calibration") },
                    modifier = Modifier.weight(1f).height(44.dp),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Text("Calibración", color = CyanPrimary, style = MaterialTheme.typography.labelLarge)
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // Device summary
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Dispositivos en ruteo",
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    if (state.session.devices.isEmpty()) {
                        Text(
                            text = "Sin dispositivos conectados",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        state.session.devices.forEach { device ->
                            Row(
                                modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    text = device.name,
                                    style = MaterialTheme.typography.labelMedium,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                                Text(
                                    text = device.connection.label,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(24.dp))
        }

        // Output detail sheet
        if (selectedOutput != null) {
            ModalBottomSheet(
                onDismissRequest = { selectedOutputId = null },
                sheetState = sheetState,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                OutputDetailSheet(
                    output = selectedOutput,
                    isTesting = state.testingOutputId == selectedOutput.id,
                    onTestSignal = { viewModel.playTestSignal(selectedOutput.id, it) },
                    onStopTest = { viewModel.stopTestSignal() },
                    onGainChange = { viewModel.setOutputGainDb(selectedOutput.id, it) },
                    onDelayChange = { viewModel.setOutputDelayMs(selectedOutput.id, it) },
                    onMuteToggle = { viewModel.toggleOutputMute(selectedOutput.id) },
                    onSoloToggle = { viewModel.toggleOutputSolo(selectedOutput.id) },
                    onRemove = {
                        viewModel.removeOutput(selectedOutput.id)
                        selectedOutputId = null
                    }
                )
            }
        }

        // Create output sheet — reached from the map nodes or the add button
        if (showCreateOutput) {
            ModalBottomSheet(
                onDismissRequest = { showCreateOutput = false },
                sheetState = createSheetState,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                CreateOutputSheet(
                    selectedType = createType,
                    name = createName,
                    onTypeChange = { createType = it },
                    onNameChange = { createName = it },
                    onCreate = {
                        viewModel.addOutput(createName, createType)
                        showCreateOutput = false
                        createName = ""
                    }
                )
            }
        }
    }
}

@Composable
private fun NodeWidget(
    node: RoutingNode,
    outputs: List<OutputTarget>,
    onClick: () -> Unit
) {
    val allMuted = outputs.isNotEmpty() && outputs.all { it.isMuted }
    val hasOutputs = outputs.isNotEmpty()

    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .height(48.dp)
            .clip(RoundedCornerShape(8.dp))
            .clickable { onClick() }
            .background(
                when {
                    !hasOutputs -> SurfaceElevated.copy(alpha = 0.2f)
                    allMuted -> CoralAlert.copy(alpha = 0.12f)
                    else -> CyanPrimary.copy(alpha = 0.1f)
                }
            )
            .padding(horizontal = 6.dp)
    ) {
        NodeIcon(node.type, node.isActive)
        Spacer(modifier = Modifier.height(2.dp))
        Text(
            text = node.name,
            style = MaterialTheme.typography.labelSmall,
            color = if (node.isActive) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
            fontWeight = FontWeight.Medium
        )
        Text(
            text = when {
                !hasOutputs -> "Añadir"
                allMuted -> "MUTE"
                else -> "%d%%".format(outputs.first().volumePercent)
            },
            style = MaterialTheme.typography.labelSmall,
            color = when {
                !hasOutputs -> CyanDim
                allMuted -> CoralAlert
                else -> CyanGlow
            },
            fontWeight = if (allMuted) FontWeight.Bold else FontWeight.Normal
        )
    }
}

@Composable
private fun OutputControlRow(
    output: OutputTarget,
    onClick: () -> Unit,
    onMuteToggle: () -> Unit,
    onVolumeChange: (Float) -> Unit
) {
    val connectionColor = when (output.connectionState) {
        DeviceState.CONNECTED -> LimeActive
        DeviceState.CONNECTING -> AmberAccent
        DeviceState.ERROR -> CoralAlert
        else -> MaterialTheme.colorScheme.outline
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .clickable { onClick() }
            .background(SurfaceElevated.copy(alpha = 0.4f))
            .padding(horizontal = 10.dp, vertical = 8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                imageVector = deviceTypeIcon(output.deviceType),
                contentDescription = null,
                tint = connectionColor,
                modifier = Modifier.size(20.dp)
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = output.name,
                    style = MaterialTheme.typography.labelMedium,
                    color = if (output.isMuted) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurface,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = "%s · %s · %+.1f dB · %.1f ms".format(
                        output.deviceType.label,
                        output.connectionState.label,
                        output.effectiveGainDb,
                        output.delayMs
                    ),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (output.isSolo) {
                StatusPill(text = "Solo", color = AmberAccent)
            }
            IconButton(onClick = onMuteToggle) {
                Icon(
                    imageVector = if (output.isMuted) Icons.Filled.VolumeOff else Icons.Filled.VolumeUp,
                    contentDescription = if (output.isMuted) "Activar" else "Silenciar",
                    tint = if (output.isMuted) CoralAlert else CyanGlow
                )
            }
        }
        Slider(
            value = output.volume,
            onValueChange = onVolumeChange,
            colors = SliderDefaults.colors(
                thumbColor = CyanGlow,
                activeTrackColor = CyanPrimary,
                inactiveTrackColor = SurfaceElevated
            )
        )
    }
}

@Composable
private fun OutputDetailSheet(
    output: OutputTarget,
    isTesting: Boolean,
    onTestSignal: (TestSignalType) -> Unit,
    onStopTest: () -> Unit,
    onGainChange: (Float) -> Unit,
    onDelayChange: (Float) -> Unit,
    onMuteToggle: () -> Unit,
    onSoloToggle: () -> Unit,
    onRemove: () -> Unit
) {
    var testType by remember { mutableStateOf(TestSignalType.PINK_NOISE) }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 24.dp, vertical = 16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    output.name,
                    style = MaterialTheme.typography.titleMedium,
                    color = CyanGlow,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    "%s · %s".format(output.deviceType.label, output.connectionState.label),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            StatusPill(
                text = if (output.isMuted) "Mute" else "Activa",
                color = if (output.isMuted) CoralAlert else LimeActive
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            "Ganancia: %+.1f dB".format(output.gainDb),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Slider(
            value = output.gainDb,
            onValueChange = onGainChange,
            valueRange = -12f..12f,
            colors = SliderDefaults.colors(
                thumbColor = CyanGlow,
                activeTrackColor = CyanPrimary,
                inactiveTrackColor = SurfaceElevated
            )
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            "Delay: %.1f ms".format(output.delayMs),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Slider(
            value = output.delayMs,
            onValueChange = onDelayChange,
            valueRange = 0f..2000f,
            colors = SliderDefaults.colors(
                thumbColor = AmberAccent,
                activeTrackColor = AmberAccent.copy(alpha = 0.6f),
                inactiveTrackColor = SurfaceElevated
            )
        )

        Spacer(modifier = Modifier.height(8.dp))

        // Test signal — verify the output actually sounds
        Text(
            "Comprobar que suena",
            style = MaterialTheme.typography.titleSmall,
            color = CyanGlow,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            text = if (output.isMuted) {
                "La salida está silenciada: actívala para comprobarla."
            } else {
                "Reproduce 2 s por la salida respetando volumen, ganancia y delay."
            },
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(6.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            TestSignalType.entries.forEach { signal ->
                FilterChip(
                    selected = testType == signal,
                    onClick = { testType = signal },
                    label = { Text(signal.shortLabel) },
                    enabled = !isTesting,
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = CyanPrimary.copy(alpha = 0.25f),
                        selectedLabelColor = CyanGlow
                    )
                )
            }
        }
        Spacer(modifier = Modifier.height(8.dp))
        if (isTesting) {
            Button(
                onClick = onStopTest,
                modifier = Modifier.fillMaxWidth().height(44.dp),
                shape = RoundedCornerShape(10.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = CoralAlert.copy(alpha = 0.2f),
                    contentColor = CoralAlert
                )
            ) {
                Text("Detener señal")
            }
        } else {
            Button(
                onClick = { onTestSignal(testType) },
                enabled = !output.isMuted,
                modifier = Modifier.fillMaxWidth().height(44.dp),
                shape = RoundedCornerShape(10.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyanPrimary,
                    contentColor = Color.Black
                )
            ) {
                Text("Comprobar (2 s)", fontWeight = FontWeight.SemiBold)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = output.isMuted, onCheckedChange = { onMuteToggle() })
                Spacer(modifier = Modifier.width(8.dp))
                Text("Mute", style = MaterialTheme.typography.labelMedium)
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = output.isSolo, onCheckedChange = { onSoloToggle() })
                Spacer(modifier = Modifier.width(8.dp))
                Text("Solo", style = MaterialTheme.typography.labelMedium)
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        TextButton(
            onClick = onRemove,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Quitar salida", color = CoralAlert)
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}

@Composable
private fun NodeIcon(type: RoutingNodeType, isActive: Boolean) {
    val icon: ImageVector = when (type) {
        RoutingNodeType.INPUT -> Icons.Filled.Mic
        RoutingNodeType.PROCESS -> Icons.Filled.GraphicEq
        RoutingNodeType.OUTPUT -> Icons.Filled.Speaker
        RoutingNodeType.REFERENCE -> Icons.Filled.Waves
    }
    val color = if (isActive) CyanGlow else CyanDim
    Box(
        modifier = Modifier
            .size(28.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(color.copy(alpha = 0.12f)),
        contentAlignment = Alignment.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = color,
            modifier = Modifier.size(16.dp)
        )
    }
}

private fun deviceTypeIcon(type: DeviceType): ImageVector = when (type) {
    DeviceType.PHONE -> Icons.Filled.Speaker
    DeviceType.TABLET -> Icons.Filled.Speaker
    DeviceType.CONSOLE -> Icons.Filled.Tune
    DeviceType.BLUETOOTH_SPEAKER -> Icons.Filled.Speaker
    DeviceType.USB_AUDIO -> Icons.Filled.Usb
    DeviceType.COMPUTER -> Icons.Filled.Computer
    DeviceType.PA_SYSTEM -> Icons.Filled.SpeakerGroup
    DeviceType.MONITOR -> Icons.Filled.Tv
    DeviceType.IN_EARS -> Icons.Filled.Headphones
}

private fun outputsForNode(nodeId: String, outputs: List<OutputTarget>): List<OutputTarget> = when (nodeId) {
    "out_console" -> outputs.filter { it.deviceType == DeviceType.CONSOLE }
    "out_bt" -> outputs.filter { it.deviceType == DeviceType.BLUETOOTH_SPEAKER }
    "out_pa" -> outputs.filter { it.deviceType == DeviceType.PA_SYSTEM }
    "out_inear" -> outputs.filter { it.deviceType == DeviceType.IN_EARS }
    "out_monitor" -> outputs.filter { it.isLocalDevice || it.deviceType == DeviceType.MONITOR }
    else -> emptyList()
}

/** Which output type a routing node spawns when tapped without outputs. */
private fun deviceTypeForNode(nodeId: String): DeviceType = when (nodeId) {
    "out_console" -> DeviceType.CONSOLE
    "out_bt" -> DeviceType.BLUETOOTH_SPEAKER
    "out_pa" -> DeviceType.PA_SYSTEM
    "out_inear" -> DeviceType.IN_EARS
    "out_monitor" -> DeviceType.MONITOR
    else -> DeviceType.MONITOR
}

/**
 * Sheet to create a new output straight from the routing map:
 * pick a type, optionally name it, done.
 */
@Composable
private fun CreateOutputSheet(
    selectedType: DeviceType,
    name: String,
    onTypeChange: (DeviceType) -> Unit,
    onNameChange: (String) -> Unit,
    onCreate: () -> Unit
) {
    val creatableTypes = listOf(
        DeviceType.PA_SYSTEM,
        DeviceType.IN_EARS,
        DeviceType.BLUETOOTH_SPEAKER,
        DeviceType.CONSOLE,
        DeviceType.MONITOR,
        DeviceType.USB_AUDIO,
        DeviceType.PHONE
    )

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 24.dp, vertical = 16.dp)
    ) {
        Text(
            "Nueva salida",
            style = MaterialTheme.typography.titleMedium,
            color = CyanGlow,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            "Elige el tipo de salida y dale un nombre si quieres",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(12.dp))

        creatableTypes.chunked(2).forEach { rowTypes ->
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                rowTypes.forEach { type ->
                    FilterChip(
                        selected = selectedType == type,
                        onClick = { onTypeChange(type) },
                        label = { Text(type.label) },
                        modifier = Modifier.weight(1f),
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = CyanPrimary.copy(alpha = 0.25f),
                            selectedLabelColor = CyanGlow
                        )
                    )
                }
                if (rowTypes.size == 1) {
                    Spacer(modifier = Modifier.weight(1f))
                }
            }
            Spacer(modifier = Modifier.height(6.dp))
        }

        Spacer(modifier = Modifier.height(4.dp))
        OutlinedTextField(
            value = name,
            onValueChange = onNameChange,
            label = { Text("Nombre (opcional)") },
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(10.dp),
            singleLine = true
        )

        Spacer(modifier = Modifier.height(12.dp))

        Button(
            onClick = onCreate,
            modifier = Modifier
                .fillMaxWidth()
                .height(44.dp),
            shape = RoundedCornerShape(10.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = CyanPrimary,
                contentColor = Color.Black
            )
        ) {
            Text("Añadir salida", fontWeight = FontWeight.SemiBold)
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}
