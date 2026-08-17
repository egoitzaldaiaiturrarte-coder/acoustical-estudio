package com.rork.acoustical.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material.icons.filled.NoiseAware
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.model.ScenarioPreset
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.ProgressIndicator
import com.rork.acoustical.ui.components.SpectrumAnalyzer
import com.rork.acoustical.ui.components.SplGauge
import com.rork.acoustical.ui.components.StatBlock
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@Composable
fun DashboardScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    var scenarioExpanded by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 12.dp)
    ) {
        Spacer(modifier = Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            StatusPill(
                text = if (state.isRunning) "Analizando" else "Detenido",
                color = if (state.isRunning) LimeActive else MaterialTheme.colorScheme.outline
            )
            if (state.isRunning && state.isCorrecting) {
                StatusPill(text = "Corrigiendo", color = CyanGlow)
            }
            if (state.rt60Ms > 0f) {
                StatusPill(text = "RT60: %.1fs".format(state.rt60Ms / 1000f), color = AmberAccent)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // SPL Gauge + stats
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "Presión Sonora",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(4.dp))
                SplGauge(
                    spl = state.currentSpl,
                    targetSpl = state.config.targetSpl
                )
                Spacer(modifier = Modifier.height(8.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    StatBlock(
                        label = "PICO",
                        value = if (state.peakSpl > 0) "%.1f".format(state.peakSpl) else "--",
                        valueColor = AmberAccent
                    )
                    StatBlock(
                        label = "PROMEDIO",
                        value = if (state.averageSpl > 0) "%.1f".format(state.averageSpl) else "--",
                        valueColor = CyanPrimary
                    )
                    StatBlock(
                        label = "OBJETIVO",
                        value = "%.0f".format(state.config.targetSpl),
                        valueColor = CyanGlow
                    )
                    StatBlock(
                        label = "RETARDO",
                        value = "%.1fms".format(state.config.audioDelayMs),
                        valueColor = AmberAccent
                    )
                }

                if (state.targetSplReached && state.isRunning) {
                    Spacer(modifier = Modifier.height(4.dp))
                    StatusPill(text = "Objetivo alcanzado", color = LimeActive)
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Start/Stop button
        Button(
            onClick = { viewModel.toggleEngine() },
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = if (state.isRunning) CoralAlert else CyanPrimary,
                contentColor = Color.Black
            )
        ) {
            Icon(
                imageVector = if (state.isRunning) Icons.Filled.Stop else Icons.Filled.PlayArrow,
                contentDescription = if (state.isRunning) "Detener" else "Iniciar"
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(
                text = if (state.isRunning) "Detener análisis" else "Iniciar análisis",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Scenario presets
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Escenario",
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    TextButton(onClick = { scenarioExpanded = !scenarioExpanded }) {
                        Text(
                            text = ScenarioPreset.byId(state.scenarioPreset).name,
                            color = CyanGlow,
                            style = MaterialTheme.typography.labelLarge
                        )
                        Icon(
                            imageVector = if (scenarioExpanded) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore,
                            contentDescription = null,
                            tint = CyanGlow,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }

                AnimatedVisibility(
                    visible = scenarioExpanded,
                    enter = expandVertically(),
                    exit = shrinkVertically()
                ) {
                    Column {
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = ScenarioPreset.byId(state.scenarioPreset).description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        LazyRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            items(ScenarioPreset.all) { preset ->
                                ScenarioChip(
                                    preset = preset,
                                    selected = state.scenarioPreset == preset.id,
                                    onClick = { viewModel.applyScenarioPreset(preset) }
                                )
                            }
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Spectrum analyzer
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Espectro de Frecuencia",
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Icon(
                        imageVector = Icons.Filled.GraphicEq,
                        contentDescription = null,
                        tint = if (state.isRunning) CyanGlow else MaterialTheme.colorScheme.outline
                    )
                }
                Spacer(modifier = Modifier.height(8.dp))
                SpectrumAnalyzer(
                    measured = state.measuredSpectrum,
                    corrected = state.correctedSpectrum,
                    showCorrected = state.isCorrecting
                )
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(RoundedCornerShape(2.dp))
                                .background(AmberAccent)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Medido", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(RoundedCornerShape(2.dp))
                                .background(CyanGlow)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Corregido", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Correction intensity + RT60
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "Corrección",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = "%.0f%%".format(state.correctionIntensity * 100),
                        style = MaterialTheme.typography.titleSmall,
                        color = CyanGlow,
                        fontWeight = FontWeight.SemiBold
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                ProgressIndicator(progress = state.correctionIntensity, barColor = CyanGlow)

                if (state.rt60Ms > 0f) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Tiempo de reverberación (RT60)",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "%.2f s".format(state.rt60Ms / 1000f),
                            style = MaterialTheme.typography.titleSmall,
                            color = AmberAccent,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Geo info card
        if (state.geoInfo.hasFix) {
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = Icons.Filled.LocationOn,
                                contentDescription = null,
                                tint = if (state.geoAutoAdjust) LimeActive else MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = state.geoInfo.label,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        if (state.geoAutoAdjust) {
                            StatusPill(text = "Auto", color = LimeActive)
                        }
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Altitud: %.0f m".format(state.geoInfo.altitude),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "v_sonido: %.0f m/s".format(state.geoInfo.speedOfSound),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = if (state.geoInfo.isOutdoor) "Exterior" else "Interior",
                            style = MaterialTheme.typography.labelSmall,
                            color = if (state.geoInfo.isOutdoor) AmberAccent else CyanGlow
                        )
                    }
                    if (state.geoAdjustmentApplied != 0f) {
                        Spacer(modifier = Modifier.height(2.dp))
                        Text(
                            text = "Corrección por altitud: %+.1f dB".format(state.geoAdjustmentApplied),
                            style = MaterialTheme.typography.labelSmall,
                            color = AmberAccent
                        )
                    }
                }
            }
            Spacer(modifier = Modifier.height(8.dp))
        }

        // Noise profile card
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Filled.NoiseAware,
                            contentDescription = null,
                            tint = if (state.hasNoiseProfile) LimeActive else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "Ruido de fondo",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    if (state.hasNoiseProfile) {
                        StatusPill(text = "Activo", color = LimeActive)
                    }
                }

                if (state.isNoiseCapturing) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Capturando... %.0f%%".format(state.noiseCaptureProgress * 100),
                        style = MaterialTheme.typography.bodySmall,
                        color = CyanGlow
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    ProgressIndicator(progress = state.noiseCaptureProgress, barColor = LimeActive)
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = { viewModel.cancelNoiseCapture() },
                        modifier = Modifier.fillMaxWidth().height(40.dp),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text("Cancelar", color = CoralAlert)
                    }
                } else {
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { viewModel.startNoiseCapture() },
                            modifier = Modifier.weight(1f).height(40.dp),
                            shape = RoundedCornerShape(10.dp),
                            enabled = state.isRunning
                        ) {
                            Text(
                                if (state.hasNoiseProfile) "Recapturar" else "Capturar ruido",
                                color = CyanPrimary,
                                style = MaterialTheme.typography.labelMedium
                            )
                        }
                        if (state.hasNoiseProfile) {
                            OutlinedButton(
                                onClick = { viewModel.clearNoiseProfile() },
                                modifier = Modifier.weight(1f).height(40.dp),
                                shape = RoundedCornerShape(10.dp)
                            ) {
                                Text("Limpiar", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.labelMedium)
                            }
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Quick nav buttons
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedButton(
                onClick = { navController.navigate("equalizer") },
                modifier = Modifier.weight(1f).height(44.dp),
                shape = RoundedCornerShape(12.dp)
            ) {
                Icon(imageVector = Icons.Filled.Tune, contentDescription = null, tint = CyanPrimary, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(4.dp))
                Text("EQ", color = CyanPrimary, style = MaterialTheme.typography.labelLarge)
            }
            OutlinedButton(
                onClick = { navController.navigate("calibration") },
                modifier = Modifier.weight(1f).height(44.dp),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text("Calibración", color = CyanPrimary, style = MaterialTheme.typography.labelLarge)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Engine info
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "Motor",
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(4.dp))
                EngineInfoRow("Sample Rate", state.config.sampleRate.label)
                EngineInfoRow("FFT", state.config.fftSize.label)
                EngineInfoRow("Intervalo", state.config.analysisInterval.label)
                EngineInfoRow("Bandas", state.config.bandCount.label)
                EngineInfoRow("Retardo", "%.1f ms".format(state.config.audioDelayMs))
                EngineInfoRow("Frames", "${state.framesAnalyzed}")
            }
        }

        Spacer(modifier = Modifier.height(16.dp))
    }
}

@Composable
private fun EngineInfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurface)
    }
}

@Composable
private fun ScenarioChip(
    preset: ScenarioPreset,
    selected: Boolean,
    onClick: () -> Unit
) {
    val bg = if (selected) CyanPrimary.copy(alpha = 0.15f) else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
    val fg = if (selected) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(10.dp))
            .background(bg)
            .padding(horizontal = 12.dp, vertical = 8.dp)
    ) {
        TextButton(onClick = onClick) {
            Text(preset.name, color = fg, style = MaterialTheme.typography.labelMedium, fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal)
        }
    }
}
