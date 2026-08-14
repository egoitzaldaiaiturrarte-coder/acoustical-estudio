package com.rork.acoustical.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
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
import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import kotlin.math.round

/**
 * Vertical EQ slider for a single band.
 * Drag up/down to adjust gain from -maxGain to +maxGain dB.
 */
@Composable
fun EqVerticalSlider(
    band: EqBand,
    maxGain: Float,
    onGainChange: (Float) -> Unit,
    modifier: Modifier = Modifier
) {
    val animatedGain by animateFloatAsState(
        targetValue = band.gainDb,
        animationSpec = tween(100),
        label = "gain"
    )

    val density = LocalDensity.current
    var sliderHeight by remember { mutableFloatStateOf(0f) }

    val normalizedGain = (animatedGain / maxGain).coerceIn(-1f, 1f)
    val centerY = 1f - (0f + 1f) / 2f // 0.5 normalized

    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Gain value
        Text(
            text = "%+.1f".format(band.gainDb),
            style = MaterialTheme.typography.labelSmall,
            color = if (band.gainDb > 0.1f) LimeActive else if (band.gainDb < -0.1f) AmberAccent else MaterialTheme.colorScheme.onSurfaceVariant,
            fontWeight = FontWeight.Medium
        )

        Spacer(modifier = Modifier.height(4.dp))

        Box(
            modifier = Modifier
                .width(36.dp)
                .height(160.dp)
                .pointerInput(maxGain) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        if (sliderHeight > 0f) {
                            val deltaGain = -(dragAmount.y / sliderHeight) * 2f * maxGain
                            val newGain = (band.gainDb + deltaGain).coerceIn(-maxGain, maxGain)
                            onGainChange(round(newGain * 10f) / 10f)
                        }
                    }
                }
                .pointerInput(maxGain) {
                    detectTapGestures { offset ->
                        if (sliderHeight > 0f) {
                            val tapNorm = 1f - (offset.y / sliderHeight)
                            val newGain = (tapNorm * 2f - 1f) * maxGain
                            onGainChange(round(newGain * 10f) / 10f)
                        }
                    }
                }
        ) {
            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(160.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(SurfaceTeal)
                    .pointerInput(Unit) {
                        awaitPointerEventScope {
                            while (true) {
                                awaitPointerEvent()
                                val h = this.size.height.toFloat()
                                if (h > 0f) sliderHeight = h
                            }
                        }
                    }
            ) {
                sliderHeight = size.height

                val w = size.width
                val h = size.height
                val barWidth = w * 0.4f
                val barX = (w - barWidth) / 2f
                val centerY = h / 2f

                // Track
                drawRoundRect(
                    color = SurfaceElevated,
                    topLeft = Offset(barX, 0f),
                    size = Size(barWidth, h),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(barWidth / 2, barWidth / 2)
                )

                // Center line (0 dB)
                drawLine(
                    color = Color.White.copy(alpha = 0.15f),
                    start = Offset(0f, centerY),
                    end = Offset(w, centerY),
                    strokeWidth = 1f
                )

                // Filled portion
                val gainNorm = (animatedGain / maxGain).coerceIn(-1f, 1f)
                val fillHeight = (gainNorm * h / 2f)
                val fillBrush = if (gainNorm >= 0) {
                    Brush.verticalGradient(
                        colors = listOf(CyanGlow, CyanPrimary),
                        startY = centerY,
                        endY = centerY - fillHeight
                    )
                } else {
                    Brush.verticalGradient(
                        colors = listOf(AmberAccent, CoralAlert),
                        startY = centerY,
                        endY = centerY - fillHeight
                    )
                }

                val fillTop = if (fillHeight >= 0) centerY - fillHeight else centerY
                val fillSize = Size(barWidth, kotlin.math.abs(fillHeight))

                drawRoundRect(
                    brush = fillBrush,
                    topLeft = Offset(barX, fillTop),
                    size = fillSize,
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(barWidth / 2, barWidth / 2)
                )

                // Thumb indicator
                val thumbY = centerY - gainNorm * h / 2f
                val thumbRadius = w * 0.35f

                drawCircle(
                    color = CyanGlow,
                    radius = thumbRadius,
                    center = Offset(w / 2f, thumbY)
                )
                drawCircle(
                    color = AbyssBlackColor,
                    radius = thumbRadius * 0.6f,
                    center = Offset(w / 2f, thumbY)
                )
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        // Frequency label
        Text(
            text = formatFreq(band.centerFreq),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

/**
 * Row of vertical EQ sliders for all bands.
 */
@Composable
fun EqSliderRow(
    bands: List<EqBand>,
    maxGain: Float,
    onBandGainChange: (Int, Float) -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
    ) {
        bands.forEach { band ->
            EqVerticalSlider(
                band = band,
                maxGain = maxGain,
                onGainChange = { gain -> onBandGainChange(band.index, gain) }
            )
        }
    }
}

private fun formatFreq(freq: Float): String {
    return if (freq >= 1000f) {
        "%.0fk".format(freq / 1000f)
    } else {
        "%.0f".format(freq)
    }
}

private val AbyssBlackColor = Color(0xFF050807)
