package com.rork.acoustical.ui.screens

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material.icons.filled.VolumeDown
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.ui.components.GlassCard
import com.rork.acoustical.ui.components.StatusPill
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CoralAlert
import com.rork.acoustical.ui.theme.CyanDim
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import com.rork.acoustical.ui.theme.SurfaceTeal
import com.rork.acoustical.ui.theme.TextPrimary
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@Composable
fun MusicianScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()
    val musician = state.musicianState
    // The live meter only moves while musician mode is ON, so it can't be
    // mistaken for the EQ engine being active.
    val spl = if (musician.isActive) state.currentSpl else 0f
    val geo = state.geoInfo

    val infiniteTransition = rememberInfiniteTransition(label = "pulse")
    val pulseAlpha by infiniteTransition.animateFloat(
        initialValue = 0.15f,
        targetValue = 0.4f,
        animationSpec = infiniteRepeatable(tween(1000), RepeatMode.Reverse),
        label = "pulseAlpha"
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(AbyssBlack, SurfaceTeal.copy(alpha = 0.5f))
                )
            )
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "Modo Musico",
                    style = MaterialTheme.typography.headlineSmall,
                    color = TextPrimary,
                    fontWeight = FontWeight.Bold
                )
                Switch(
                    checked = musician.isActive,
                    onCheckedChange = { viewModel.setMusicianMode(it) }
                )
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Stage visualization with text overlay
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(280.dp)
                    .clip(RoundedCornerShape(16.dp))
                    .background(SurfaceTeal)
            ) {
                StageCanvas(
                    spl = spl,
                    isOverLimit = musician.isOverLimit,
                    hasGpsFix = musician.hasGpsFix,
                    pulseAlpha = pulseAlpha,
                    modifier = Modifier.fillMaxSize()
                )
                Text(
                    "ESCENARIO",
                    style = MaterialTheme.typography.labelSmall,
                    color = CyanDim.copy(alpha = 0.6f),
                    fontWeight = FontWeight.Medium,
                    modifier = Modifier
                        .align(Alignment.TopCenter)
                        .padding(top = 28.dp)
                )
                if (spl > 0f) {
                    Text(
                        "%.0f dB".format(spl),
                        style = MaterialTheme.typography.titleSmall,
                        color = if (musician.isOverLimit) CoralAlert else CyanGlow,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .padding(bottom = 28.dp)
                    )
                }
                if (!musician.hasGpsFix) {
                    Text(
                        "Sin GPS",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.outline,
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .padding(bottom = 8.dp)
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // GPS info
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Icon(
                        Icons.Filled.LocationOn,
                        contentDescription = null,
                        tint = if (musician.hasGpsFix) CyanGlow else MaterialTheme.colorScheme.outline,
                        modifier = Modifier.size(20.dp)
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = if (musician.hasGpsFix) geo.label else "Sin GPS",
                            style = MaterialTheme.typography.bodyMedium,
                            color = TextPrimary,
                            fontWeight = FontWeight.Medium
                        )
                        Text(
                            text = if (musician.hasGpsFix) {
                                "%.4f, %.4f · %.0fm".format(musician.latitude, musician.longitude, musician.altitude)
                            } else "Activa GPS para seguimiento",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    if (musician.hasGpsFix) {
                        StatusPill(text = "%.0fm PA".format(musician.distanceToPaM), color = CyanPrimary)
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // SPL reading - large
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            "SPL en tu posicion",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        val splColor = when {
                            spl >= musician.safeSplLimit -> CoralAlert
                            spl >= musician.safeSplLimit - 15f -> AmberAccent
                            spl > 0f -> LimeActive
                            else -> MaterialTheme.colorScheme.outline
                        }
                        Text(
                            text = if (spl > 0f) "%.0f dB".format(spl) else "-- dB",
                            style = MaterialTheme.typography.displaySmall,
                            color = splColor,
                            fontWeight = FontWeight.Bold
                        )
                    }
                    if (spl >= musician.safeSplLimit) {
                        StatusPill(text = "Sobre limite", color = CoralAlert)
                    } else if (spl > 0f) {
                        StatusPill(text = "Seguro", color = LimeActive)
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // SPL History Chart - Antes y Después
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            "Historial SPL · 30s",
                            style = MaterialTheme.typography.labelMedium,
                            color = TextPrimary,
                            fontWeight = FontWeight.SemiBold
                        )
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            LegendDot(color = CyanDim, label = "Antes")
                            LegendDot(color = LimeActive, label = "Después")
                        }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(140.dp)
                    ) {
                        SplHistoryChart(
                            measured = state.splHistoryMeasured,
                            corrected = state.splHistoryCorrected,
                            safeLimit = musician.safeSplLimit,
                            modifier = Modifier.fillMaxSize()
                        )
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .align(Alignment.BottomCenter),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                "-30s",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.outline
                            )
                            Text(
                                "Ahora",
                                style = MaterialTheme.typography.labelSmall,
                                color = CyanGlow
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // More me / Less me buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Button(
                    onClick = { viewModel.lessMe() },
                    modifier = Modifier
                        .weight(1f)
                        .height(56.dp),
                    shape = RoundedCornerShape(14.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = SurfaceElevated,
                        contentColor = AmberAccent
                    )
                ) {
                    Icon(Icons.Filled.VolumeDown, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Menos yo", fontWeight = FontWeight.SemiBold)
                }
                Button(
                    onClick = { viewModel.moreMe() },
                    modifier = Modifier
                        .weight(1f)
                        .height(56.dp),
                    shape = RoundedCornerShape(14.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyanPrimary.copy(alpha = 0.2f),
                        contentColor = CyanGlow
                    )
                ) {
                    Icon(Icons.Filled.VolumeUp, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Mas yo", fontWeight = FontWeight.SemiBold)
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Personal volume
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            "Volumen personal",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            "%d%% · %+.1f dB".format(musician.volumePercent, musician.personalGainDb),
                            style = MaterialTheme.typography.labelMedium,
                            color = CyanGlow,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Slider(
                        value = musician.personalVolume,
                        onValueChange = { viewModel.setPersonalVolume(it) },
                        colors = SliderDefaults.colors(
                            thumbColor = CyanGlow,
                            activeTrackColor = CyanPrimary,
                            inactiveTrackColor = SurfaceElevated
                        )
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Delay compensation
            GlassCard(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            "Delay compensado",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            "%.1f ms".format(musician.recommendedDelayMs),
                            style = MaterialTheme.typography.titleMedium,
                            color = AmberAccent,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                    Text(
                        "Vel. sonido: %.0f m/s".format(geo.speedOfSound),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // GPS tracking button
            if (!musician.hasGpsFix && musician.isActive) {
                Button(
                    onClick = { viewModel.startMusicianTracking() },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp),
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = CyanPrimary,
                        contentColor = Color.Black
                    )
                ) {
                    Icon(Icons.Filled.MyLocation, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Iniciar seguimiento GPS", fontWeight = FontWeight.SemiBold)
                }
            }

            Spacer(modifier = Modifier.height(24.dp))
        }
    }
}

@Composable
private fun StageCanvas(
    spl: Float,
    isOverLimit: Boolean,
    hasGpsFix: Boolean,
    pulseAlpha: Float,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height

        // Background gradient
        drawRect(
            brush = Brush.verticalGradient(
                colors = listOf(SurfaceTeal, AbyssBlack)
            )
        )

        // Stage platform
        val stageHeight = h * 0.25f
        val stageTop = 20f
        drawRoundRect(
            color = SurfaceElevated.copy(alpha = 0.5f),
            topLeft = Offset(20f, stageTop),
            size = Size(w - 40f, stageHeight),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(8f, 8f)
        )
        drawRoundRect(
            color = CyanDim.copy(alpha = 0.3f),
            topLeft = Offset(20f, stageTop),
            size = Size(w - 40f, stageHeight),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(8f, 8f),
            style = Stroke(width = 2f)
        )

        // PA speakers
        val paLeftX = 40f + 30f
        val paRightX = w - 40f - 30f
        val paY = stageTop + stageHeight / 2f
        val paRadius = 18f

        // SPL zones around PAs
        val zoneColor = when {
            spl >= 100f -> CoralAlert
            spl >= 85f -> AmberAccent
            spl > 0f -> LimeActive
            else -> CyanDim
        }

        for (paX in listOf(paLeftX, paRightX)) {
            drawCircle(
                color = zoneColor.copy(alpha = 0.08f),
                radius = 100f,
                center = Offset(paX, paY)
            )
            drawCircle(
                color = zoneColor.copy(alpha = 0.12f),
                radius = 65f,
                center = Offset(paX, paY)
            )
            drawCircle(
                color = zoneColor.copy(alpha = 0.18f),
                radius = 35f,
                center = Offset(paX, paY)
            )
        }

        // PA circles
        for (paX in listOf(paLeftX, paRightX)) {
            drawCircle(
                brush = Brush.radialGradient(
                    colors = listOf(SurfaceElevated, SurfaceTeal),
                    center = Offset(paX, paY),
                    radius = paRadius
                ),
                radius = paRadius,
                center = Offset(paX, paY)
            )
            drawCircle(
                color = CyanGlow,
                radius = paRadius,
                center = Offset(paX, paY),
                style = Stroke(width = 2f)
            )
            drawCircle(
                color = CyanGlow.copy(alpha = 0.4f),
                radius = 6f,
                center = Offset(paX, paY)
            )
        }

        // Musician position
        val musicianX = w / 2f
        val musicianY = h * 0.72f
        val musicianRadius = 16f

        val musicianColor = when {
            isOverLimit -> CoralAlert
            spl >= 85f -> AmberAccent
            spl > 0f -> CyanGlow
            else -> CyanDim
        }

        // Pulsing glow
        drawCircle(
            color = musicianColor.copy(alpha = pulseAlpha),
            radius = musicianRadius + 20f,
            center = Offset(musicianX, musicianY)
        )
        drawCircle(
            color = musicianColor.copy(alpha = pulseAlpha * 0.5f),
            radius = musicianRadius + 35f,
            center = Offset(musicianX, musicianY)
        )

        // Distance lines to both PAs
        for (paX in listOf(paLeftX, paRightX)) {
            drawLine(
                color = musicianColor.copy(alpha = 0.3f),
                start = Offset(paX, paY),
                end = Offset(musicianX, musicianY),
                strokeWidth = 2f,
                pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 10f))
            )
        }

        // Musician circle
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(musicianColor, musicianColor.copy(alpha = 0.3f)),
                center = Offset(musicianX, musicianY),
                radius = musicianRadius
            ),
            radius = musicianRadius,
            center = Offset(musicianX, musicianY)
        )
        drawCircle(
            color = musicianColor,
            radius = musicianRadius,
            center = Offset(musicianX, musicianY),
            style = Stroke(width = 2f)
        )
    }
}

