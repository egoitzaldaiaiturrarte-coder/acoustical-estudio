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
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.PhoneAndroid
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.audio.TestSignalType
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.DeviceType
import com.rork.acoustical.domain.model.InputTarget
import com.rork.acoustical.domain.model.InputType
import com.rork.acoustical.domain.model.OutputTarget
import com.rork.acoustical.domain.model.ReferenceSource
import com.rork.acoustical.ui.components.FineDelayControl
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.ProcessorsSection
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
    val inputs = state.inputs
    val outputs = state.outputs
    val context = LocalContext.current

    var selectedOutputId by remember { mutableStateOf<String?>(null) }
    var selectedInputId by remember { mutableStateOf<String?>(null) }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val selectedOutput = outputs.find { it.id == selectedOutputId }
    val selectedInput = inputs.find { it.id == selectedInputId }
    val masterVolume = outputs.firstOrNull()?.volume ?: 0.75f

    // Create-output sheet state — every output can be added from here
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
                text = "Ruteos",
                style = MaterialTheme.typography.headlineMedium,
                color = CyanGlow,
                fontWeight = FontWeight.Medium
            )
            Text(
                text = "Entradas a la izquierda, salidas a la derecha. Toca cualquiera para ajustarlo.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(modifier = Modifier.height(12.dp))

            // === Two connected columns: inputs ⇄ outputs ===
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            "Entradas",
                            style = MaterialTheme.typography.labelMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.weight(1f)
                        )
                        Spacer(modifier = Modifier.width(44.dp))
                        Text(
                            "Salidas",
                            style = MaterialTheme.typography.labelMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.weight(1f)
                        )
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(modifier = Modifier.fillMaxWidth()) {
                        // Inputs column
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            inputs.forEach { input ->
                                InputNodeRow(
                                    input = input,
                                    onClick = { selectedInputId = input.id }
                                )
                            }
                        }

                        // Connection bus: every active input feeds every active output
                        Canvas(
                            modifier = Modifier
                                .width(44.dp)
                                .height(
                                    (maxOf(inputs.size, outputs.size) * 60 - 6)
                                        .coerceAtLeast(0)
                                        .dp
                                )
                        ) {
                            val pitch = 60.dp.toPx()
                            val half = 27.dp.toPx()
                            inputs.forEachIndexed { i, input ->
                                outputs.forEachIndexed { j, output ->
                                    val active = input.isActive && output.isActive && !output.isMuted
                                    drawLine(
                                        color = if (active) CyanPrimary.copy(alpha = 0.55f)
                                        else Color.White.copy(alpha = 0.08f),
                                        start = Offset(0f, i * pitch + half),
                                        end = Offset(size.width, j * pitch + half),
                                        strokeWidth = if (active) 2.5f else 1.5f,
                                        cap = StrokeCap.Round
                                    )
                                }
                            }
                        }

                        // Outputs column
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            if (outputs.isEmpty()) {
                                Text(
                                    text = "Sin salidas. Añade la primera abajo.",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier
                                        .height(54.dp)
                                        .fillMaxWidth()
                                        .clip(RoundedCornerShape(10.dp))
                                        .background(SurfaceElevated.copy(alpha = 0.25f))
                                        .padding(horizontal = 10.dp)
                                        .wrapContentHeight(Alignment.CenterVertically)
                                )
                            } else {
                                outputs.forEach { output ->
                                    OutputNodeRow(
                                        output = output,
                                        onClick = { selectedOutputId = output.id }
                                    )
                                }
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(6.dp))

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

                    outputs.forEach { output ->
                        OutputControlRow(
                            output = output,
                            onClick = { selectedOutputId = output.id },
                            onMuteToggle = { viewModel.toggleOutputMute(output.id) },
                            onVolumeChange = { viewModel.setOutputVolume(output.id, it) }
                        )
                        Spacer(modifier = Modifier.height(6.dp))
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
                            text = "Delay: %.2f ms".format(state.config.audioDelayMs),
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

            // The three automated processors + simultaneous digital inputs
            ProcessorsSection(
                state = state,
                viewModel = viewModel,
                modifier = Modifier.fillMaxWidth()
            )

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

        // Input detail sheet
        if (selectedInput != null) {
            ModalBottomSheet(
                onDismissRequest = { selectedInputId = null },
                sheetState = sheetState,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                InputDetailSheet(
                    input = selectedInput,
                    onToggleActive = { viewModel.toggleInputActive(selectedInput.id) },
                    onGainChange = { viewModel.setInputGainDb(selectedInput.id, it) }
                )
            }
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
                    maxGain = state.config.maxGainDb,
                    isTesting = selectedOutput.id in state.testingOutputIds,
                    onTestSignal = { viewModel.playTestSignal(selectedOutput.id, it) },
                    onStopTest = { viewModel.stopTestSignal(selectedOutput.id) },
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

        // Create output sheet — reached from the add button
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
private fun InputNodeRow(
    input: InputTarget,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .height(54.dp)
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .clickable { onClick() }
            .background(
                if (input.isActive) CyanPrimary.copy(alpha = 0.12f)
                else SurfaceElevated.copy(alpha = 0.25f)
            )
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(
            imageVector = inputTypeIcon(input.type),
            contentDescription = null,
            tint = if (input.isActive) CyanGlow else CyanDim,
            modifier = Modifier.size(18.dp)
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = input.name,
                style = MaterialTheme.typography.labelMedium,
                color = if (input.isActive) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
                fontWeight = FontWeight.SemiBold,
                maxLines = 1
            )
            Text(
                text = input.subtitle,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1
            )
        }
        StatusPill(
            text = if (input.isActive) "Activa" else "Off",
            color = if (input.isActive) LimeActive else MaterialTheme.colorScheme.outline
        )
    }
}

