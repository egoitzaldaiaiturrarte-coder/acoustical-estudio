package com.rork.acoustical.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel
import kotlin.math.log10

/** Accent color of each dynamic EQ (1: cyan, 2: amber, 3: magenta). */
val DynamicEqColors = listOf(CyanPrimary, AmberAccent, Color(0xFFFF5FD2))

/** Log position of a frequency inside the 20 Hz–20 kHz range (0..1). */
fun freqNorm01(freqHz: Float): Float =
    (log10((freqHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).coerceIn(0.0, 1.0).toFloat()

/** Nearest band index for a frequency on a log-spaced 20 Hz–20 kHz grid. */
fun bandIndexForFrequency(freqHz: Float, bandCount: Int): Int =
    (freqNorm01(freqHz) * bandCount).toInt().coerceIn(0, (bandCount - 1).coerceAtLeast(0))

/**
 * Read-only mini EQ of one dynamic EQ: shows its live gain curve per band,
 * highlights the band it is correcting right now, and reflects the selected
 * channel (L/R) when the channels are unlinked.
 */
@Composable
fun DynamicEqCanvas(
    index: Int,
    title: String,
    active: Boolean,
    status: AudioEngineViewModel.SweepStatus?,
    gainsL: FloatArray,
    gainsR: FloatArray,
    linked: Boolean,
    showRight: Boolean,
    maxGain: Float,
    modifier: Modifier = Modifier
) {
    val color = DynamicEqColors[index % DynamicEqColors.size]
    val gains = if (!linked && showRight) gainsR else gainsL

    Column(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(if (active) color else Color.Gray.copy(alpha = 0.5f))
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                title,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurface,
                fontWeight = FontWeight.SemiBold
            )
            if (!linked) {
                Spacer(modifier = Modifier.width(4.dp))
                Text(
                    if (showRight) "R" else "L",
                    style = MaterialTheme.typography.labelSmall,
                    color = color,
                    fontWeight = FontWeight.Bold
                )
            }
            Spacer(modifier = Modifier.weight(1f))
            Text(
                when {
                    !active -> "Inactivo"
                    status != null && status.bandHz > 0f -> {
                        val freq = if (status.bandHz >= 1000f) "%.2fk".format(status.bandHz / 1000f)
                        else "%.0f".format(status.bandHz)
                        "$freq Hz · %+.1f dB · ${status.channel}"
                            .format(status.gainDb)
                    }
                    else -> "Esperando señal"
                },
                style = MaterialTheme.typography.labelSmall,
                color = if (active) LimeActive else MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Spacer(modifier = Modifier.height(2.dp))
        Canvas(modifier = Modifier.fillMaxWidth().height(96.dp)) {
            val n = gains.size
            if (n == 0) return@Canvas
            val w = size.width / n
            val labelSpacePx = 12.dp.toPx()
            val top = 4.dp.toPx()
            val bottom = size.height - labelSpacePx
            val half = (bottom - top) / 2f
            val centerY = top + half

            // Zero line
            drawLine(
                color = Color.White.copy(alpha = 0.10f),
                start = Offset(0f, centerY),
                end = Offset(size.width, centerY),
                strokeWidth = 1.dp.toPx()
            )

            val barWidth = (w * 0.55f).coerceAtMost(3.dp.toPx())
            val activeIdx = if (active && status != null && status.bandHz > 0f) {
                bandIndexForFrequency(status.bandHz, n)
            } else -1

            for (i in 0 until n) {
                val x = (i + 0.5f) * w
                val gain = gains[i].coerceIn(-maxGain, maxGain)
                drawLine(
                    color = Color.White.copy(alpha = 0.06f),
                    start = Offset(x, top),
                    end = Offset(x, bottom),
                    strokeWidth = 1.dp.toPx()
                )
                if (gain != 0f) {
                    val y = centerY - (gain / maxGain) * half
                    drawLine(
                        color = if (i == activeIdx) Color.White else color,
                        start = Offset(x, centerY),
                        end = Offset(x, y),
                        strokeWidth = barWidth,
                        cap = StrokeCap.Round
                    )
                }
                if (i == activeIdx) {
                    val y = centerY - (gains[i].coerceIn(-maxGain, maxGain) / maxGain) * half
                    drawCircle(
                        color = color,
                        radius = barWidth * 1.8f,
                        center = Offset(x, y)
                    )
                }
            }

            // Key frequency marks only
            val paint = android.graphics.Paint().apply {
                setColor(android.graphics.Color.argb(140, 255, 255, 255))
                textSize = 10.dp.toPx()
                textAlign = android.graphics.Paint.Align.CENTER
                isAntiAlias = true
            }
            val native = drawContext.canvas.nativeCanvas
            listOf(100f to "100", 1000f to "1k", 10000f to "10k").forEach { (freq, label) ->
                native.drawText(label, freqNorm01(freq) * size.width, size.height - 2.dp.toPx(), paint)
            }
        }
    }
}
