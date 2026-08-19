package com.rork.acoustical.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.ModalBottomSheet
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.model.BitDepth
import com.rork.acoustical.domain.model.ProbeQuality
import com.rork.acoustical.domain.model.SpatialPosition
import com.rork.acoustical.domain.model.SplCompensation
import com.rork.acoustical.domain.model.StereoMode
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.SpatialJoystick
import com.rork.acoustical.ui.components.SplBadge
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.components.VerticalFader
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
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
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    Box(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 12.dp)
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            // Top bar: stereo mode + SPL
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Stereo mode selector
                LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    items(StereoMode.entries) { mode ->
                        val selected = workConfig.stereoMode == mode
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (selected) CyanPrimary.copy(alpha = 0.15f) else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f))
                                .padding(horizontal = 10.dp, vertical = 6.dp)
                        ) {
                            androidx.compose.material3.TextButton(onClick = { viewModel.setStereoMode(mode) }) {
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

                if (state.isRunning) {
                    SplBadge(spl = state.currentSpl)
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Main control area: joystick left, faders right
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Joystick (left)
                SpatialJoystick(
                    position = spatial,
                    environment = workConfig.environment,
                    onPositionChange = { x, y ->
                        viewModel.updateSpatialPosition(spatial.copy(x = x, y = y))
                    },
                    isActive = state.isRunning
                )

                Spacer(modifier = Modifier.width(12.dp))

                // Two faders stacked (right)
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    VerticalFader(
                        value = spatial.z,
                        label = "Adelante/Atrás",
                        valueLabel = spatial.distanceLabel,
                        onValueChange = { z ->
                            viewModel.updateSpatialPosition(spatial.copy(z = z))
                        },
                        splCompensationDb = compensation.gainAdjustDb,
                        splCompensationActive = compensation.isActive,
                        faderHeight = 200
                    )

                    VerticalFader(
                        value = spatial.size,
                        label = "Dimensionar",
                        valueLabel = spatial.sizeLabel,
                        onValueChange = { size ->
                            viewModel.updateSpatialPosition(spatial.copy(size = size))
                        },
                        splCompensationDb = (0.5f - spatial.size) * 6f,
                        splCompensationActive = compensation.isActive,
                        faderHeight = 200
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

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
                    Icon(
                        imageVector = if (state.isRunning) Icons.Filled.Speed else Icons.Filled.Speed,
                        contentDescription = null
                    )
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
                    Text("Musico")
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
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
