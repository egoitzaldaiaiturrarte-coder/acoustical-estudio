package com.rork.acoustical.ui.screens

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.repeatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
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
import androidx.compose.material.icons.filled.Hub
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.NoiseAware
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material.icons.filled.Waves
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import com.rork.acoustical.ui.components.GlassCard
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.domain.model.RoutingNodeType
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanDim
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@Composable
fun RoutingMapScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val routingNodes = state.routingNodes
    val routingConnections = state.routingConnections

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 12.dp)
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Mapa de Ruteos",
            style = MaterialTheme.typography.headlineMedium,
            color = CyanGlow,
            fontWeight = FontWeight.Medium
        )
        Text(
            text = "Flujo de señal: Entrada → Proceso → Salida",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Signal flow canvas
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(400.dp)
            ) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val w = size.width
                    val h = size.height
                    val colCount = 3
                    val colWidth = w / colCount
                    val rowHeight = 80f
                    val padding = 30f

                    // Column headers
                    val headers = listOf("Entradas", "Proceso", "Salidas")
                    for (i in headers.indices) {
                        val x = i * colWidth + colWidth / 2f
                        drawLine(
                            color = CyanDim.copy(alpha = 0.2f),
                            start = Offset(x - colWidth * 0.35f, padding),
                            end = Offset(x + colWidth * 0.35f, padding),
                            strokeWidth = 1f
                        )
                    }

                    // Draw connection lines
                    routingConnections.forEach { conn ->
                        val fromNode = routingNodes.find { it.id == conn.fromId }
                        val toNode = routingNodes.find { it.id == conn.toId }
                        if (fromNode != null && toNode != null) {
                            val fromPos = nodePosition(fromNode.id, routingNodes, colWidth, rowHeight, padding + 40f, w, h)
                            val toPos = nodePosition(toNode.id, routingNodes, colWidth, rowHeight, padding + 40f, w, h)

                            val lineColor = when {
                                !conn.isActive -> Color.White.copy(alpha = 0.1f)
                                fromNode.hasError || toNode.hasError -> AmberAccent.copy(alpha = 0.6f)
                                else -> CyanPrimary.copy(alpha = 0.5f)
                            }

                            // Curved connection
                            val midX = (fromPos.x + toPos.x) / 2f
                            drawLine(
                                color = lineColor,
                                start = fromPos,
                                end = Offset(midX, fromPos.y),
                                strokeWidth = 2f,
                                cap = StrokeCap.Round
                            )
                            drawLine(
                                color = lineColor,
                                start = Offset(midX, fromPos.y),
                                end = Offset(midX, toPos.y),
                                strokeWidth = 2f,
                                cap = StrokeCap.Round
                            )
                            drawLine(
                                color = lineColor,
                                start = Offset(midX, toPos.y),
                                end = toPos,
                                strokeWidth = 2f,
                                cap = StrokeCap.Round
                            )

                            // Animated probe dots
                            if (conn.isActive && state.workConfig.autoCheck.enabled) {
                                val probeAlpha = 0.8f
                                drawCircle(
                                    color = CyanGlow.copy(alpha = probeAlpha),
                                    radius = 4f,
                                    center = Offset(midX, fromPos.y)
                                )
                            }
                        }
                    }

                    // Draw nodes
                    routingNodes.forEach { node ->
                        val pos = nodePosition(node.id, routingNodes, colWidth, rowHeight, padding + 40f, w, h)
                        val nodeRadius = 28f

                        val nodeColor = when {
                            node.hasError -> CoralAlert
                            node.isActive -> CyanPrimary
                            else -> SurfaceElevated
                        }

                        // Node circle
                        drawCircle(
                            color = nodeColor.copy(alpha = 0.15f),
                            radius = nodeRadius,
                            center = pos
                        )
                        drawCircle(
                            color = nodeColor,
                            radius = nodeRadius,
                            center = pos,
                            style = Stroke(width = 2f)
                        )

                        // Inner dot
                        drawCircle(
                            color = if (node.isActive) CyanGlow else CyanDim,
                            radius = 6f,
                            center = pos
                        )
                    }
                }

                // Column header labels
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp, start = 8.dp, end = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Entradas", style = MaterialTheme.typography.labelMedium, color = CyanGlow, fontWeight = FontWeight.SemiBold)
                    Text("Proceso", style = MaterialTheme.typography.labelMedium, color = CyanGlow, fontWeight = FontWeight.SemiBold)
                    Text("Salidas", style = MaterialTheme.typography.labelMedium, color = CyanGlow, fontWeight = FontWeight.SemiBold)
                }

                // Node labels
                routingNodes.forEach { node ->
                    val colIndex = when (node.type) {
                        RoutingNodeType.INPUT -> 0
                        RoutingNodeType.PROCESS -> 1
                        RoutingNodeType.OUTPUT -> 2
                        RoutingNodeType.REFERENCE -> 0
                    }
                    val rowIndex = routingNodes.filter { it.type == node.type }.indexOf(node)
                    val yOffset = 60.dp + (rowIndex * 70).dp

                    Box(
                        modifier = Modifier
                            .padding(
                                start = (colIndex * 33).dp + 8.dp,
                                top = yOffset
                            )
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            NodeIcon(node.type, node.isActive)
                            Spacer(modifier = Modifier.height(2.dp))
                            Text(
                                text = node.name,
                                style = MaterialTheme.typography.labelSmall,
                                color = if (node.isActive) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
                                fontWeight = FontWeight.Medium
                            )
                            if (node.spl > 0f) {
                                Text(
                                    text = "%.0f dB".format(node.spl),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = CyanGlow
                                )
                            }
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Reference section
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Referencia",
                        style = MaterialTheme.typography.titleSmall,
                        color = AmberAccent,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = state.workConfig.referenceSource.label,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = state.workConfig.referenceSource.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "%s · %s".format(state.workConfig.bitDepth.label, state.workConfig.sampleRate.label),
                        style = MaterialTheme.typography.labelSmall,
                        color = CyanGlow
                    )
                    Text(
                        text = state.workConfig.stereoMode.label,
                        style = MaterialTheme.typography.labelSmall,
                        color = CyanGlow
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Quick access to EQ and detailed views
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            androidx.compose.material3.OutlinedButton(
                onClick = { navController.navigate("full_eq") },
                modifier = Modifier.weight(1f).height(44.dp),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text("EQ Completo", color = CyanPrimary, style = MaterialTheme.typography.labelLarge)
            }
            androidx.compose.material3.OutlinedButton(
                onClick = { navController.navigate("calibration") },
                modifier = Modifier.weight(1f).height(44.dp),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text("Calibración", color = CyanPrimary, style = MaterialTheme.typography.labelLarge)
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Device summary
        GlassCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = "Dispositivos en ruteo",
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(8.dp))
                if (state.session.devices.isEmpty()) {
                    Text(
                        text = "Sin dispositivos conectados",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    state.session.devices.forEach { device ->
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = device.name,
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                text = device.connection.label,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
    }
}

@Composable
private fun NodeIcon(type: RoutingNodeType, isActive: Boolean) {
    val icon: ImageVector = when (type) {
        RoutingNodeType.INPUT -> Icons.Filled.Mic
        RoutingNodeType.PROCESS -> Icons.Filled.GraphicEq
        RoutingNodeType.OUTPUT -> Icons.Filled.Speaker
        RoutingNodeType.REFERENCE -> Icons.Filled.Waves
    }
    val color = if (isActive) CyanGlow else CyanDim
    Box(
        modifier = Modifier
            .size(32.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(color.copy(alpha = 0.12f)),
        contentAlignment = Alignment.Center
    ) {
        androidx.compose.material3.Icon(
            imageVector = icon,
            contentDescription = null,
            tint = color,
            modifier = Modifier.size(18.dp)
        )
    }
}

private fun nodePosition(
    nodeId: String,
    nodes: List<com.rork.acoustical.domain.model.RoutingNode>,
    colWidth: Float,
    rowHeight: Float,
    startY: Float,
    w: Float,
    h: Float
): Offset {
    val node = nodes.find { it.id == nodeId } ?: return Offset(0f, 0f)
    val colIndex = when (node.type) {
        RoutingNodeType.INPUT -> 0
        RoutingNodeType.PROCESS -> 1
        RoutingNodeType.OUTPUT -> 2
        RoutingNodeType.REFERENCE -> 0
    }
    val rowIndex = nodes.filter { it.type == node.type }.indexOf(node)
    val x = colIndex * colWidth + colWidth / 2f
    val y = startY + rowIndex * rowHeight
    return Offset(x, y)
}
