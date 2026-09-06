package com.rork.acoustical.ui.components

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.rork.acoustical.domain.audio.SweepDirection
import com.rork.acoustical.domain.model.ProbeQuality
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

/**
 * The three dynamic EQs, managed from Ruteos: the same automatic free-frequency
 * corrector repeated three times with different settings (direction, interval,
 * gain, speed, extra sweeps and support bands — configured in Ajustes).
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
        // --- Entradas digitales (varias a la vez) ---
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

        // --- Los tres ecuas dinámicos (idénticos, con distintos ajustes) ---
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
                                style = MaterialTheme.typography.titleSmall,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                directionSubtitle(eq.config.startFrom),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = eq.enabled,
                            onCheckedChange = { viewModel.setDynamicEqEnabled(index, it) }
                        )
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    SweepStatusText(
                        active = eq.enabled,
                        isRunning = state.isRunning,
                        status = eq.status
                    )
                    if (index == 2) {
                        Spacer(modifier = Modifier.height(6.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                "Verificación automática",
                                style = MaterialTheme.typography.labelMedium,
                                color = CyanGlow
                            )
                            Switch(
                                checked = state.workConfig.autoCheck.enabled,
                                onCheckedChange = { viewModel.setAutoCheckEnabled(it) }
                            )
                        }
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
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        "Parámetros, mezclador y bandas de apoyo: en Ajustes",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}

private fun directionSubtitle(direction: SweepDirection): String = when (direction) {
    SweepDirection.NEED_BASED -> "Va donde más se necesita"
    SweepDirection.BOTTOM_UP -> "Empieza por los graves"
    SweepDirection.TOP_DOWN -> "Empieza por los agudos"
}

/** Live status line of one dynamic EQ. */
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
