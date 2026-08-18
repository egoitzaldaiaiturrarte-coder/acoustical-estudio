package com.rork.acoustical.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rork.acoustical.domain.model.SpatialPosition
import com.rork.acoustical.domain.model.WorkEnvironmentType
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.CyanDim
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin

/**
 * Circular spatial joystick for positioning elements in the virtual environment.
 * Drag inside the circle to set X (left/right) and Y (forward/backward).
 * Returns normalized values -1..1 for each axis.
 */
@Composable
fun SpatialJoystick(
    position: SpatialPosition,
    environment: WorkEnvironmentType,
    onPositionChange: (Float, Float) -> Unit,
    modifier: Modifier = Modifier,
    isActive: Boolean = true
) {
    val animatedX by animateFloatAsState(
        targetValue = position.x,
        animationSpec = tween(80),
        label = "joyX"
    )
    val animatedY by animateFloatAsState(
        targetValue = position.y,
        animationSpec = tween(80),
        label = "joyY"
    )

    var dragRadius by remember { mutableFloatStateOf(0f) }

    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(
            modifier = Modifier
                .size(220.dp)
                .clip(CircleShape)
                .background(
                    Brush.radialGradient(
                        colors = listOf(
                            SurfaceElevated.copy(alpha = 0.9f),
                            SurfaceTeal.copy(alpha = 0.7f),
                            AbyssBlack.copy(alpha = 0.5f)
                        )
                    )
                )
                .pointerInput(isActive) {
                    if (!isActive) return@pointerInput
                    detectDragGestures { change, _ ->
                        change.consume()
                        if (dragRadius > 0f) {
                            val cx = dragRadius
                            val cy = dragRadius
                            val dx = change.position.x - cx
                            val dy = change.position.y - cy
                            val dist = hypot(dx, dy)
                            val maxDist = dragRadius
                            val clampedDist = minOf(dist, maxDist)
                            val angle = if (dist > 0.001f) {
                                atan2(dy.toFloat(), dx.toFloat())
                            } else 0f
                            val nx = (cos(angle) * clampedDist / maxDist).toFloat()
                            val ny = -(sin(angle) * clampedDist / maxDist).toFloat()
                            onPositionChange(nx, ny)
                        }
                    }
                }
        ) {
            Canvas(modifier = Modifier.size(220.dp)) {
                val w = size.width
                val h = size.height
                val center = Offset(w / 2f, h / 2f)
                val radius = minOf(w, h) / 2f
                dragRadius = radius * 0.85f

                // Outer ring
                drawCircle(
                    color = CyanDim.copy(alpha = 0.3f),
                    radius = radius * 0.92f,
                    center = center,
                    style = Stroke(width = 2f)
                )

                // Inner rings
                for (ratio in listOf(0.33f, 0.66f)) {
                    drawCircle(
                        color = Color.White.copy(alpha = 0.06f),
                        radius = radius * ratio,
                        center = center,
                        style = Stroke(width = 1f)
                    )
                }

                // Cross hair
                val crossLen = radius * 0.85f
                drawLine(
                    color = Color.White.copy(alpha = 0.08f),
                    start = Offset(center.x - crossLen, center.y),
                    end = Offset(center.x + crossLen, center.y),
                    strokeWidth = 1f
                )
                drawLine(
                    color = Color.White.copy(alpha = 0.08f),
                    start = Offset(center.x, center.y - crossLen),
                    end = Offset(center.x, center.y + crossLen),
                    strokeWidth = 1f
                )

                // Diagonal guides
                val diagLen = radius * 0.6f
                for (angleDeg in listOf(45, 135, 225, 315)) {
                    val rad = angleDeg * PI / 180f
                    drawLine(
                        color = Color.White.copy(alpha = 0.04f),
                        start = center,
                        end = Offset(
                            (center.x + cos(rad) * diagLen).toFloat(),
                            (center.y + sin(rad) * diagLen).toFloat()
                        ),
                        strokeWidth = 1f
                    )
                }

                // Position indicator (thumb)
                val thumbX = center.x + animatedX * dragRadius
                val thumbY = center.y - animatedY * dragRadius

                // Halo glow
                drawCircle(
                    brush = Brush.radialGradient(
                        colors = listOf(
                            CyanGlow.copy(alpha = 0.4f),
                            CyanGlow.copy(alpha = 0f)
                        ),
                        center = Offset(thumbX, thumbY),
                        radius = 40f
                    ),
                    radius = 40f,
                    center = Offset(thumbX, thumbY)
                )

                // Connection line from center to thumb
                drawLine(
                    color = CyanPrimary.copy(alpha = 0.4f),
                    start = center,
                    end = Offset(thumbX, thumbY),
                    strokeWidth = 2f
                )

                // Thumb layers
                drawCircle(color = CyanGlow, radius = 16f, center = Offset(thumbX, thumbY))
                drawCircle(color = AbyssBlack, radius = 10f, center = Offset(thumbX, thumbY))
                drawCircle(color = CyanGlow, radius = 4f, center = Offset(thumbX, thumbY))

                // Direction tick marks
                drawLine(
                    color = CyanDim.copy(alpha = 0.5f),
                    start = Offset(center.x, center.y - radius * 0.88f),
                    end = Offset(center.x, center.y - radius * 0.78f),
                    strokeWidth = 3f
                )
                drawLine(
                    color = CyanDim.copy(alpha = 0.5f),
                    start = Offset(center.x, center.y + radius * 0.78f),
                    end = Offset(center.x, center.y + radius * 0.88f),
                    strokeWidth = 3f
                )
                drawLine(
                    color = CyanDim.copy(alpha = 0.5f),
                    start = Offset(center.x - radius * 0.88f, center.y),
                    end = Offset(center.x - radius * 0.78f, center.y),
                    strokeWidth = 3f
                )
                drawLine(
                    color = CyanDim.copy(alpha = 0.5f),
                    start = Offset(center.x + radius * 0.78f, center.y),
                    end = Offset(center.x + radius * 0.88f, center.y),
                    strokeWidth = 3f
                )
            }

            // Direction labels
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
                Text("Frente", fontSize = 9.sp, color = CyanDim, modifier = Modifier.padding(top = 4.dp))
            }
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.BottomCenter) {
                Text("Atrás", fontSize = 9.sp, color = CyanDim, modifier = Modifier.padding(bottom = 4.dp))
            }
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.CenterStart) {
                Text("Izq", fontSize = 9.sp, color = CyanDim, modifier = Modifier.padding(start = 6.dp))
            }
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.CenterEnd) {
                Text("Der", fontSize = 9.sp, color = CyanDim, modifier = Modifier.padding(end = 6.dp))
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Position readout
        Text(
            text = position.positionLabel(environment),
            style = MaterialTheme.typography.labelMedium,
            color = if (position.isCenter) MaterialTheme.colorScheme.onSurfaceVariant else CyanGlow,
            fontWeight = FontWeight.Medium
        )
    }
}
