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
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.audio.SweepDirection
import com.rork.acoustical.domain.model.AnalysisInterval
import com.rork.acoustical.domain.model.BandCount
import com.rork.acoustical.domain.model.FftSize
import com.rork.acoustical.domain.model.SampleRate
import com.rork.acoustical.ui.components.ChipSelector
import com.rork.acoustical.ui.components.FineDelayControl
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.service.PhoneSyncManager
import com.rork.acoustical.ui.components.SupportBandRow
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

            // Motor de audio — parámetros generales restaurados
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Motor de audio", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                    Spacer(modifier = Modifier.height(6.dp))
                    Text("Muestreo", style = MaterialTheme.typography.labelMedium, color = CyanGlow)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        SampleRate.entries.forEach { sr ->
                            ChipSelector(sr.label, config.sampleRate == sr) { viewModel.setSampleRate(sr) }
                        }
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text("Tamaño de FFT", style = MaterialTheme.typography.labelMedium, color = CyanGlow)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        FftSize.entries.forEach { fs ->
                            ChipSelector(fs.label, config.fftSize == fs) { viewModel.setFftSize(fs) }
                        }
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text("Intervalo de análisis", style = MaterialTheme.typography.labelMedium, color = CyanGlow)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        AnalysisInterval.entries.forEach { ai ->
                            ChipSelector(ai.label, config.analysisInterval == ai) { viewModel.setAnalysisInterval(ai) }
                        }
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(
                        "Ganancia máxima: %.0f dB".format(config.maxGainDb),
                        style = MaterialTheme.typography.labelSmall,
                        color = CyanGlow
                    )
                    Slider(
                        value = config.maxGainDb,
                        onValueChange = { viewModel.setMaxGainDb(it) },
                        valueRange = 1f..50f
                    )
                    Text(
                        "Suavizado: %.2f".format(config.smoothingFactor),
                        style = MaterialTheme.typography.labelSmall,
                        color = CyanGlow
                    )
                    Slider(
                        value = config.smoothingFactor,
                        onValueChange = { viewModel.setSmoothingFactor(it) },
                        valueRange = 0.05f..0.8f
                    )
                    Text(
                        "Umbral de ruido: %.0f".format(config.noiseFloorDb),
                        style = MaterialTheme.typography.labelSmall,
                        color = CyanGlow
                    )
                    Slider(
                        value = config.noiseFloorDb,
                        onValueChange = { viewModel.setNoiseFloorDb(it) },
                        valueRange = -140f..-60f
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Ecualizadores dinámicos — intro
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Ecualizadores dinámicos", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        "El mismo corrector automático tres veces con distintos ajustes: más control y proceso más rápido. Cada uno con barridos extra y bandas de frecuencia libre.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Una sección específica por cada ecu dinámico
            state.dynamicEqs.forEachIndexed { index, eq ->
                GlassCard(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text(
                                    "Ecu dinámico ${index + 1}",
                                    style = MaterialTheme.typography.titleMedium,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                                Text(
                                    directionLabel(eq.config.startFrom),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Switch(
                                checked = eq.enabled,
                                onCheckedChange = { viewModel.setDynamicEqEnabled(index, it) }
                            )
                        }
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            "Intervalo de decisión: ${eq.config.decisionIntervalMs} ms · los valores se ajustan cada 10 ms",
                            style = MaterialTheme.typography.labelSmall,
                            color = CyanGlow
                        )
                        Slider(
                            value = eq.config.decisionIntervalMs.toFloat(),
                            onValueChange = { v ->
                                viewModel.setDynamicEqConfig(index) { c -> c.copy(decisionIntervalMs = v.toInt()) }
                            },
                            valueRange = 100f..2000f
                        )
                        Text(
                            "Ganancia máxima: %.0f dB".format(eq.config.maxGainDb),
                            style = MaterialTheme.typography.labelSmall,
                            color = CyanGlow
                        )
                        Slider(
                            value = eq.config.maxGainDb,
                            onValueChange = { v ->
                                viewModel.setDynamicEqConfig(index) { c -> c.copy(maxGainDb = v) }
                            },
                            valueRange = 1f..50f
                        )
                        Text(
                            "Mezclador propio: ${(eq.config.mixerLevel * 100).toInt()}%",
                            style = MaterialTheme.typography.labelSmall,
                            color = CyanGlow
                        )
                        Slider(
                            value = eq.config.mixerLevel,
                            onValueChange = { viewModel.setDynamicEqMixerLevel(index, it) },
                            valueRange = 0f..1f
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text("Velocidad del suavizado", style = MaterialTheme.typography.labelMedium, color = CyanGlow)
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(0.5f, 1f, 2f, 4f).forEach { s ->
                                ChipSelector(
                                    label = "×" + if (s < 1f) "0.5" else s.toInt().toString(),
                                    selected = eq.config.speedMultiplier == s
                                ) {
                                    viewModel.setDynamicEqConfig(index) { c -> c.copy(speedMultiplier = s) }
                                }
                            }
                        }
                        Spacer(modifier = Modifier.height(4.dp))
                        Text("Barridos extra por decisión", style = MaterialTheme.typography.labelMedium, color = CyanGlow)
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(0, 1, 2, 3, 4, 6).forEach { n ->
                                ChipSelector(
                                    label = "$n",
                                    selected = eq.config.extraSweeps == n
                                ) {
                                    viewModel.setDynamicEqConfig(index) { c -> c.copy(extraSweeps = n) }
                                }
                            }
                        }
                        Spacer(modifier = Modifier.height(6.dp))
                        Text("Bandas de apoyo (frecuencia libre)", style = MaterialTheme.typography.labelMedium, color = CyanGlow)
                        eq.supportBands.forEachIndexed { bandIndex, band ->
                            SupportBandRow(
                                index = bandIndex,
                                band = band,
                                onChange = { freq, gain ->
                                    viewModel.setDynamicEqSupportBand(index, bandIndex, freq, gain)
                                }
                            )
                        }
                    }
                }
                Spacer(modifier = Modifier.height(8.dp))
            }

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

            Spacer(modifier = Modifier.height(8.dp))

            // PC / Windows — sincronización y actualización automática por USB
            WindowsPcCard()

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

