package com.rork.acoustical.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.rork.acoustical.domain.model.SpectrumFrame
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.SpectrumHigh
import com.rork.acoustical.ui.theme.SpectrumLow
import com.rork.acoustical.ui.theme.SpectrumMid
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min

/**
 * Real-time frequency spectrum analyzer.
 * Displays FFT magnitudes as a filled curve with log-spaced frequency axis.
 * Optionally overlays the corrected spectrum and band centers.
 */
@Composable
fun SpectrumAnalyzer(
    measured: SpectrumFrame?,
    corrected: SpectrumFrame?,
    modifier: Modifier = Modifier,
    showCorrected: Boolean = true
) {
    val maxFreq = 20000f
    val minFreq = 20f
    val minDb = -80f
    val maxDb = 0f

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(260.dp)
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height

            // Background grid
            val gridColor = Color.White.copy(alpha = 0.04f)
            val gridFreqs = listOf(100f, 500f, 1000f, 5000f, 10000f)
            gridFreqs.forEach { freq ->
                val x = freqToX(freq, minFreq, maxFreq, w)
                drawLine(
                    color = gridColor,
                    start = Offset(x, 0f),
                    end = Offset(x, h),
                    strokeWidth = 1f
                )
            }
            // Horizontal grid lines
            listOf(-60f, -40f, -20f, 0f).forEach { db ->
                val y = dbToY(db, minDb, maxDb, h)
                drawLine(
                    color = gridColor,
                    start = Offset(0f, y),
                    end = Offset(w, y),
                    strokeWidth = 1f
                )
            }

            // Measured spectrum
            if (measured != null && measured.size > 1) {
                val path = Path()
                val fillPath = Path()
                var first = true
                val step = max(1, measured.size / 256)

                for (i in 0 until measured.size step step) {
                    val freq = measured.frequencies[i]
                    if (freq < minFreq || freq > maxFreq) continue
                    val x = freqToX(freq, minFreq, maxFreq, w)
                    val y = dbToY(measured.magnitudesDb[i], minDb, maxDb, h)

                    if (first) {
                        path.moveTo(x, y)
                        fillPath.moveTo(x, h)
                        fillPath.lineTo(x, y)
                        first = false
                    } else {
                        path.lineTo(x, y)
                        fillPath.lineTo(x, y)
                    }
                }
                if (!first) {
                    fillPath.lineTo(w, h)
                    fillPath.close()

                    drawPath(
                        path = fillPath,
                        brush = Brush.verticalGradient(
                            colors = listOf(
                                AmberAccent.copy(alpha = 0.35f),
                                AmberAccent.copy(alpha = 0.05f)
                            ),
                            startY = 0f,
                            endY = h
                        )
                    )

                    drawPath(
                        path = path,
                        color = AmberAccent.copy(alpha = 0.9f),
                        style = Stroke(width = 2.5f, cap = StrokeCap.Round)
                    )
                }
            }

            // Corrected spectrum overlay
            if (showCorrected && corrected != null && corrected.size > 1) {
                val path = Path()
                var first = true
                val step = max(1, corrected.size / 256)

                for (i in 0 until corrected.size step step) {
                    val freq = corrected.frequencies[i]
                    if (freq < minFreq || freq > maxFreq) continue
                    val x = freqToX(freq, minFreq, maxFreq, w)
                    val y = dbToY(corrected.magnitudesDb[i], minDb, maxDb, h)

                    if (first) {
                        path.moveTo(x, y)
                        first = false
                    } else {
                        path.lineTo(x, y)
                    }
                }

                drawPath(
                    path = path,
                    color = CyanGlow.copy(alpha = 0.95f),
                    style = Stroke(width = 2.5f, cap = StrokeCap.Round)
                )
            }
        }

        // Frequency labels
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .align(Alignment.BottomStart)
                .padding(horizontal = 4.dp),
            horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceBetween
        ) {
            listOf("20", "100", "1k", "5k", "10k", "20k").forEach {
                Text(
                    text = it,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

/**
 * Vertical bar-style spectrum analyzer showing band levels.
 */
@Composable
fun BandSpectrumBars(
    bandLevels: FloatArray,
    bandFrequencies: FloatArray,
    modifier: Modifier = Modifier,
    barColor: Color = CyanPrimary
) {
    Canvas(
        modifier = modifier
            .fillMaxWidth()
            .height(180.dp)
    ) {
        if (bandLevels.isEmpty()) return@Canvas

        val w = size.width
        val h = size.height
        val barCount = bandLevels.size
        val gap = 3f
        val barWidth = (w - gap * (barCount - 1)) / barCount
        val minDb = -80f
        val maxDb = 0f

        for (i in bandLevels.indices) {
            val normalized = ((bandLevels[i] - minDb) / (maxDb - minDb)).coerceIn(0f, 1f)
            val barHeight = normalized * h
            val x = i * (barWidth + gap)

            val brush = Brush.verticalGradient(
                colors = listOf(
                    SpectrumHigh,
                    SpectrumMid,
                    SpectrumLow
                ),
                startY = h - barHeight,
                endY = h
            )

            drawRoundRect(
                brush = brush,
                topLeft = Offset(x, h - barHeight),
                size = androidx.compose.ui.geometry.Size(barWidth, barHeight),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(barWidth / 4, barWidth / 4)
            )
        }
    }
}

private fun freqToX(freq: Float, minFreq: Float, maxFreq: Float, width: Float): Float {
    val logMin = log10(minFreq.toDouble())
    val logMax = log10(maxFreq.toDouble())
    val logFreq = log10(freq.coerceIn(minFreq, maxFreq).toDouble())
    return ((logFreq - logMin) / (logMax - logMin) * width).toFloat()
}

private fun dbToY(db: Float, minDb: Float, maxDb: Float, height: Float): Float {
    val normalized = ((db - minDb) / (maxDb - minDb)).coerceIn(0f, 1f)
    return height - normalized * height
}
