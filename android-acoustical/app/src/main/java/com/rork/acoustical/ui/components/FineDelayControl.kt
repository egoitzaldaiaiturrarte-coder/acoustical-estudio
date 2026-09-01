package com.rork.acoustical.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.SurfaceElevated
import kotlin.math.roundToInt

/** Distance the sound travels in [ms] at ~343 m/s (34.3 cm per ms). */
fun formatDelayDistance(ms: Float): String {
    val meters = ms * 0.343f
    return if (meters >= 1f) "%.2f m".format(meters) else "%.1f cm".format(meters * 100f)
}

/**
 * Delay control with centimeter-level precision: 0.01 ms steps with +/− fine
 * buttons plus a slider, always showing the time and its equivalent distance
 * ("12.45 ms · 4.27 m", cm for short delays).
 */
@Composable
fun FineDelayControl(
    label: String,
    valueMs: Float,
    valueRange: ClosedFloatingPointRange<Float> = 0f..2000f,
    accent: Color = AmberAccent,
    onChange: (Float) -> Unit,
    modifier: Modifier = Modifier
) {
    fun clampRounded(value: Float): Float =
        (value.coerceIn(valueRange.start, valueRange.endInclusive) * 100f).roundToInt() / 100f

    Column(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Surface(
                    onClick = { onChange(clampRounded(valueMs - 0.01f)) },
                    shape = RoundedCornerShape(8.dp),
                    color = SurfaceElevated.copy(alpha = 0.6f)
                ) {
                    Icon(
                        imageVector = Icons.Filled.Remove,
                        contentDescription = "Menos 0.01 ms",
                        tint = accent,
                        modifier = Modifier
                            .padding(6.dp)
                            .size(16.dp)
                    )
                }
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = "%.2f ms".format(valueMs),
                    style = MaterialTheme.typography.titleMedium,
                    color = accent,
                    fontWeight = FontWeight.SemiBold
                )
                Spacer(modifier = Modifier.width(8.dp))
                Surface(
                    onClick = { onChange(clampRounded(valueMs + 0.01f)) },
                    shape = RoundedCornerShape(8.dp),
                    color = SurfaceElevated.copy(alpha = 0.6f)
                ) {
                    Icon(
                        imageVector = Icons.Filled.Add,
                        contentDescription = "Más 0.01 ms",
                        tint = accent,
                        modifier = Modifier
                            .padding(6.dp)
                            .size(16.dp)
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(2.dp))

        Slider(
            value = valueMs.coerceIn(valueRange.start, valueRange.endInclusive),
            onValueChange = { onChange(clampRounded(it)) },
            valueRange = valueRange
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = androidx.compose.foundation.layout.Arrangement.End
        ) {
            Text(
                text = "%.2f ms · %s".format(valueMs, formatDelayDistance(valueMs)),
                style = MaterialTheme.typography.labelSmall,
                color = CyanGlow
            )
        }
    }
}