@Composable
private fun WindowsPcCard() {
    val context = LocalContext.current
    val sync = remember(context) { PhoneSyncManager.get(context.applicationContext) }
    val serverRunning by sync.serverRunning.collectAsState()
    val payloadReady by sync.payloadReady.collectAsState()
    val downloading by sync.downloading.collectAsState()
    val syncStatus by sync.status.collectAsState()
    var url by remember { mutableStateOf(sync.windowsPayloadUrl()) }

    GlassCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Text("PC / Windows", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                "Al conectar el móvil por USB, el ordenador sincroniza los ajustes y se actualiza solo: instala la versión nueva sin compilar ni tocar nada.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                if (serverRunning) "Servidor USB activo (puerto 41041)" else "Servidor iniciándose…",
                style = MaterialTheme.typography.labelSmall,
                color = if (serverRunning) CyanGlow else AmberAccent
            )
            Text(
                if (payloadReady) {
                    "Paquete de Windows listo (v${sync.installedPayloadVersion() ?: "?"}) — se instalará al conectar el PC"
                } else {
                    "Sin paquete de Windows: el PC sincronizará ajustes pero no se actualizará"
                },
                style = MaterialTheme.typography.labelSmall,
                color = if (payloadReady) CyanGlow else MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (!payloadReady) {
                Spacer(modifier = Modifier.height(6.dp))
                OutlinedTextField(
                    value = url,
                    onValueChange = { url = it },
                    label = { Text("Enlace del instalador de Windows") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Text(
                    "El enlace debe incluir la versión, p. ej. AcousticalEstudioSetup-1.1.0.exe",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                Button(
                    onClick = { sync.downloadWindowsPayload(url) },
                    enabled = url.isNotBlank() && !downloading
                ) {
                    if (downloading) {
                        CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                    } else {
                        Text("Descargar paquete para Windows")
                    }
                }
            } else {
                Spacer(modifier = Modifier.height(6.dp))
                TextButton(onClick = { sync.clearWindowsPayload() }, enabled = !downloading) {
                    Text("Eliminar paquete")
                }
            }
            if (syncStatus.isNotEmpty()) {
                Spacer(modifier = Modifier.height(4.dp))
                Text(syncStatus, style = MaterialTheme.typography.labelSmall, color = CyanGlow)
            }
        }
    }
}

private fun directionLabel(direction: SweepDirection): String = when (direction) {
    SweepDirection.NEED_BASED -> "Va donde más se necesita"
    SweepDirection.BOTTOM_UP -> "Empieza por los graves"
    SweepDirection.TOP_DOWN -> "Empieza por los agudos"
}