@Composable
private fun LegendDot(color: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .background(color, CircleShape)
        )
        Spacer(modifier = Modifier.width(4.dp))
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun SplHistoryChart(
    measured: List<Float>,
    corrected: List<Float>,
    safeLimit: Float,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height
        val padTop = 6f
        val padBottom = 16f
        val padLeft = 4f
        val padRight = 4f
        val chartW = w - padLeft - padRight
        val chartH = h - padTop - padBottom

        // Compute data range from both series + safe limit
        val allValues = measured + corrected
        val dataMin = allValues.minOrNull() ?: 40f
        val dataMax = allValues.maxOrNull() ?: 90f
        val minVal = (dataMin - 5f).coerceAtLeast(0f)
        val maxVal = (dataMax + 5f).coerceAtMost(130f)
        val range = (maxVal - minVal).coerceAtLeast(10f)

        // Horizontal grid lines
        val gridCount = 4
        for (i in 0..gridCount) {
            val y = padTop + chartH * i / gridCount
            drawLine(
                color = SurfaceElevated.copy(alpha = 0.25f),
                start = Offset(padLeft, y),
                end = Offset(w - padRight, y),
                strokeWidth = 1f
            )
        }

        // Safe limit dashed line
        val safeNormalized = ((safeLimit - minVal) / range).coerceIn(0f, 1f)
        val safeY = padTop + chartH * (1f - safeNormalized)
        drawLine(
            color = AmberAccent.copy(alpha = 0.4f),
            start = Offset(padLeft, safeY),
            end = Offset(w - padRight, safeY),
            strokeWidth = 1.5f,
            pathEffect = PathEffect.dashPathEffect(floatArrayOf(6f, 6f))
        )

        // Value -> Y mapping
        fun mapY(value: Float): Float {
            val normalized = ((value - minVal) / range).coerceIn(0f, 1f)
            return padTop + chartH * (1f - normalized)
        }

        // Measured line (Antes) -- cyan
        if (measured.size >= 2) {
            val path = Path()
            val fillPath = Path()
            val stepX = chartW / (measured.size - 1)

            path.moveTo(padLeft, mapY(measured[0]))
            fillPath.moveTo(padLeft, padTop + chartH)
            fillPath.lineTo(padLeft, mapY(measured[0]))

            for (i in 1 until measured.size) {
                val x = padLeft + stepX * i
                val y = mapY(measured[i])
                path.lineTo(x, y)
                fillPath.lineTo(x, y)
            }

            fillPath.lineTo(padLeft + stepX * (measured.size - 1), padTop + chartH)
            fillPath.close()

            drawPath(
                path = fillPath,
                brush = Brush.verticalGradient(
                    colors = listOf(CyanDim.copy(alpha = 0.15f), Color.Transparent),
                    startY = padTop,
                    endY = padTop + chartH
                )
            )
            drawPath(
                path = path,
                color = CyanDim,
                style = Stroke(width = 2f)
            )
        }

        // Corrected line (Después) -- lime
        if (corrected.size >= 2) {
            val path = Path()
            val fillPath = Path()
            val stepX = chartW / (corrected.size - 1)

            path.moveTo(padLeft, mapY(corrected[0]))
            fillPath.moveTo(padLeft, padTop + chartH)
            fillPath.lineTo(padLeft, mapY(corrected[0]))

            for (i in 1 until corrected.size) {
                val x = padLeft + stepX * i
                val y = mapY(corrected[i])
                path.lineTo(x, y)
                fillPath.lineTo(x, y)
            }

            fillPath.lineTo(padLeft + stepX * (corrected.size - 1), padTop + chartH)
            fillPath.close()

            drawPath(
                path = fillPath,
                brush = Brush.verticalGradient(
                    colors = listOf(LimeActive.copy(alpha = 0.12f), Color.Transparent),
                    startY = padTop,
                    endY = padTop + chartH
                )
            )
            drawPath(
                path = path,
                color = LimeActive,
                style = Stroke(width = 2.5f)
            )
        }

        // Current value markers at right edge
        if (measured.isNotEmpty()) {
            drawCircle(
                color = CyanDim,
                radius = 3f,
                center = Offset(padLeft + chartW, mapY(measured.last()))
            )
        }
        if (corrected.isNotEmpty()) {
            drawCircle(
                color = LimeActive,
                radius = 3f,
                center = Offset(padLeft + chartW, mapY(corrected.last()))
            )
        }
    }
}
