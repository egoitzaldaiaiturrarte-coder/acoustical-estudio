package com.rork.acoustical.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowLeft
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.theme.LimeActive
import com.rork.acoustical.ui.theme.SurfaceElevated
import kotlinx.coroutines.delay

/**
 * Direction of a D-pad press in the bidimensional spatial plane.
 */
enum class DpadDirection {
    UP, DOWN, LEFT, RIGHT
}

/**
 * Keyboard-style directional pad replacing the circular joystick.
 *
 * Four arrow buttons move the element in the bidimensional plane:
 * up/down/left/right. Press-and-hold repeats the movement,
 * so sweeps feel continuous like a console jog wheel.
 */
@Composable
fun DirectionalPad(
    onDirection: (DpadDirection) -> Unit,
    modifier: Modifier = Modifier,
    isActive: Boolean = true,
    stepLabel: String = ""
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (stepLabel.isNotEmpty()) {
            Text(
                text = stepLabel,
                style = MaterialTheme.typography.labelSmall,
                color = CyanGlow,
                fontWeight = FontWeight.Medium
            )
            Spacer(modifier = Modifier.height(6.dp))
        }

        DirectionButton(
            icon = Icons.Filled.KeyboardArrowUp,
            direction = DpadDirection.UP,
            onDirection = onDirection,
            isActive = isActive
        )

        Spacer(modifier = Modifier.height(8.dp))

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            DirectionButton(
                icon = Icons.Filled.KeyboardArrowLeft,
                direction = DpadDirection.LEFT,
                onDirection = onDirection,
                isActive = isActive
            )
            DirectionButton(
                icon = Icons.Filled.KeyboardArrowRight,
                direction = DpadDirection.RIGHT,
                onDirection = onDirection,
                isActive = isActive
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        DirectionButton(
            icon = Icons.Filled.KeyboardArrowDown,
            direction = DpadDirection.DOWN,
            onDirection = onDirection,
            isActive = isActive
        )
    }
}

@Composable
private fun DirectionButton(
    icon: ImageVector,
    direction: DpadDirection,
    onDirection: (DpadDirection) -> Unit,
    isActive: Boolean
) {
    var pressed by remember { mutableStateOf(false) }

    // Repeat while held: first tick immediately, then every 160 ms
    LaunchedEffect(pressed) {
        while (pressed) {
            delay(160)
            onDirection(direction)
        }
    }

    val bg = when {
        !isActive -> SurfaceElevated.copy(alpha = 0.4f)
        pressed -> CyanPrimary
        else -> SurfaceElevated
    }
    val tint = when {
        !isActive -> MaterialTheme.colorScheme.outline
        pressed -> AbyssBlack
        else -> CyanGlow
    }

    Box(
        modifier = Modifier
            .size(64.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(bg)
            .pointerInput(isActive, direction) {
                if (!isActive) return@pointerInput
                detectTapGestures(
                    onPress = {
                        pressed = true
                        onDirection(direction)
                        try {
                            awaitRelease()
                        } finally {
                            pressed = false
                        }
                    }
                )
            },
        contentAlignment = Alignment.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = direction.name,
            tint = tint,
            modifier = Modifier.size(34.dp)
        )
        if (pressed) {
            Box(
                modifier = Modifier
                    .size(6.dp)
                    .align(Alignment.BottomCenter)
                    .background(if (isActive) Color.Black else LimeActive, RoundedCornerShape(3.dp))
            )
        }
    }
}
