package com.rork.acoustical.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import com.rork.acoustical.ui.components.EqSliderRow
import com.rork.acoustical.ui.components.SpectrumAnalyzer
import com.rork.acoustical.ui.theme.AbyssBlack
import com.rork.acoustical.ui.theme.AmberAccent
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

@Composable
fun FullEqScreen(
    navController: NavController,
    viewModel: AudioEngineViewModel = viewModel()
) {
    val state by viewModel.uiState.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(AbyssBlack)
            .padding(horizontal = 8.dp)
    ) {
        // Top bar with close button
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 8.dp, start = 8.dp, end = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "EQ Dinámico",
                style = MaterialTheme.typography.titleMedium,
                color = CyanGlow,
                fontWeight = FontWeight.SemiBold
            )
            Row {
                IconButton(onClick = { viewModel.resetBands() }) {
                    Icon(
                        Icons.Filled.Refresh,
                        contentDescription = "Reset",
                        tint = AmberAccent
                    )
                }
                IconButton(onClick = { navController.popBackStack() }) {
                    Icon(
                        Icons.Filled.Close,
                        contentDescription = "Cerrar",
                        tint = CyanGlow
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Corner controls row
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Left corner: band count info
            CornerControl(
                label = "Bandas",
                value = state.config.bandCount.label
            )
            CornerControl(
                label = "SPL",
                value = "%.1f".format(state.currentSpl)
            )
            CornerControl(
                label = "Corrección",
                value = "%.0f%%".format(state.correctionIntensity * 100)
            )
            CornerControl(
                label = "Max",
                value = "+%.0fdB".format(state.config.maxGainDb)
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Spectrum overlay
        SpectrumAnalyzer(
            measured = state.measuredSpectrum,
            corrected = state.correctedSpectrum,
            showCorrected = state.isCorrecting,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(12.dp))

        // EQ sliders
        if (state.bands.isNotEmpty()) {
            EqSliderRow(
                bands = state.bands,
                maxGain = state.config.maxGainDb,
                onBandGainChange = { index, gain ->
                    viewModel.setBandGain(index, gain)
                },
                modifier = Modifier.fillMaxWidth()
            )
        } else {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "Inicia el motor para ver las bandas EQ",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))
    }
}

@Composable
private fun CornerControl(
    label: String,
    value: String
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(CyanPrimary.copy(alpha = 0.08f))
            .padding(horizontal = 8.dp, vertical = 4.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = value,
            style = MaterialTheme.typography.labelMedium,
            color = CyanGlow,
            fontWeight = FontWeight.SemiBold
        )
    }
}
