package com.rork.acoustical

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.core.content.ContextCompat
import com.rork.acoustical.ui.navigation.AppNavigation
import com.rork.acoustical.ui.theme.AppTheme

class MainActivity : ComponentActivity() {

    companion object {
        const val ACTION_START_ENGINE = "com.rork.acoustical.ACTION_START_ENGINE"
        const val ACTION_STOP_ENGINE = "com.rork.acoustical.ACTION_STOP_ENGINE"
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { }

    /** Pending shortcut action to deliver to the ViewModel after composition. */
    @Volatile
    var pendingShortcutAction: String? = null
        private set

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        requestAudioPermission()
        handleShortcutIntent(intent)
        setContent {
            AppTheme {
                Surface(color = MaterialTheme.colorScheme.background) {
                    AppNavigation(
                        pendingShortcutAction = pendingShortcutAction,
                        onShortcutConsumed = { pendingShortcutAction = null }
                    )
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleShortcutIntent(intent)
    }

    private fun handleShortcutIntent(intent: Intent?) {
        val action = intent?.action ?: return
        when (action) {
            ACTION_START_ENGINE, ACTION_STOP_ENGINE -> {
                pendingShortcutAction = action
            }
        }
    }

    private fun requestAudioPermission() {
        val permissions = arrayOf(
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.POST_NOTIFICATIONS,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.ACCESS_FINE_LOCATION
        )
        val needsRequest = permissions.any {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needsRequest) {
            permissionLauncher.launch(permissions)
        }
    }
}
