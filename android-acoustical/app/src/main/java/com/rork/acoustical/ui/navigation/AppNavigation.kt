package com.rork.acoustical.ui.navigation

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Equalizer
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Hub
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.rork.acoustical.MainActivity
import com.rork.acoustical.ui.screens.CalibrationScreen
import com.rork.acoustical.ui.screens.ConsoleScreen
import com.rork.acoustical.ui.screens.DashboardScreen
import com.rork.acoustical.ui.screens.EqualizerScreen
import com.rork.acoustical.ui.screens.SettingsScreen
import com.rork.acoustical.ui.theme.CyanGlow
import com.rork.acoustical.ui.theme.CyanPrimary
import com.rork.acoustical.ui.viewmodel.AudioEngineViewModel

data class BottomNavItem(
    val route: String,
    val label: String,
    val icon: ImageVector
)

@Composable
fun AppNavigation(
    pendingShortcutAction: String? = null,
    onShortcutConsumed: () -> Unit = {}
) {
    val navController = rememberNavController()
    val viewModel: AudioEngineViewModel = viewModel()

    // Handle shortcut actions from app icon
    LaunchedEffect(pendingShortcutAction) {
        when (pendingShortcutAction) {
            MainActivity.ACTION_START_ENGINE -> viewModel.startEngine()
            MainActivity.ACTION_STOP_ENGINE -> viewModel.stopEngine()
        }
        if (pendingShortcutAction != null) {
            onShortcutConsumed()
        }
    }

    val items = listOf(
        BottomNavItem("dashboard", "Inicio", Icons.Filled.GraphicEq),
        BottomNavItem("equalizer", "EQ", Icons.Filled.Equalizer),
        BottomNavItem("console", "Consola", Icons.Filled.Hub),
        BottomNavItem("calibration", "Calibrar", Icons.Filled.Tune),
        BottomNavItem("settings", "Ajustes", Icons.Filled.Tune)
    )

    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination

    Scaffold(
        bottomBar = {
            NavigationBar(
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                items.forEach { item ->
                    val selected = currentDestination?.hierarchy?.any { it.route == item.route } == true
                    NavigationBarItem(
                        selected = selected,
                        onClick = {
                            navController.navigate(item.route) {
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                        icon = { Icon(item.icon, contentDescription = item.label) },
                        label = { androidx.compose.material3.Text(item.label) },
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = CyanGlow,
                            selectedTextColor = CyanGlow,
                            unselectedIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            unselectedTextColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            indicatorColor = CyanPrimary.copy(alpha = 0.15f)
                        )
                    )
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = "dashboard",
            modifier = Modifier.padding(innerPadding)
        ) {
            composable("dashboard") {
                DashboardScreen(navController = navController, viewModel = viewModel)
            }
            composable("equalizer") {
                EqualizerScreen(navController = navController, viewModel = viewModel)
            }
            composable("console") {
                ConsoleScreen(navController = navController, viewModel = viewModel)
            }
            composable("calibration") {
                CalibrationScreen(navController = navController, viewModel = viewModel)
            }
            composable("settings") {
                SettingsScreen(navController = navController, viewModel = viewModel)
            }
        }
    }
}