@Composable
private fun OutputNodeRow(
    output: OutputTarget,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .height(54.dp)
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .clickable { onClick() }
            .background(
                when {
                    output.isMuted -> CoralAlert.copy(alpha = 0.12f)
                    output.isActive -> CyanPrimary.copy(alpha = 0.12f)
                    else -> SurfaceElevated.copy(alpha = 0.25f)
                }
            )
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(
            imageVector = deviceTypeIcon(output.deviceType),
            contentDescription = null,
            tint = when {
                output.isMuted -> CoralAlert
                output.isActive -> CyanGlow
                else -> CyanDim
            },
            modifier = Modifier.size(18.dp)
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = output.name,
                style = MaterialTheme.typography.labelMedium,
                color = if (output.isMuted) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurface,
                fontWeight = FontWeight.SemiBold,
                maxLines = 1
            )
            Text(
                text = "%+.1f dB · %.2f ms".format(output.effectiveGainDb, output.delayMs),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1
            )
        }
        StatusPill(
            text = when {
                output.isMuted -> "MUTE"
                output.isActive -> "%d%%".format(output.volumePercent)
                else -> "Off"
            },
            color = when {
                output.isMuted -> CoralAlert
                output.isActive -> CyanGlow
                else -> MaterialTheme.colorScheme.outline
            }
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
                    text = "%s · %s · %+.1f dB · %.2f ms".format(
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
    maxGain: Float,
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
            valueRange = -maxGain..maxGain,
            colors = SliderDefaults.colors(
                thumbColor = CyanGlow,
                activeTrackColor = CyanPrimary,
                inactiveTrackColor = SurfaceElevated
            )
        )

        Spacer(modifier = Modifier.height(8.dp))

        // Fine delay: 0.01 ms steps with time + distance readout
        FineDelayControl(
            label = "Delay por salida (0.01 ms)",
            valueMs = output.delayMs,
            valueRange = 0f..2000f,
            onChange = onDelayChange
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
                "Reproduce 2 s por la salida respetando volumen, ganancia y delay. Varias salidas pueden sonar a la vez."
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
private fun InputDetailSheet(
    input: InputTarget,
    onToggleActive: () -> Unit,
    onGainChange: (Float) -> Unit
) {
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
                    input.name,
                    style = MaterialTheme.typography.titleMedium,
                    color = CyanGlow,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    "%s · %s".format(input.type.label, input.subtitle),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            StatusPill(
                text = if (input.isActive) "Activa" else "Inactiva",
                color = if (input.isActive) LimeActive else MaterialTheme.colorScheme.outline
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            "Nivel de entrada: %+.1f dB".format(input.gainDb),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Slider(
            value = input.gainDb,
            onValueChange = onGainChange,
            valueRange = -12f..12f,
            colors = SliderDefaults.colors(
                thumbColor = CyanGlow,
                activeTrackColor = CyanPrimary,
                inactiveTrackColor = SurfaceElevated
            )
        )

        Spacer(modifier = Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("Entrada activa", style = MaterialTheme.typography.labelMedium)
            Switch(checked = input.isActive, onCheckedChange = { onToggleActive() })
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = inputDescription(input.type),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(24.dp))
    }
}

private fun inputDescription(type: InputType): String = when (type) {
    InputType.MIC -> "El micrófono del móvil mide la sala. Necesita el permiso de micrófono concedido."
    InputType.APP_CAPTURE -> "Captura digital del audio interno de otras apps (Spotify, YouTube…). Pide permiso de proyección la primera vez."
    InputType.EXTERNAL -> "Audio enviado desde otro móvil por la sesión. Actívalo cuando el otro dispositivo esté conectado."
    InputType.USB -> "Interface de audio por USB-OTG. Conéctala y actívala cuando Android la detecte."
    InputType.CONSOLE_IN -> "Entrada OSC de la consola por WiFi. Configura IP y puerto en la sección Consola."
    InputType.FILE_REFERENCE -> "Fuente de señal de referencia: archivo, loop de consola, secuencia o DAW."
}

private fun inputTypeIcon(type: InputType): ImageVector = when (type) {
    InputType.MIC -> Icons.Filled.Mic
    InputType.APP_CAPTURE -> Icons.Filled.GraphicEq
    InputType.EXTERNAL -> Icons.Filled.PhoneAndroid
    InputType.USB -> Icons.Filled.Usb
    InputType.CONSOLE_IN -> Icons.Filled.Tune
    InputType.FILE_REFERENCE -> Icons.Filled.Waves
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

/**
 * Sheet to create a new output straight from the routing screen:
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
