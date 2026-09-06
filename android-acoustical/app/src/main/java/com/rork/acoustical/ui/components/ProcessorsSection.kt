package com.rork.acoustical.ui.components

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.rork.acoustical.domain.model.ProbeQuality
import com.rork.acoustical.domain.model.SupportBand
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel
import kotlin.math.log10
import kotlin.math.pow

/**
 * The three automated processors, managed from Ruteos. All three are the same
 * free-frequency automatic corrector — a decision every 800 ms while the gain
 * values adjust every 10 ms — differing only in where they work:
 * 1. Auto ayuda — goes where it is most needed, with its own mixer.
 * 2. EQ normal — starts from the bass, keeps the L/R faders and 4 support bands.
 * 3. Auto-chequeo — starts from the treble, keeps its settings and 4 support bands.
 * Also hosts the simultaneous digital input controls (internal app capture,
 * external phone input).
 */
@Composable
fun ProcessorsSection(
    state: AudioEngineViewModel.UiState,
    viewModel: AudioEngineViewModel,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        // --- Inputs digitales (varias a la vez) ---
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    "Entradas digitales (varias a la vez)",
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(6.dp))
                val captureLauncher = rememberLauncherForActivityResult(
                    ActivityResultContracts.StartActivityForResult()
                ) { result ->
                    viewModel.onCaptureResult(result.resultCode, result.data)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (!state.isAppCaptureActive) {
                        Button(
                            onClick = {
                                viewModel.captureRequestIntent()?.let { intent ->
                                    captureLauncher.launch(intent)
                                }
                            },
                            modifier = Modifier.weight(1f).height(48.dp),
                            shape = RoundedCornerShape(12.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = CyanPrimary,
                                contentColor = androidx.compose.ui.graphics.Color.Black
                            )
                        ) {
                            Text("Capturar audio interno", style = MaterialTheme.typography.labelLarge)
                        }
                    } else {
                        OutlinedButton(
                            onClick = { viewModel.stopAppCapture() },
                            modifier = Modifier.weight(1f).height(48.dp),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Text(
                                "Detener captura",
                                color = AmberAccent,
                                style = MaterialTheme.typography.labelLarge
                            )
                        }
                    }
                    OutlinedButton(
                        onClick = { viewModel.toggleInputActive("in_external") },
                        enabled = state.session.isActive,
                        modifier = Modifier.weight(1f).height(48.dp),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Text(
                            if (state.isExternalInputActive) "Externa ON" else "Externa OFF",
                            color = if (state.isExternalInputActive) LimeActive else CyanPrimary,
                            style = MaterialTheme.typography.labelLarge
                        )
                    }
                }
                if (!state.session.isActive) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        "La entrada externa requiere una sesión activa (pestaña Sesión).",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }

        // --- Proceso 1: Auto ayuda ---
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            "Auto ayuda",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            "Decide cada 800 ms · corrige cada 10 ms · va donde más se necesita",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(
                        checked = state.autoHelpActive,
                        onCheckedChange = { viewModel.setAutoHelpEnabled(it) }
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                SweepStatusText(
                    active = state.autoHelpActive,
                    isRunning = state.isRunning,
                    status = state.sweepHelp
                )
                Spacer(modifier = Modifier.height(6.dp))
                MixerSlider(
                    label = "Mezclador propio",
                    level = state.autoHelpMixerLevel,
                    onChange = { viewModel.setAutoHelpMixerLevel(it) }
                )
            }
        }

        // --- Proceso 2: EQ normal + bandas de apoyo ---
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            "EQ normal",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            "Automático — empieza por los graves · faders L/R y Link en EQ",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(
                        checked = state.normalSweepActive,
                        onCheckedChange = { viewModel.setNormalSweepEnabled(it) }
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                SweepStatusText(
                    active = state.normalSweepActive,
                    isRunning = state.isRunning,
                    status = state.sweepNormal
                )
                Spacer(modifier = Modifier.height(6.dp))
                MixerSlider(
                    label = "Mezclador propio",
                    level = state.normalMixerLevel,
                    onChange = { viewModel.setNormalSweepMixerLevel(it) }
                )
                Spacer(modifier = Modifier.height(6.dp))
                Text(
                    "Bandas de apoyo (frecuencia libre)",
                    style = MaterialTheme.typography.labelMedium,
                    color = CyanGlow
                )
                state.supportBandsEq.forEachIndexed { index, band ->
                    SupportBandRow(
                        index = index,
                        band = band,
                        onChange = { freq, gain -> viewModel.setSupportBandEq(index, freq, gain) }
                    )
                }
            }
        }

        // --- Proceso 3: Auto-chequeo + bandas de apoyo ---
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            "Auto-chequeo",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            "Automático — empieza por los agudos · verifica con sus propios ajustes",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(
                        checked = state.workConfig.autoCheck.enabled,
                        onCheckedChange = { viewModel.setAutoCheckEnabled(it) }
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                SweepStatusText(
                    active = state.workConfig.autoCheck.enabled,
                    isRunning = state.isRunning,
                    status = state.sweepCheck
                )
                Spacer(modifier = Modifier.height(6.dp))
                MixerSlider(
                    label = "Mezclador propio",
                    level = state.checkMixerLevel,
                    onChange = { viewModel.setCheckSweepMixerLevel(it) }
                )
                Spacer(modifier = Modifier.height(6.dp))
                Text(
                    "Ciclo de verificación",
                    style = MaterialTheme.typography.labelMedium,
                    color = CyanGlow
                )
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf(15, 30, 60, 120).forEach { seconds ->
                        ChipSelector(
                            label = "${seconds}s",
                            selected = state.workConfig.autoCheck.intervalSeconds == seconds
                        ) {
                            viewModel.setAutoCheckInterval(seconds)
                        }
                    }
                }
                Spacer(modifier = Modifier.height(4.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ProbeQuality.entries.forEach { quality ->
                        ChipSelector(
                            label = quality.label,
                            selected = state.workConfig.autoCheck.quality == quality
                        ) {
                            viewModel.setAutoCheckQuality(quality)
                        }
                    }
                }
                Spacer(modifier = Modifier.height(6.dp))
                Text(
                    "Bandas de apoyo (frecuencia libre)",
                    style = MaterialTheme.typography.labelMedium,
                    color = CyanGlow
                )
                state.supportBandsCheck.forEachIndexed { index, band ->
                    SupportBandRow(
                        index = index,
                        band = band,
                        onChange = { freq, gain -> viewModel.setSupportBandCheck(index, freq, gain) }
                    )
                }
            }
        }
    }
}

