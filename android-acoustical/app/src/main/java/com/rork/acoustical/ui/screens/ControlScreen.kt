package com.rork.acoustical.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.CenterFocusStrong
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Psychology
import androidx.compose.material.icons.filled.RecordVoiceOver
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Straighten
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.model.AgentAdvice
import com.rork.acoustical.domain.model.AgentMode
import com.rork.acoustical.domain.model.AgentSeverity
import com.rork.acoustical.domain.model.BitDepth
import com.rork.acoustical.domain.model.DistanceStep
import com.rork.acoustical.domain.model.PanZone
import com.rork.acoustical.domain.model.ProbeQuality
import com.rork.acoustical.domain.model.SpatialPosition
import com.rork.acoustical.ui.components.DpadDirection
import com.rork.acoustical.ui.components.DirectionalPad
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.SplBadge
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.components.VerticalFader
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
fun ControlScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val spatial = state.spatialPosition
    val workConfig = state.workConfig
    val compensation = state.splCompensation

    var showAutoCheck by remember { mutableStateOf(false) }
    var showDistanceTool by remember { mutableStateOf(false) }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

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
            Spacer(modifier = Modifier.height(12.dp))

            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "Control",
                    style = MaterialTheme.typography.headlineSmall,
                    color = CyanGlow,
                    fontWeight = FontWeight.Bold
                )
                if (state.isRunning) {
                    SplBadge(spl = state.currentSpl)
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Engine start failure — visible reason instead of a silently dead motor
            state.engineError?.let { error ->
                Text(
                    text = error,
                    style = MaterialTheme.typography.bodySmall,
                    color = CoralAlert,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(CoralAlert.copy(alpha = 0.15f))
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                )
                Spacer(modifier = Modifier.height(8.dp))
            }

            // Agent mode selector (4 modes)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                AgentMode.entries.forEach { mode ->
                    val selected = state.agentMode == mode
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .clip(RoundedCornerShape(10.dp))
                            .background(
                                if (selected) CyanPrimary.copy(alpha = 0.25f)
                                else SurfaceElevated.copy(alpha = 0.5f)
                            )
                            .clickable { viewModel.setAgentMode(mode) }
                            .padding(vertical = 8.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(
                                Icons.Filled.Psychology,
                                contentDescription = null,
                                tint = if (selected) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(16.dp)
                            )
                            Text(
                                mode.label,
                                style = MaterialTheme.typography.labelSmall,
                                color = if (selected) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant,
                                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Agent advice panel
            if (state.agentAdvices.isNotEmpty()) {
                AgentAdvicePanel(
                    advices = state.agentAdvices.take(4),
                    isOnDemand = state.agentMode == AgentMode.ON_DEMAND,
                    showExplanations = state.agentMode == AgentMode.MASTER,
                    onConsult = { viewModel.consultAgent() },
                    onDismiss = { viewModel.clearAgentAdvices() }
                )
                Spacer(modifier = Modifier.height(10.dp))
            }

            // Sweep modifiers — affect every action on this device
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                SweepToggle(
                    label = "Barrido rápido",
                    icon = Icons.Filled.Bolt,
                    active = state.isFastSweep,
                    modifier = Modifier.weight(1f),
                    onToggle = { viewModel.setFastSweep(it) }
                )
                SweepToggle(
                    label = "Barrido fino",
                    icon = Icons.Filled.Tune,
                    active = state.isFineSweep,
                    modifier = Modifier.weight(1f),
                    onToggle = { viewModel.setFineSweep(it) }
                )
            }
            Text(
                text = "Paso activo: ${state.sweepLabel} — afecta a botones, faders, paneo y distancias",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )

            Spacer(modifier = Modifier.height(10.dp))

            // Main control area: D-pad left, faders right
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                DirectionalPad(
                    onDirection = { dir ->
                        when (dir) {
                            DpadDirection.UP -> viewModel.nudgeSpatial(dxSteps = 0, dySteps = 1)
                            DpadDirection.DOWN -> viewModel.nudgeSpatial(dxSteps = 0, dySteps = -1)
                            DpadDirection.LEFT -> viewModel.nudgeSpatial(dxSteps = -1, dySteps = 0)
                            DpadDirection.RIGHT -> viewModel.nudgeSpatial(dxSteps = 1, dySteps = 0)
                        }
                    },
                    isActive = true,
                    stepLabel = "Espacio 2D"
                )

                Spacer(modifier = Modifier.width(8.dp))

                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    VerticalFader(
                        value = spatial.z,
                        label = "Traer aquí / llevar",
                        valueLabel = spatial.distanceLabel,
                        onValueChange = { z ->
                            viewModel.updateSpatialPosition(spatial.copy(z = z))
                        },
                        splCompensationDb = compensation.gainAdjustDb,
                        splCompensationActive = compensation.isActive,
                        faderHeight = 170
                    )

                    VerticalFader(
                        value = spatial.size,
                        label = "Tamaño equipo",
                        valueLabel = spatial.sizeLabel,
                        onValueChange = { size ->
                            viewModel.updateSpatialPosition(spatial.copy(size = size))
                        },
                        splCompensationDb = (0.5f - spatial.size) * 6f,
                        splCompensationActive = compensation.isActive,
                        faderHeight = 170
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Pan matrix: L / Mid / R / Lados — one linked send
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                "Envíos L · Mid · R · Lados",
                                style = MaterialTheme.typography.titleSmall,
                                color = MaterialTheme.colorScheme.onSurface,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                "Un solo envío: se auto-corrigen entre sí",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        androidx.compose.material3.IconButton(onClick = { viewModel.resetPan() }) {
                            Icon(
                                Icons.Filled.CenterFocusStrong,
                                contentDescription = "Centrar paneo",
                                tint = CyanPrimary,
                                modifier = Modifier.size(20.dp)
                            )
                        }
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        PanZone.entries.forEach { zone ->
                            PanZoneControl(
                                zone = zone,
                                weight = state.panMatrix.weight(zone),
                                onUp = { viewModel.adjustPanZone(zone, up = true) },
                                onDown = { viewModel.adjustPanZone(zone, up = false) },
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Distance measurement card
            val dm = state.distanceMeasure
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                "Distancia emisor → receptor",
                                style = MaterialTheme.typography.titleSmall,
                                color = MaterialTheme.colorScheme.onSurface,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = when {
                                    dm.effectiveDistanceM != null ->
                                        "%.2f m · delay %.1f ms · %+.1f dB".format(
                                            dm.effectiveDistanceM!!, dm.delayMs ?: 0f, dm.gainCompensationDb ?: 0f
                                        )
                                    else -> "Mide con GPS para automatizar delay y ganancia"
                                },
                                style = MaterialTheme.typography.labelSmall,
                                color = if (dm.effectiveDistanceM != null) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (dm.step == DistanceStep.DONE) {
                            StatusPill(text = "OK", color = LimeActive)
                        }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Button(
                        onClick = { showDistanceTool = true },
                        modifier = Modifier.fillMaxWidth().height(44.dp),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyanPrimary.copy(alpha = 0.2f),
                            contentColor = CyanGlow
                        )
                    ) {
                        Icon(Icons.Filled.Straighten, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("Medir distancias", fontWeight = FontWeight.SemiBold)
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Walkie-talkie card
            WalkieCard(
                peers = state.meshPeers,
                targets = state.walkieTargets,
                isActive = state.walkieActive,
                onToggleTarget = { viewModel.toggleWalkieTarget(it) },
                onSetActive = { viewModel.setWalkieActive(it) }
            )

            Spacer(modifier = Modifier.height(10.dp))

            // Compensation info
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "Compensación SPL",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSurface,
                            fontWeight = FontWeight.Medium
                        )
                        Text(
                            text = "Ganancia: %+.1f dB · Delay: %.1f ms".format(
                                compensation.gainAdjustDb,
                                compensation.delayAdjustMs
                            ),
                            style = MaterialTheme.typography.labelMedium,
                            color = if (compensation.isActive) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    StatusPill(
                        text = if (compensation.isActive) "Activa" else "Off",
                        color = if (compensation.isActive) LimeActive else MaterialTheme.colorScheme.outline
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Target SPL + position info
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                GlassCard(modifier = Modifier.weight(1f)) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            text = "SPL Objetivo",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "%.0f dB".format(state.config.targetSpl),
                            style = MaterialTheme.typography.titleLarge,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
                GlassCard(modifier = Modifier.weight(1f)) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            text = "Posición",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = spatial.positionLabel(workConfig.environment),
                            style = MaterialTheme.typography.titleSmall,
                            color = CyanGlow,
                            fontWeight = FontWeight.Medium
                        )
                    }
                }
                GlassCard(modifier = Modifier.weight(1f)) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            text = "RT60",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = if (state.rt60Ms > 0) "%.2fs".format(state.rt60Ms / 1000f) else "--",
                            style = MaterialTheme.typography.titleLarge,
                            color = AmberAccent,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Engine start/stop + auto-check
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = { viewModel.toggleEngine() },
                    modifier = Modifier.weight(1f).height(48.dp),
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (state.isRunning) CoralAlert else CyanPrimary,
                        contentColor = Color.Black
                    )
                ) {
                    Icon(Icons.Filled.Speed, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        if (state.isRunning) "Detener" else "Iniciar",
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold
                    )
                }
                Button(
                    onClick = { showAutoCheck = true },
                    modifier = Modifier.weight(1f).height(48.dp),
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.secondaryContainer,
                        contentColor = AmberAccent
                    )
                ) {
                    Icon(Icons.Filled.AutoFixHigh, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        "Auto-chequeo",
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Navigation to output mix and musician mode
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = { navController.navigate("output_mix") },
                    modifier = Modifier.weight(1f).height(44.dp),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Icon(Icons.Filled.Speaker, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Salidas")
                }
                OutlinedButton(
                    onClick = { navController.navigate("musician") },
                    modifier = Modifier.weight(1f).height(44.dp),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Icon(Icons.Filled.Person, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Músico")
                }
            }

            Spacer(modifier = Modifier.height(24.dp))
        }

        // Auto-check bottom sheet
        if (showAutoCheck) {
            ModalBottomSheet(
                onDismissRequest = { showAutoCheck = false },
                sheetState = sheetState,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                AutoCheckPanel(
                    config = state.workConfig.autoCheck,
                    onIntervalChange = { viewModel.setAutoCheckInterval(it) },
                    onQualityChange = { viewModel.setAutoCheckQuality(it) },
                    onBitDepthChange = { viewModel.setAutoCheckBitDepth(it) },
                    onToggle = { viewModel.setAutoCheckEnabled(it) }
                )
            }
        }

        // Distance measurement bottom sheet
        if (showDistanceTool) {
            ModalBottomSheet(
                onDismissRequest = { showDistanceTool = false },
                sheetState = sheetState,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                DistanceMeasurePanel(
                    measure = state.distanceMeasure,
                    onCapture = { viewModel.captureDistancePoint() },
                    onAdjust = { coarse, up -> viewModel.adjustMeasuredDistance(coarse, up) },
                    onApply = { viewModel.applyDistanceAutomation() },
                    onReset = { viewModel.resetDistanceMeasure() }
                )
            }
        }
    }
}

