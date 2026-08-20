package com.rork.acoustical.domain.audio

import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.provider.Settings

/**
 * Focus ("concert") mode manager.
 *
 * Silences everything that is not part of the audio workflow:
 * - Applies Do Not Disturb (needs user-granted notification policy access).
 * - Offers the system airplane-mode settings panel (Android does not allow
 *   third-party apps to toggle airplane mode programmatically).
 */
class FocusModeManager(private val context: Context) {

    private val notificationManager: NotificationManager =
        context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    /** Whether the user granted Do-Not-Disturb (notification policy) access to the app. */
    fun hasPolicyAccess(): Boolean = notificationManager.isNotificationPolicyAccessGranted

    /** Intent that opens the system screen to grant DND access to this app. */
    fun policyAccessIntent(): Intent = Intent(Settings.ACTION_NOTIFICATION_POLICY_ACCESS_SETTINGS)

    /** Intent that opens the airplane-mode settings panel. */
    fun airplaneModeIntent(): Intent = Intent(Settings.ACTION_AIRPLANE_MODE_SETTINGS)

    /**
     * Apply or release focus mode.
     * Uses PRIORITY filter: alarms only, no calls, no notifications from other apps,
     * while AcoustiCal's own foreground service keeps running.
     *
     * @return true if the filter was applied successfully.
     */
    fun applyFocus(active: Boolean): Boolean {
        if (active && !hasPolicyAccess()) return false
        return try {
            val filter = if (active) {
                NotificationManager.INTERRUPTION_FILTER_PRIORITY
            } else {
                NotificationManager.INTERRUPTION_FILTER_ALL
            }
            notificationManager.setInterruptionFilter(filter)
            true
        } catch (e: SecurityException) {
            false
        }
    }

    /** Whether a DND-like filter is currently active. */
    fun isFocusActive(): Boolean {
        val current = notificationManager.currentInterruptionFilter
        return current == NotificationManager.INTERRUPTION_FILTER_PRIORITY ||
            current == NotificationManager.INTERRUPTION_FILTER_NONE ||
            current == NotificationManager.INTERRUPTION_FILTER_ALARMS
    }
}
