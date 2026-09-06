package com.rork.acoustical.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import com.rork.acoustical.domain.model.EqBand
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanPrimary
import kotlin.math.log10

/**
 * Compact full-view EQ: every band (124 included) is visible at once as a thin
 * vertical fader on one canvas — no horizontal scrolling. Drag vertically on a
 * band to move it; tap to select it and read its exact frequency and gain.
 * Key frequency marks (100 Hz, 1 kHz, 10 kHz) are the only labels, to save
 * room on small screens. Bands a dynamic EQ is working on right now are
 * tinted with that EQ's accent color ([highlightBands]).
 */
@Composable
fun CompactEqCanvas(
    bands: List<EqBand>,
    maxGain: Float,
    onBandGainChange: (Int, Float) -> Unit,
    modifier: Modifier = Modifier,
    highlightBands: Map<Int, Color> = emptyMap()
) {
    var selected by remember { mutableIntStateOf(-1) }

    Column(modifier = modifier) {
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(210.dp)
                .pointerInput(bands.size, maxGain) {
                    detectDragGestures(
                        onDragStart = { offset ->
                            selected = bandAt(offset.x, size.width.toFloat(), bands.size)
                        },
                        onDrag = { change, _ ->
                            val index = selected
                            if (index in bands.indices) {
                                val half = size.height / 2f
                                val gain = (-(change.position.y - half) / half * maxGain)
                                    .coerceIn(-maxGain, maxGain)
                                onBandGainChange(index, gain)
                            }
                        }
                    )
                }
                .pointerInput(bands.size) {
                    detectTapGestures(
                        onTap = { offset ->
                            selected = bandAt(offset.x, size.width.toFloat(), bands.size)
                        }
                    )
                }
        ) {
            val n = bands.size
            if (n == 0) return@Canvas
            val w = size.width / n
            val labelSpacePx = 14.dp.toPx()
            val top = 6.dp.toPx()
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

            val trackWidth = 1.dp.toPx()
            val barWidth = (w * 0.55f).coerceAtMost(3.dp.toPx())

            bands.forEachIndexed { i, band ->
                val x = (i + 0.5f) * w
                // Track — tinted with the color of the dynamic EQ working on it
                drawLine(
                    color = highlightBands[i]?.copy(alpha = 0.55f) ?: Color.White.copy(alpha = 0.08f),
                    start = Offset(x, top),
                    end = Offset(x, bottom),
                    strokeWidth = trackWidth
                )
                // Gain bar from the zero line
                if (band.gainDb != 0f) {
                    val y = centerY - (band.gainDb / maxGain) * half
                    drawLine(
                        color = if (band.gainDb >= 0f) CyanPrimary else AmberAccent,
                        start = Offset(x, centerY),
                        end = Offset(x, y),
                        strokeWidth = barWidth,
                        cap = StrokeCap.Round
                    )
                }
                // Selection marker
                if (i == selected) {
                    val y = centerY - (band.gainDb / maxGain) * half
                    drawCircle(
                        color = Color.White,
                        radius = barWidth,
                        center = Offset(x, y)
                    )
                }
                // Dynamic EQ activity marker
                highlightBands[i]?.let { hl ->
                    drawCircle(
                        color = hl,
                        radius = barWidth * 1.4f,
                        center = Offset(x, top + 2.dp.toPx())
                    )
                }
            }

            // Key frequency marks only — the exact value shows in the readout
            val paint = android.graphics.Paint().apply {
                color = android.graphics.Color.argb(160, 255, 255, 255)
                textSize = 11.dp.toPx()
                textAlign = android.graphics.Paint.Align.CENTER
                isAntiAlias = true
            }
            val native = drawContext.canvas.nativeCanvas
            listOf(100f to "100", 1000f to "1k", 10000f to "10k").forEach { (freq, label) ->
                val x = freqNorm(freq) * size.width
                native.drawText(label, x, size.height - 3.dp.toPx(), paint)
            }
        }

        Spacer(modifier = Modifier.height(2.dp))

        // Readout of the selected band (tap any fader)
        val band = bands.getOrNull(selected)
        if (band != null) {
            val freqLabel = if (band.centerFreq >= 1000f) {
                "%.2f kHz".format(band.centerFreq / 1000f)
            } else {
                "%.0f Hz".format(band.centerFreq)
            }
            androidx.compose.material3.Text(
                text = "$freqLabel · %+.1f dB".format(band.gainDb),
                style = androidx.compose.material3.MaterialTheme.typography.labelSmall,
                color = CyanPrimary
            )
        }
    }
}

private fun bandAt(x: Float, width: Float, count: Int): Int {
    if (count == 0 || width <= 0f) return -1
    val index = (x / width * count).toInt().coerceIn(0, count - 1)
    return index
}

private fun freqNorm(freqHz: Float): Float =
    (log10((freqHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).coerceIn(0.0, 1.0).toFloat()
