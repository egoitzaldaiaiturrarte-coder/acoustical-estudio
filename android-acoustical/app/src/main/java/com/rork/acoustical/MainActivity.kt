package com.rork.acoustical

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.rork.acoustical.service.PhoneSyncManager
import com.rork.acoustical.ui.navigation.AppNavigation
import com.rork.acoustical.ui.theme.AppTheme
import com.rork.acoustical.ui.theme.AmberAccent

class MainActivity : ComponentActivity() {

    companion object {
        const val ACTION_START_ENGINE = "com.rork.acoustical.ACTION_START_ENGINE"
        const val ACTION_STOP_ENGINE = "com.rork.acoustical.ACTION_STOP_ENGINE"
    }

    /**
     * Permissions required for the full workflow: measuring, GPS tracking,
     * Bluetooth output control and notifications. Bluetooth runtime
     * permissions only exist on Android 12+.
     */
    private val requiredPermissions: Array<String>
        get() = buildList {
            add(Manifest.permission.RECORD_AUDIO)
            add(Manifest.permission.POST_NOTIFICATIONS)
            add(Manifest.permission.ACCESS_FINE_LOCATION)
            add(Manifest.permission.ACCESS_COARSE_LOCATION)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(Manifest.permission.BLUETOOTH_CONNECT)
                add(Manifest.permission.BLUETOOTH_SCAN)
            }
        }.toTypedArray()

    /** Pending shortcut action to deliver to the ViewModel after composition. */
    @Volatile
    var pendingShortcutAction: String? = null
        private set

    /** Names of permissions still missing — drives the pending banner. */
    private var missingPermissions by mutableStateOf(listOf<String>())

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        updateMissingPermissions()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        // Servidor de sincronización/actualización por USB (solo loopback + adb)
        PhoneSyncManager.get(applicationContext).start()
        updateMissingPermissions()
        requestPendingPermissions()
        handleShortcutIntent(intent)
        setContent {
            AppTheme {
                Surface(color = MaterialTheme.colorScheme.background) {
                    Box(modifier = Modifier.fillMaxSize()) {
                        AppNavigation(
                            pendingShortcutAction = pendingShortcutAction,
                            onShortcutConsumed = { pendingShortcutAction = null }
                        )
                        if (missingPermissions.isNotEmpty()) {
                            PermissionBanner(
                                modifier = Modifier.align(Alignment.BottomCenter),
                                count = missingPermissions.size,
                                onRequest = { requestPendingPermissions() }
                            )
                        }
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // The user may have granted permissions from system settings
        updateMissingPermissions()
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

    private fun updateMissingPermissions() {
        missingPermissions = requiredPermissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestPendingPermissions() {
        if (missingPermissions.isNotEmpty()) {
            permissionLauncher.launch(requiredPermissions)
        }
    }
}

/**
 * Persistent banner shown while any required permission is missing,
 * with a one-tap re-request button.
 */
@Composable
private fun PermissionBanner(
    count: Int,
    onRequest: () -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(12.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(AmberAccent.copy(alpha = 0.18f))
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "Permisos pendientes ($count)",
                color = AmberAccent,
                fontWeight = FontWeight.SemiBold,
                style = MaterialTheme.typography.labelMedium
            )
            Text(
                text = "Micrófono, GPS y Bluetooth son necesarios para medir, rastrear y controlar las salidas.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelSmall
            )
        }
        Spacer(modifier = Modifier.width(8.dp))
        Button(onClick = onRequest) {
            Text("Conceder")
        }
    }
}
