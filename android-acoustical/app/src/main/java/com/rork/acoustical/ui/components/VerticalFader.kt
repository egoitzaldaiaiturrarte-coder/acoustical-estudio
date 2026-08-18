package com.rork.acoustical.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import kotlin.math.round

/**
 * Vertical fader for spatial depth or size control.
 * Drag up/down to change value from 0 to 1.
 * Shows SPL compensation indicator when active.
 */
@Composable
fun VerticalFader(
    value: Float,
    label: String,
    valueLabel: String,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    splCompensationDb: Float = 0f,
    splCompensationActive: Boolean = true,
    faderHeight: Int = 240
) {
    val animatedValue by animateFloatAsState(
        targetValue = value,
        animationSpec = tween(80),
        label = "faderValue"
    )

    var trackHeight by remember { mutableFloatStateOf(0f) }

    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Label
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontWeight = FontWeight.Medium
        )

        Spacer(modifier = Modifier.height(4.dp))

        // Value readout
        Text(
            text = valueLabel,
            style = MaterialTheme.typography.titleSmall,
            color = CyanGlow,
            fontWeight = FontWeight.SemiBold
        )

        Spacer(modifier = Modifier.height(6.dp))

        // SPL compensation indicator
        if (splCompensationActive && splCompensationDb != 0f) {
            Text(
                text = "SPL %+.1fdB".format(splCompensationDb),
                fontSize = 9.sp,
                color = AmberAccent,
                fontWeight = FontWeight.Medium
            )
            Spacer(modifier = Modifier.height(4.dp))
        } else {
            Spacer(modifier = Modifier.height(14.dp))
        }

        // Fader track
        Box(
            modifier = Modifier
                .width(56.dp)
                .height(faderHeight.dp)
                .pointerInput(Unit) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        if (trackHeight > 0f) {
                            val delta = -(dragAmount.y / trackHeight)
                            val newVal = (value + delta).coerceIn(0f, 1f)
                            onValueChange(round(newVal * 100f) / 100f)
                        }
                    }
                }
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        if (trackHeight > 0f) {
                            val tapVal = 1f - (offset.y / trackHeight)
                            onValueChange(round(tapVal * 100f) / 100f)
                        }
                    }
                }
        ) {
            Canvas(
                modifier = Modifier
                    .width(56.dp)
                    .height(faderHeight.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(SurfaceTeal)
            ) {
                trackHeight = size.height
                val w = size.width
                val h = size.height
                val trackWidth = w * 0.3f
                val trackX = (w - trackWidth) / 2f

                // Track background
                drawRoundRect(
                    color = SurfaceElevated,
                    topLeft = Offset(trackX, 0f),
                    size = Size(trackWidth, h),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(trackWidth / 2, trackWidth / 2)
                )

                // Fill from bottom to value
                val fillHeight = animatedValue * h
                val fillBrush = Brush.verticalGradient(
                    colors = listOf(
                        CyanGlow,
                        CyanPrimary.copy(alpha = 0.8f)
                    ),
                    startY = h - fillHeight,
                    endY = h
                )
                drawRoundRect(
                    brush = fillBrush,
                    topLeft = Offset(trackX, h - fillHeight),
                    size = Size(trackWidth, fillHeight),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(trackWidth / 2, trackWidth / 2)
                )

                // Glow when compensation active
                if (splCompensationActive) {
                    drawRoundRect(
                        color = CyanGlow.copy(alpha = 0.15f),
                        topLeft = Offset(trackX - 2f, h - fillHeight - 2f),
                        size = Size(trackWidth + 4f, fillHeight + 4f),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(trackWidth / 2, trackWidth / 2)
                    )
                }

                // Tick marks
                for (i in 0..10) {
                    val tickY = h * (1f - i / 10f)
                    val tickW = if (i % 5 == 0) w * 0.7f else w * 0.5f
                    val tickX = (w - tickW) / 2f
                    drawLine(
                        color = Color.White.copy(alpha = if (i % 5 == 0) 0.15f else 0.06f),
                        start = Offset(tickX, tickY),
                        end = Offset(tickX + tickW, tickY),
                        strokeWidth = 1f
                    )
                }

                // Thumb
                val thumbY = h - animatedValue * h
                val thumbWidth = w * 0.8f
                val thumbHeight = 14f
                val thumbX = (w - thumbWidth) / 2f

                // Thumb shadow/glow
                if (splCompensationActive) {
                    drawRoundRect(
                        color = CyanGlow.copy(alpha = 0.3f),
                        topLeft = Offset(thumbX - 2f, thumbY - thumbHeight / 2f - 2f),
                        size = Size(thumbWidth + 4f, thumbHeight + 4f),
                        cornerRadius = androidx.compose.ui.geometry.CornerRadius(4f, 4f)
                    )
                }

                // Thumb body
                drawRoundRect(
                    brush = Brush.horizontalGradient(
                        colors = listOf(
                            Color(0xFF2A4A45),
                            Color(0xFF3A5A55),
                            Color(0xFF2A4A45)
                        )
                    ),
                    topLeft = Offset(thumbX, thumbY - thumbHeight / 2f),
                    size = Size(thumbWidth, thumbHeight),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(3f, 3f)
                )

                // Thumb indicator line
                drawLine(
                    color = CyanGlow,
                    start = Offset(thumbX + 4f, thumbY),
                    end = Offset(thumbX + thumbWidth - 4f, thumbY),
                    strokeWidth = 2f
                )
            }
        }
    }
}
