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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Hub
import androidx.compose.material.icons.filled.PhoneAndroid
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.model.DeviceConnection
import com.rork.acoustical.domain.model.DeviceState
import com.rork.acoustical.domain.model.DeviceType
import com.rork.acoustical.domain.model.WorkDevice
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@Composable
fun SessionScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val session = state.session
    var sessionName by remember { mutableStateOf(session.name) }
    var pin by remember { mutableStateOf(session.pin) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        // Header
        Text(
            text = "AcoustiCal",
            style = MaterialTheme.typography.headlineLarge,
            color = CyanGlow,
            fontWeight = FontWeight.Light
        )
        Text(
            text = "Controladora profesional de audio",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(20.dp))

        if (!session.isActive) {
            // Login form
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Sesión de trabajo",
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Crea una sesión o únete a una existente",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    OutlinedTextField(
                        value = sessionName,
                        onValueChange = { sessionName = it },
                        label = { Text("Nombre de sesión") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(12.dp)
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    OutlinedTextField(
                        value = pin,
                        onValueChange = { if (it.length <= 6) pin = it.filter { c -> c.isDigit() } },
                        label = { Text("PIN (4-6 dígitos)") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                        visualTransformation = PasswordVisualTransformation(),
                        shape = RoundedCornerShape(12.dp)
                    )

                    Spacer(modifier = Modifier.height(16.dp))

                    Button(
                        onClick = {
                            viewModel.createSession(sessionName, pin)
                        },
                        modifier = Modifier.fillMaxWidth().height(52.dp),
                        shape = RoundedCornerShape(14.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = CyanPrimary,
                            contentColor = Color.Black
                        ),
                        enabled = sessionName.isNotBlank() && pin.length >= 4
                    ) {
                        Text("Crear sesión", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    OutlinedButton(
                        onClick = {
                            viewModel.joinSession(sessionName, pin)
                        },
                        modifier = Modifier.fillMaxWidth().height(48.dp),
                        shape = RoundedCornerShape(14.dp),
                        enabled = sessionName.isNotBlank() && pin.length >= 4
                    ) {
                        Text("Unirse a sesión", color = CyanGlow, style = MaterialTheme.typography.labelLarge)
                    }
                }
            }
        } else {
            // Active session — device list
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = session.name,
                                style = MaterialTheme.typography.titleMedium,
                                color = CyanGlow,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = if (session.isHost) "Host · ${session.devices.size} dispositivo(s)" else "Conectado · ${session.devices.size} dispositivo(s)",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        StatusPill(
                            text = "Activa",
                            color = LimeActive
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Device list
            Text(
                text = "Dispositivos conectados",
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onSurface
            )

            Spacer(modifier = Modifier.height(8.dp))

            if (session.devices.isEmpty()) {
                GlassCard(modifier = Modifier.fillMaxWidth()) {
                    Box(
                        modifier = Modifier.fillMaxWidth().padding(24.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "Sin dispositivos. Añade el primero.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            } else {
                session.devices.forEach { device ->
                    DeviceCard(
                        device = device,
                        onRemove = { viewModel.removeDevice(device.id) },
                        onToggleActive = { viewModel.toggleDeviceActive(device.id) }
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Add device button
            Button(
                onClick = { navController.navigate("add_device") },
                modifier = Modifier.fillMaxWidth().height(52.dp),
                shape = RoundedCornerShape(14.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = CyanPrimary,
                    contentColor = Color.Black
                )
            ) {
                Icon(Icons.Filled.Add, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Añadir dispositivo", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Quick connection help
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Guía de conexión",
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    DeviceConnection.entries.forEach { conn ->
                        ConnectionHelpRow(conn)
                        Spacer(modifier = Modifier.height(4.dp))
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            OutlinedButton(
                onClick = { viewModel.leaveSession() },
                modifier = Modifier.fillMaxWidth().height(44.dp),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text("Salir de sesión", color = CoralAlert, style = MaterialTheme.typography.labelLarge)
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}

@Composable
private fun DeviceCard(
    device: WorkDevice,
    onRemove: () -> Unit,
    onToggleActive: () -> Unit
) {
    val stateColor = when (device.state) {
        DeviceState.CONNECTED -> LimeActive
        DeviceState.CONNECTING -> AmberAccent
        DeviceState.ERROR -> CoralAlert
        else -> MaterialTheme.colorScheme.outline
    }

    GlassCard(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Device icon
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(CyanPrimary.copy(alpha = 0.12f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = deviceTypeIcon(device.type),
                    contentDescription = null,
                    tint = CyanGlow,
                    modifier = Modifier.size(22.dp)
                )
            }

            Spacer(modifier = Modifier.width(12.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = device.name,
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    fontWeight = FontWeight.Medium
                )
                Text(
                    text = "${device.type.label} · ${device.connection.label}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (device.latencyMs > 0f) {
                    Text(
                        text = "Latencia: %.0fms".format(device.latencyMs),
                        style = MaterialTheme.typography.labelSmall,
                        color = if (device.latencyMs < 50) LimeActive else AmberAccent
                    )
                }
            }

            Column(horizontalAlignment = Alignment.End) {
                StatusPill(
                    text = device.state.label,
                    color = stateColor
                )
                Spacer(modifier = Modifier.height(4.dp))
                TextButton(onClick = onToggleActive) {
                    Text(
                        text = if (device.isActive) "Pausar" else "Activar",
                        style = MaterialTheme.typography.labelSmall,
                        color = CyanGlow
                    )
                }
            }
        }
    }
}

@Composable
private fun ConnectionHelpRow(conn: DeviceConnection) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.Top
    ) {
        Box(
            modifier = Modifier
                .size(6.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(CyanPrimary)
                .padding(top = 6.dp)
        )
        Spacer(modifier = Modifier.width(8.dp))
        Column {
            Text(
                text = conn.label,
                style = MaterialTheme.typography.labelMedium,
                color = CyanGlow,
                fontWeight = FontWeight.Medium
            )
            Text(
                text = conn.helpText,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

private fun deviceTypeIcon(type: DeviceType): ImageVector = when (type) {
    DeviceType.PHONE -> Icons.Filled.PhoneAndroid
    DeviceType.TABLET -> Icons.Filled.PhoneAndroid
    DeviceType.CONSOLE -> Icons.Filled.Hub
    DeviceType.BLUETOOTH_SPEAKER -> Icons.Filled.Speaker
    DeviceType.USB_AUDIO -> Icons.Filled.Usb
    DeviceType.COMPUTER -> Icons.Filled.Computer
    DeviceType.PA_SYSTEM -> Icons.Filled.Speaker
    DeviceType.MONITOR -> Icons.Filled.AudioFile
    DeviceType.IN_EARS -> Icons.Filled.Headphones
}
