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
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
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
import com.rork.acoustical.domain.model.AppMode
import com.rork.acoustical.domain.model.BitDepth
import com.rork.acoustical.domain.model.DeviceConnection
import com.rork.acoustical.domain.model.DeviceType
import com.rork.acoustical.domain.model.OutputTarget
import com.rork.acoustical.domain.model.ReferenceSource
import com.rork.acoustical.domain.model.SampleRate
import com.rork.acoustical.domain.model.StereoMode
import com.rork.acoustical.domain.model.WorkEnvironmentType
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@Composable
fun AddDeviceScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val workConfig = state.workConfig

    var deviceName by remember { mutableStateOf("") }
    var selectedType by remember { mutableStateOf(DeviceType.CONSOLE) }
    var selectedConnection by remember { mutableStateOf(DeviceConnection.WIFI_OSC) }
    var ipAddress by remember { mutableStateOf("192.168.1.100") }
    var port by remember { mutableStateOf("10023") }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Añadir dispositivo",
            style = MaterialTheme.typography.headlineMedium,
            color = CyanGlow,
            fontWeight = FontWeight.Medium
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Step 1: Device info
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "1. Dispositivo",
                    style = MaterialTheme.typography.titleMedium,
                    color = CyanGlow,
                    fontWeight = FontWeight.SemiBold
                )
                Spacer(modifier = Modifier.height(8.dp))

                // Device name
                androidx.compose.material3.OutlinedTextField(
                    value = deviceName,
                    onValueChange = { deviceName = it },
                    label = { Text("Nombre del dispositivo") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    shape = RoundedCornerShape(12.dp)
                )

                Spacer(modifier = Modifier.height(12.dp))

                Text(
                    text = "Tipo de dispositivo",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(DeviceType.entries) { type ->
                        ChipSelector(
                            text = type.label,
                            selected = selectedType == type,
                            onClick = { selectedType = type }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                Text(
                    text = "Conexión",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(DeviceConnection.entries) { conn ->
                        ChipSelector(
                            text = conn.label,
                            selected = selectedConnection == conn,
                            onClick = { selectedConnection = conn }
                        )
                    }
                }

                // Connection help text
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = selectedConnection.helpText,
                    style = MaterialTheme.typography.bodySmall,
                    color = AmberAccent
                )

                // IP/Port for OSC/USB
                if (selectedConnection == DeviceConnection.WIFI_OSC || selectedConnection == DeviceConnection.MIDI) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        androidx.compose.material3.OutlinedTextField(
                            value = ipAddress,
                            onValueChange = { ipAddress = it },
                            label = { Text("IP") },
                            modifier = Modifier.weight(2f),
                            singleLine = true,
                            shape = RoundedCornerShape(12.dp)
                        )
                        androidx.compose.material3.OutlinedTextField(
                            value = port,
                            onValueChange = { port = it.filter { c -> c.isDigit() } },
                            label = { Text("Puerto") },
                            modifier = Modifier.weight(1f),
                            singleLine = true,
                            shape = RoundedCornerShape(12.dp)
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        // Step 2: Work configuration
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "2. Configuración del trabajo",
                    style = MaterialTheme.typography.titleMedium,
                    color = CyanGlow,
                    fontWeight = FontWeight.SemiBold
                )

                Spacer(modifier = Modifier.height(8.dp))

                // Environment
                Text(
                    text = "Entorno virtual",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(WorkEnvironmentType.entries) { env ->
                        ChipSelector(
                            text = env.label,
                            selected = workConfig.environment == env,
                            onClick = { viewModel.setWorkEnvironment(env) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Reference source
                Text(
                    text = "Referencia de medición",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                Column {
                    ReferenceSource.entries.forEach { ref ->
                        ReferenceRow(
                            label = ref.label,
                            description = ref.description,
                            selected = workConfig.referenceSource == ref,
                            onClick = { viewModel.setReferenceSource(ref) }
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Bit depth
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
                            selected = workConfig.bitDepth == bd,
                            onClick = { viewModel.setBitDepth(bd) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Sample rate
                Text(
                    text = "Frecuencia de muestreo",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(SampleRate.entries) { sr ->
                        ChipSelector(
                            text = sr.label,
                            selected = workConfig.sampleRate == sr,
                            onClick = { viewModel.setWorkSampleRate(sr) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Stereo mode
                Text(
                    text = "Modo estéreo",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(StereoMode.entries) { mode ->
                        ChipSelector(
                            text = mode.label,
                            selected = workConfig.stereoMode == mode,
                            onClick = { viewModel.setStereoMode(mode) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // App mode
                Text(
                    text = "Modo",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(AppMode.entries) { mode ->
                        ChipSelector(
                            text = mode.label,
                            selected = workConfig.appMode == mode,
                            onClick = { viewModel.setAppMode(mode) }
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Add device button
        Button(
            onClick = {
                viewModel.addDevice(
                    name = deviceName.ifBlank { selectedType.label },
                    type = selectedType,
                    connection = selectedConnection,
                    ipAddress = ipAddress,
                    port = port.toIntOrNull() ?: 0
                )
                navController.popBackStack()
            },
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = CyanPrimary,
                contentColor = Color.Black
            )
        ) {
            Icon(Icons.Filled.Check, contentDescription = null)
            Spacer(modifier = Modifier.width(8.dp))
            Text("Añadir y conectar", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
        }

        Spacer(modifier = Modifier.height(12.dp))

        OutlinedButton(
            onClick = { navController.popBackStack() },
            modifier = Modifier.fillMaxWidth().height(44.dp),
            shape = RoundedCornerShape(12.dp)
        ) {
            Text("Cancelar", color = MaterialTheme.colorScheme.onSurfaceVariant)
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}

@Composable
fun ChipSelector(
    text: String,
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
        androidx.compose.material3.TextButton(onClick = onClick) {
            Text(text, color = fg, style = MaterialTheme.typography.labelMedium, fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal)
        }
    }
}

@Composable
private fun ReferenceRow(
    label: String,
    description: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(if (selected) CyanPrimary.copy(alpha = 0.1f) else Color.Transparent)
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(18.dp)
                .clip(RoundedCornerShape(9.dp))
                .background(if (selected) CyanGlow else MaterialTheme.colorScheme.surfaceVariant)
        )
        Spacer(modifier = Modifier.width(8.dp))
        Column {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = if (selected) CyanGlow else MaterialTheme.colorScheme.onSurface,
                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal
            )
            Text(
                text = description,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}