@Composable
private fun AgentAdvicePanel(
    advices: List<AgentAdvice>,
    isOnDemand: Boolean,
    showExplanations: Boolean,
    onConsult: () -> Unit,
    onDismiss: () -> Unit
) {
    GlassCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "Agente ingeniero",
                    style = MaterialTheme.typography.titleSmall,
                    color = CyanGlow,
                    fontWeight = FontWeight.SemiBold
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (isOnDemand) {
                        androidx.compose.material3.TextButton(onClick = onConsult) {
                            Text("Consultar", color = CyanPrimary)
                        }
                    }
                    androidx.compose.material3.TextButton(onClick = onDismiss) {
                        Text("OK", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
            Spacer(modifier = Modifier.height(4.dp))
            advices.forEach { advice ->
                val color = when (advice.severity) {
                    AgentSeverity.INFO -> CyanDim
                    AgentSeverity.WARN -> AmberAccent
                    AgentSeverity.BLOCK -> CoralAlert
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp),
                    verticalAlignment = Alignment.Top
                ) {
                    Box(
                        modifier = Modifier
                            .padding(top = 5.dp)
                            .size(8.dp)
                            .background(color, CircleShape)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Column {
                        Text(
                            advice.title,
                            style = MaterialTheme.typography.labelMedium,
                            color = color,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            advice.message,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        if (advice.suggestion.isNotEmpty()) {
                            Text(
                                advice.suggestion,
                                style = MaterialTheme.typography.labelSmall,
                                color = CyanPrimary
                            )
                        }
                        if (showExplanations && advice.explanation.isNotEmpty()) {
                            Text(
                                advice.explanation,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.outline,
                                fontStyle = androidx.compose.ui.text.font.FontStyle.Italic
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SweepToggle(
    label: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    active: Boolean,
    modifier: Modifier = Modifier,
    onToggle: (Boolean) -> Unit
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(10.dp))
            .background(if (active) AmberAccent.copy(alpha = 0.25f) else SurfaceElevated.copy(alpha = 0.5f))
            .clickable { onToggle(!active) }
            .padding(horizontal = 12.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(
                icon,
                contentDescription = null,
                tint = if (active) AmberAccent else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(16.dp)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                label,
                style = MaterialTheme.typography.labelSmall,
                color = if (active) AmberAccent else MaterialTheme.colorScheme.onSurfaceVariant,
                fontWeight = if (active) FontWeight.SemiBold else FontWeight.Normal
            )
        }
    }
}

@Composable
private fun PanZoneControl(
    zone: PanZone,
    weight: Float,
    onUp: () -> Unit,
    onDown: () -> Unit,
    modifier: Modifier = Modifier
) {
    val active = weight > 0.01f
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            zone.short,
            style = MaterialTheme.typography.labelMedium,
            color = if (active) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            "%d%%".format((weight * 100f).toInt()),
            style = MaterialTheme.typography.labelSmall,
            color = if (active) LimeActive else MaterialTheme.colorScheme.outline
        )
        Spacer(modifier = Modifier.height(4.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            MiniAdjustButton(icon = Icons.Filled.Remove, onClick = onDown)
            MiniAdjustButton(icon = Icons.Filled.Add, onClick = onUp)
        }
    }
}

@Composable
private fun MiniAdjustButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    onClick: () -> Unit
) {
    Box(
        modifier = Modifier
            .size(30.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(SurfaceElevated)
            .clickable { onClick() },
        contentAlignment = Alignment.Center
    ) {
        Icon(
            icon,
            contentDescription = null,
            tint = CyanGlow,
            modifier = Modifier.size(16.dp)
        )
    }
}

@Composable
private fun WalkieCard(
    peers: List<com.rork.acoustical.domain.console.MeshPeer>,
    targets: Set<String>,
    isActive: Boolean,
    onToggleTarget: (String) -> Unit,
    onSetActive: (Boolean) -> Unit
) {
    GlassCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "Walkie del flujo",
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    fontWeight = FontWeight.SemiBold
                )
                StatusPill(
                    text = if (isActive) "Hablando" else if (targets.isNotEmpty()) "Listo" else "Sin destino",
                    color = when {
                        isActive -> CoralAlert
                        targets.isNotEmpty() -> LimeActive
                        else -> MaterialTheme.colorScheme.outline
                    }
                )
            }
            Spacer(modifier = Modifier.height(6.dp))

            if (peers.isEmpty()) {
                Text(
                    "Sin compañeros en la sesión. Crea o únete a una sesión para hablar con ellos.",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                peers.take(4).forEach { peer ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onToggleTarget(peer.id) },
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Checkbox(
                            checked = peer.id in targets,
                            onCheckedChange = { onToggleTarget(peer.id) }
                        )
                        Text(
                            peer.deviceName,
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Press-to-talk button
            var pttPressed by remember { mutableStateOf(false) }
            val enabled = targets.isNotEmpty()
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(52.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(
                        when {
                            pttPressed && enabled -> CoralAlert
                            enabled -> CyanPrimary.copy(alpha = 0.25f)
                            else -> SurfaceElevated
                        }
                    )
                    .pointerInput(enabled) {
                        if (!enabled) return@pointerInput
                        detectTapGestures(
                            onPress = {
                                pttPressed = true
                                onSetActive(true)
                                try {
                                    awaitRelease()
                                } finally {
                                    pttPressed = false
                                    onSetActive(false)
                                }
                            }
                        )
                    },
                contentAlignment = Alignment.Center
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        Icons.Filled.RecordVoiceOver,
                        contentDescription = null,
                        tint = if (!enabled) MaterialTheme.colorScheme.outline else if (pttPressed) Color.Black else CyanGlow,
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = when {
                            !enabled -> "Elige quién te escucha"
                            pttPressed -> "Hablando..."
                            else -> "Mantén pulsado para hablar"
                        },
                        style = MaterialTheme.typography.labelLarge,
                        color = if (!enabled) MaterialTheme.colorScheme.outline else if (pttPressed) Color.Black else CyanGlow,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
    }
}

@Composable
private fun DistanceMeasurePanel(
    measure: com.rork.acoustical.domain.model.DistanceMeasurement,
    onCapture: () -> Unit,
    onAdjust: (coarse: Boolean, up: Boolean) -> Unit,
    onApply: () -> Unit,
    onReset: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp, vertical = 16.dp)
    ) {
        Text(
            "Medición emisor → receptor",
            style = MaterialTheme.typography.titleMedium,
            color = CyanGlow,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            "Un toque en el emisor, otro en el punto de chequeo. El GPS propone el sitio exacto.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(12.dp))

        // Step indicator
        Text(
            text = when (measure.step) {
                DistanceStep.EMITTER -> "1 · Toca para capturar el EMISOR"
                DistanceStep.RECEIVER -> "2 · Toca para capturar el RECEPTOR"
                DistanceStep.DONE -> "3 · Ajusta y aplica la automatización"
            },
            style = MaterialTheme.typography.labelMedium,
            color = AmberAccent,
            fontWeight = FontWeight.SemiBold
        )

        Spacer(modifier = Modifier.height(8.dp))

        Button(
            onClick = onCapture,
            modifier = Modifier.fillMaxWidth().height(48.dp),
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = CyanPrimary,
                contentColor = Color.Black
            )
        ) {
            Text(
                when (measure.step) {
                    DistanceStep.EMITTER -> "Capturar emisor (GPS)"
                    DistanceStep.RECEIVER -> "Capturar receptor (GPS)"
                    DistanceStep.DONE -> "Nueva medición"
                },
                fontWeight = FontWeight.SemiBold
            )
        }

        Spacer(modifier = Modifier.height(12.dp))

        // Captured points
        measure.emitter?.let { e ->
            Text(
                "Emisor: %.5f, %.5f · %.1fm".format(e.latitude, e.longitude, e.altitude),
                style = MaterialTheme.typography.labelSmall,
                color = CyanGlow
            )
        }
        measure.receiver?.let { r ->
            Text(
                "Receptor: %.5f, %.5f · %.1fm".format(r.latitude, r.longitude, r.altitude),
                style = MaterialTheme.typography.labelSmall,
                color = CyanGlow
            )
        }

        val distance = measure.effectiveDistanceM
        if (distance != null) {
            Spacer(modifier = Modifier.height(12.dp))

            // Results with decimals
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                GlassCard(modifier = Modifier.weight(1f)) {
                    Column {
                        Text("Distancia", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            "%.2f m".format(distance),
                            style = MaterialTheme.typography.titleLarge,
                            color = CyanGlow,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
                GlassCard(modifier = Modifier.weight(1f)) {
                    Column {
                        Text("Delay", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            "%.1f ms".format(measure.delayMs ?: 0f),
                            style = MaterialTheme.typography.titleLarge,
                            color = AmberAccent,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
                GlassCard(modifier = Modifier.weight(1f)) {
                    Column {
                        Text("Ganancia", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            "%+.1f dB".format(measure.gainCompensationDb ?: 0f),
                            style = MaterialTheme.typography.titleLarge,
                            color = LimeActive,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // Scale button: held = coarse, released = fine
            var coarseHeld by remember { mutableStateOf(false) }
            Text(
                "Escala: " + if (coarseHeld) "GRANDE (0.5 m por toque)" else "FINA (0.01 m por toque)",
                style = MaterialTheme.typography.labelMedium,
                color = if (coarseHeld) AmberAccent else CyanGlow,
                fontWeight = FontWeight.SemiBold
            )
            Spacer(modifier = Modifier.height(8.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(48.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(if (coarseHeld) AmberAccent else SurfaceElevated)
                        .pointerInput(Unit) {
                            detectTapGestures(
                                onPress = {
                                    coarseHeld = true
                                    try {
                                        awaitRelease()
                                    } finally {
                                        coarseHeld = false
                                    }
                                }
                            )
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        "ESCALA",
                        style = MaterialTheme.typography.labelLarge,
                        color = if (coarseHeld) Color.Black else CyanGlow,
                        fontWeight = FontWeight.Bold
                    )
                }
                MiniAdjustButton(icon = Icons.Filled.Remove) { onAdjust(coarseHeld, false) }
                MiniAdjustButton(icon = Icons.Filled.Add) { onAdjust(coarseHeld, true) }
            }

            Spacer(modifier = Modifier.height(14.dp))

            Button(
                onClick = onApply,
                modifier = Modifier.fillMaxWidth().height(48.dp),
                shape = RoundedCornerShape(12.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyanPrimary,
                    contentColor = Color.Black
                )
            ) {
                Text("Aplicar automatización", fontWeight = FontWeight.SemiBold)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        androidx.compose.material3.TextButton(
            onClick = onReset,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Reiniciar medición", color = CoralAlert)
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AutoCheckPanel(
    config: com.rork.acoustical.domain.model.AutoCheckConfig,
    onIntervalChange: (Int) -> Unit,
    onQualityChange: (ProbeQuality) -> Unit,
    onBitDepthChange: (BitDepth) -> Unit,
    onToggle: (Boolean) -> Unit
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
            Text(
                text = "Auto-chequeo de sondas",
                style = MaterialTheme.typography.titleMedium,
                color = CyanGlow,
                fontWeight = FontWeight.SemiBold
            )
            androidx.compose.material3.Switch(
                checked = config.enabled,
                onCheckedChange = onToggle
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Intervalo: cada ${config.intervalSeconds}s",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(4.dp))
        androidx.compose.material3.Slider(
            value = config.intervalSeconds.toFloat(),
            onValueChange = { onIntervalChange(it.toInt()) },
            valueRange = 10f..600f,
            steps = 58,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(12.dp))

        Text(
            text = "Calidad",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(6.dp))
        LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(ProbeQuality.entries) { q ->
                ChipSelector(
                    text = q.label,
                    selected = config.quality == q,
                    onClick = { onQualityChange(q) }
                )
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        Text(
            text = "Profundidad de bits",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(6.dp))
        LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(BitDepth.entries) { bd ->
                ChipSelector(
                    text = bd.label,
                    selected = config.bitDepth == bd,
                    onClick = { onBitDepthChange(bd) }
                )
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}
