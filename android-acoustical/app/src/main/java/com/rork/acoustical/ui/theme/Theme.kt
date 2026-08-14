package com.rork.acoustical.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val AcoustiCalColorScheme = darkColorScheme(
    primary = CyanPrimary,
    onPrimary = AbyssBlack,
    primaryContainer = SurfaceElevated,
    onPrimaryContainer = CyanGlow,
    secondary = AmberAccent,
    onSecondary = AbyssBlack,
    secondaryContainer = SurfaceVariant,
    onSecondaryContainer = AmberAccent,
    tertiary = CyanDim,
    onTertiary = TextPrimary,
    tertiaryContainer = SurfaceTeal,
    onTertiaryContainer = CyanPrimary,
    background = AbyssBlack,
    onBackground = TextPrimary,
    surface = DeepTeal,
    onSurface = TextPrimary,
    surfaceVariant = SurfaceTeal,
    onSurfaceVariant = TextSecondary,
    surfaceTint = CyanPrimary,
    inverseSurface = TextPrimary,
    inverseOnSurface = DeepTeal,
    error = CoralAlert,
    onError = AbyssBlack,
    errorContainer = CoralAlert,
    onErrorContainer = AbyssBlack,
    outline = TextTertiary,
    outlineVariant = SurfaceVariant,
    scrim = AbyssBlack
)

@Composable
fun AppTheme(
    @Suppress("UNUSED_PARAMETER") darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = AcoustiCalColorScheme,
        typography = AcoustiCalTypography,
        content = content
    )
}
