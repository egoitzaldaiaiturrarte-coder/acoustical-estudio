package com.rork.acoustical.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.rork.acoustical.domain.model.ModeProfiles
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

/**
 * Work modes (v1.8): the six startup profiles of the SHARED catalog — the
 * same table (ids, values and order) that the Windows app and the VST read
 * from the C core, mirrored here as [ModeProfiles] and pinned by the JVM
 * test. Picking one applies the complete configuration (engine, the three
 * dynamic EQs with their support-band banks, spatial position), persists the
 * choice and reloads it silently at the next startup.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ModesScreen(
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val activeId = state.activeModeId

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Modos de trabajo", color = MaterialTheme.colorScheme.onSurface) },
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
            Text(
                "Los 6 perfiles de arranque compartidos por las tres versiones (app Windows, plugin y móvil): " +
                    "cada uno fija SPL objetivo, motor, los 3 ecuas dinámicos con su banco de bandas de apoyo y la " +
                    "posición espacial. El modo activo se guarda y se recarga solo al abrir la app.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(12.dp))

            // "Sin modo": free adjustments, nothing from the catalog.
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                ModeModeCard(
                    title = "Sin modo (ajustes libres)",
                    summary = "El motor queda como lo dejes: ningún perfil del catálogo activo.",
                    isActive = activeId.isEmpty(),
                    activeLabel = "Activo",
                    activeColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    buttonLabel = if (activeId.isEmpty()) null else "Volver a sin modo",
                    onActivate = { viewModel.setWorkMode(null) }
                )
            }
            Spacer(modifier = Modifier.height(8.dp))

            ModeProfiles.ALL.forEach { m ->
                GlassCard(modifier = Modifier.fillMaxWidth()) {
                    ModeModeCard(
                        title = m.name,
                        summary = m.summary,
                        isActive = activeId == m.id,
                        activeLabel = "Modo activo",
                        activeColor = CyanGlow,
                        buttonLabel = if (activeId == m.id) null else "Activar",
                        onActivate = { viewModel.setWorkMode(m.id) }
                    )
                }
                Spacer(modifier = Modifier.height(8.dp))
            }
        }
    }
}

@Composable
private fun ModeModeCard(
    title: String,
    summary: String,
    isActive: Boolean,
    activeLabel: String,
    activeColor: Color,
    buttonLabel: String?,
    onActivate: () -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.onSurface
            )
            if (isActive) {
                Text(
                    activeLabel,
                    style = MaterialTheme.typography.labelMedium,
                    color = activeColor
                )
            }
        }
        Spacer(modifier = Modifier.height(4.dp))
        Text(
            summary,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        if (buttonLabel != null) {
            Spacer(modifier = Modifier.height(10.dp))
            Button(
                onClick = onActivate,
                modifier = Modifier.fillMaxWidth().height(40.dp),
                shape = RoundedCornerShape(10.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyanPrimary.copy(alpha = 0.25f),
                    contentColor = CyanGlow
                )
            ) {
                Text(buttonLabel, style = MaterialTheme.typography.labelLarge)
            }
        }
    }
}
