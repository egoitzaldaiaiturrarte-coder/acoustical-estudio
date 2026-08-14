package com.rork.acoustical.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * Circular SPL gauge with needle indicator and color-coded zones.
 */
@Composable
fun SplGauge(
    spl: Float,
    targetSpl: Float,
    modifier: Modifier = Modifier
) {
    val animatedSpl by animateFloatAsState(
        targetValue = spl,
        animationSpec = tween(200),
        label = "spl"
    )

    Box(
        modifier = modifier.size(200.dp),
        contentAlignment = Alignment.Center
    ) {
        Canvas(modifier = Modifier.size(200.dp)) {
            val center = Offset(size.width / 2, size.height / 2)
            val radius = minOf(size.width, size.height) / 2f
            val arcStrokeWidth = radius * 0.12f

            // Background arc (270 degrees, from -225 to 45)
            drawArc(
                color = SurfaceElevated,
                startAngle = -225f,
                sweepAngle = 270f,
                useCenter = false,
                topLeft = Offset(center.x - radius + arcStrokeWidth / 2, center.y - radius + arcStrokeWidth / 2),
                size = Size((radius - arcStrokeWidth / 2) * 2, (radius - arcStrokeWidth / 2) * 2),
                style = Stroke(width = arcStrokeWidth, cap = StrokeCap.Round)
            )

            // SPL range: 30-120 dB mapped to 270 degrees
            val minVal = 30f
            val maxVal = 120f
            val normalized = ((animatedSpl - minVal) / (maxVal - minVal)).coerceIn(0f, 1f)
            val sweepAngle = normalized * 270f

            // Color based on SPL level
            val arcColor = when {
                animatedSpl < 40 -> CyanPrimary
                animatedSpl < 70 -> LimeActive
                animatedSpl < 85 -> AmberAccent
                else -> CoralAlert
            }

            // Active arc
            drawArc(
                color = arcColor,
                startAngle = -225f,
                sweepAngle = sweepAngle,
                useCenter = false,
                topLeft = Offset(center.x - radius + arcStrokeWidth / 2, center.y - radius + arcStrokeWidth / 2),
                size = Size((radius - arcStrokeWidth / 2) * 2, (radius - arcStrokeWidth / 2) * 2),
                style = Stroke(width = arcStrokeWidth, cap = StrokeCap.Round)
            )

            // Target marker
            val targetNormalized = ((targetSpl - minVal) / (maxVal - minVal)).coerceIn(0f, 1f)
            val targetAngle = (-225f + targetNormalized * 270f) * (PI / 180f)
            val markerRadius = radius - arcStrokeWidth / 2
            val markerInner = markerRadius - arcStrokeWidth * 0.6f
            val markerOuter = markerRadius + arcStrokeWidth * 0.6f
            val mx1 = center.x + markerInner * cos(targetAngle - PI / 2).toFloat()
            val my1 = center.y + markerInner * sin(targetAngle - PI / 2).toFloat()
            val mx2 = center.x + markerOuter * cos(targetAngle - PI / 2).toFloat()
            val my2 = center.y + markerOuter * sin(targetAngle - PI / 2).toFloat()
            drawLine(
                color = CyanGlow,
                start = Offset(mx1, my1),
                end = Offset(mx2, my2),
                strokeWidth = 4f,
                cap = StrokeCap.Round
            )

            // Tick marks
            for (db in listOf(30, 50, 70, 90, 110)) {
                val tickNorm = ((db - minVal) / (maxVal - minVal))
                val tickAngle = (-225f + tickNorm * 270f) * (PI / 180f)
                val tickInner = radius - arcStrokeWidth * 1.3f
                val tickOuter = radius - arcStrokeWidth * 1.15f
                val tx1 = center.x + tickInner * cos(tickAngle - PI / 2).toFloat()
                val ty1 = center.y + tickInner * sin(tickAngle - PI / 2).toFloat()
                val tx2 = center.x + tickOuter * cos(tickAngle - PI / 2).toFloat()
                val ty2 = center.y + tickOuter * sin(tickAngle - PI / 2).toFloat()
                drawLine(
                    color = Color.White.copy(alpha = 0.2f),
                    start = Offset(tx1, ty1),
                    end = Offset(tx2, ty2),
                    strokeWidth = 2f
                )
            }
        }

        Column(
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = if (animatedSpl > 0) "%.1f".format(animatedSpl) else "--",
                fontSize = 42.sp,
                fontWeight = FontWeight.Light,
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = "dB SPL",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "Target: %.0f".format(targetSpl),
                style = MaterialTheme.typography.labelSmall,
                color = CyanGlow
            )
        }
    }
}

/**
 * Compact SPL readout for use in app bars and notifications.
 */
@Composable
fun SplBadge(
    spl: Float,
    modifier: Modifier = Modifier
) {
    val color = when {
        spl <= 0 -> SurfaceElevated
        spl < 70 -> LimeActive
        spl < 85 -> AmberAccent
        else -> CoralAlert
    }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(color.copy(alpha = 0.15f))
            .padding(horizontal = 12.dp, vertical = 6.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(color)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(
                text = "%.1f dB".format(spl),
                style = MaterialTheme.typography.labelLarge,
                color = color,
                fontWeight = FontWeight.SemiBold
            )
        }
    }
}
