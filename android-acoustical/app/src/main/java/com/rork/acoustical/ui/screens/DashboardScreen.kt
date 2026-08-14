package com.rork.acoustical.ui.screens

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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.ProgressIndicator
import com.rork.acoustical.ui.components.SpectrumAnalyzer
import com.rork.acoustical.ui.components.SplGauge
import com.rork.acoustical.ui.components.StatBlock
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

/**
 * Main dashboard screen — shows real-time SPL, spectrum analyzer,
 * engine controls, and quick stats.
 */
@Composable
fun DashboardScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        // Engine status + start/stop
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
                StatusPill(
                    text = "Corrigiendo",
                    color = CyanGlow
                )
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        // SPL Gauge + stats
        GlassCard(
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "Presión Sonora",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(8.dp))
                SplGauge(
                    spl = state.currentSpl,
                    targetSpl = state.config.targetSpl
                )
                Spacer(modifier = Modifier.height(12.dp))

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
                }

                if (state.targetSplReached && state.isRunning) {
                    Spacer(modifier = Modifier.height(8.dp))
                    StatusPill(
                        text = "Objetivo alcanzado",
                        color = LimeActive
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Start/Stop button
        Button(
            onClick = { viewModel.toggleEngine() },
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp),
            shape = RoundedCornerShape(16.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = if (state.isRunning) MaterialTheme.colorScheme.error else CyanPrimary,
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

        Spacer(modifier = Modifier.height(20.dp))

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
                Spacer(modifier = Modifier.height(12.dp))
                SpectrumAnalyzer(
                    measured = state.measuredSpectrum,
                    corrected = state.correctedSpectrum,
                    showCorrected = state.isCorrecting
                )
                Spacer(modifier = Modifier.height(8.dp))
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
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "Medido",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(RoundedCornerShape(2.dp))
                                .background(CyanGlow)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "Corregido",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Correction intensity bar
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "Intensidad de corrección",
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
                Spacer(modifier = Modifier.height(8.dp))
                ProgressIndicator(
                    progress = state.correctionIntensity,
                    barColor = CyanGlow
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Quick nav buttons
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedButton(
                onClick = { navController.navigate("equalizer") },
                modifier = Modifier.weight(1f).height(52.dp),
                shape = RoundedCornerShape(14.dp)
            ) {
                Icon(imageVector = Icons.Filled.Tune, contentDescription = null, tint = CyanPrimary)
                Spacer(modifier = Modifier.width(6.dp))
                Text("Ecualizador", color = CyanPrimary)
            }
            OutlinedButton(
                onClick = { navController.navigate("calibration") },
                modifier = Modifier.weight(1f).height(52.dp),
                shape = RoundedCornerShape(14.dp)
            ) {
                Text("Calibración", color = CyanPrimary)
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Engine info
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "Información del motor",
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Sample Rate", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(state.config.sampleRate.label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurface)
                }
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("FFT", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(state.config.fftSize.label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurface)
                }
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Intervalo", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(state.config.analysisInterval.label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurface)
                }
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Frames analizados", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text("${state.framesAnalyzed}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurface)
                }
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}