/** One free-frequency support band: log frequency slider + gain slider. */
@Composable
private fun SupportBandRow(
    index: Int,
    band: SupportBand,
    onChange: (Float, Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text(
            "Apoyo ${index + 1} · ${band.label} Hz · %+.1f dB".format(band.gainDb),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        // Log frequency slider: 20 Hz → 20 kHz
        val position = (log10((band.frequencyHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).toFloat()
        Slider(
            value = position.coerceIn(0f, 1f),
            onValueChange = { p ->
                val freq = (20f * 10f.pow(3f * p)).coerceIn(20f, 20000f)
                onChange(freq, band.gainDb)
            }
        )
        Slider(
            value = band.gainDb,
            onValueChange = { onChange(band.frequencyHz, it) },
            valueRange = -12f..12f
        )
    }
}

/** Live status line of one automated processor. */
@Composable
private fun SweepStatusText(
    active: Boolean,
    isRunning: Boolean,
    status: AudioEngineViewModel.SweepStatus?
) {
    val text = when {
        active && isRunning -> status?.let { st ->
            val freq = if (st.bandHz >= 1000f) "%.2fk".format(st.bandHz / 1000f)
            else "%.0f".format(st.bandHz)
            "Corrigiendo $freq Hz · %+.1f dB · suavizado %.1f ms".format(st.gainDb, st.smoothingMs)
        } ?: "Activo — esperando señal"
        active -> "Activo — arranca con el motor"
        else -> "Inactivo"
    }
    Text(
        text,
        style = MaterialTheme.typography.labelSmall,
        color = if (active && isRunning) LimeActive else CyanGlow
    )
}

/** A processor's own mixer level (0..1). */
@Composable
private fun MixerSlider(label: String, level: Float, onChange: (Float) -> Unit) {
    Text(
        "$label: ${(level * 100).toInt()}%",
        style = MaterialTheme.typography.labelSmall,
        color = CyanGlow
    )
    Slider(value = level, onValueChange = onChange, valueRange = 0f..1f)
}
