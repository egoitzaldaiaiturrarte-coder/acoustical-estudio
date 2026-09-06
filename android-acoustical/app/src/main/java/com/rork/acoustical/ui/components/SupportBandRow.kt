package com.rork.acoustical.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.rork.acoustical.domain.model.SupportBand
import kotlin.math.log10
import kotlin.math.pow

/** One free-frequency support band: log frequency slider + gain slider. */
@Composable
fun SupportBandRow(
    index: Int,
    band: SupportBand,
    onChange: (Float, Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text(
            "Apoyo ${index + 1} · ${band.label} Hz · %+.1f dB".format(band.gainDb),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        // Log frequency slider: 20 Hz → 20 kHz
        val position = (log10((band.frequencyHz.coerceAtLeast(20f) / 20f).toDouble()) / 3.0).toFloat()
        Slider(
            value = position.coerceIn(0f, 1f),
            onValueChange = { p ->
                val freq = (20f * 10f.pow(3f * p)).coerceIn(20f, 20000f)
                onChange(freq, band.gainDb)
            }
        )
        Slider(
            value = band.gainDb,
            onValueChange = { onChange(band.frequencyHz, it) },
            valueRange = -12f..12f
        )
    }
}
