package com.rork.acoustical.ui.screens

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.model.AnalysisInterval
import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.FftSize
import com.rork.acoustical.domain.model.SampleRate
import com.rork.acoustical.ui.components.ChipSelector
import com.rork.acoustical.ui.components.FineDelayControl
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val config = state.config

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Ajustes", color = MaterialTheme.colorScheme.onSurface) },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Volver",
                            tint = CyanPrimary
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface
                )
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp)
        ) {
            Spacer(modifier = Modifier.height(8.dp))

            // Motor — valores fijos: todo lo automático ya no se toca
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Motor — valores fijos", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                    Spacer(modifier = Modifier.height(4.dp))
                    Text("Muestreo: 96 kHz fijo · Nyquist 48 kHz", style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                    Text("Umbral de ruido: 120 (fijo, sin control)", style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                    Text("Barrido y suavizado: automáticos por frecuencia", style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                    Text("Procesos, mezcladores y bandas de apoyo: en Ruteos", style = MaterialTheme.typography.labelSmall, color = AmberAccent)
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Band Count
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Bandas de ecualización", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                    Spacer(modifier = Modifier.height(2.dp))
                    Text("Más bandas = corrección más precisa.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        BandCount.entries.forEach { bc ->
                            ChipSelector(bc.label, config.bandCount == bc) { viewModel.setBandCount(bc) }
                        }
                    }
                    Spacer(modifier = Modifier.height(2.dp))
                    Text("${config.bandCount.count} bandas de corrección", style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Audio Delay with decimals
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Retardo de audio", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                    Spacer(modifier = Modifier.height(2.dp))
                    Text("Sincroniza múltiples dispositivos con precisión decimal.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(modifier = Modifier.height(6.dp))
                    FineDelayControl(
                        label = "Retardo global",
                        valueMs = config.audioDelayMs,
                        valueRange = 25f..2000f,
                        onChange = { viewModel.setAudioDelayMs(it) }
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Geolocation auto-adjust
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
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.padding(4.dp))
                            Column {
                                Text("Ajuste por geolocalización", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                                Text("GPS ajusta SPL y retardo automáticamente", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                        Switch(
                            checked = state.geoAutoAdjust,
                            onCheckedChange = { viewModel.setGeoAutoAdjust(it) }
                        )
                    }
                    if (state.geoInfo.hasFix) {
                        Spacer(modifier = Modifier.height(6.dp))
                        Text("Ubicación: ${state.geoInfo.label}", style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                        Text("Altitud: %.0f m · v_sonido: %.0f m/s".format(state.geoInfo.altitude, state.geoInfo.speedOfSound), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text("SPL recomendado: %.0f dB · Retardo: %.1f ms".format(state.geoInfo.recommendedSpl, state.geoInfo.recommendedDelayMs), style = MaterialTheme.typography.labelSmall, color = CyanGlow)
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Noise Subtraction
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Sustracción de ruido", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                    Spacer(modifier = Modifier.height(2.dp))
                    Text("Resta el perfil de ruido capturado del espectro medido.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Activar sustracción", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurface)
                        Switch(
                            checked = config.noiseSubtractionEnabled,
                            onCheckedChange = { viewModel.setNoiseSubtractionEnabled(it) }
                        )
                    }
                    if (!state.hasNoiseProfile) {
                        Spacer(modifier = Modifier.height(2.dp))
                        Text("Captura un perfil de ruido desde Inicio.", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

