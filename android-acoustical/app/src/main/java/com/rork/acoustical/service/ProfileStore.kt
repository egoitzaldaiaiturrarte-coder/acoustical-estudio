package com.rork.acoustical.service

import android.content.Context
import android.content.SharedPreferences
import com.rork.acoustical.domain.model.RoomProfile
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

/**
 * Persists room calibration profiles on device storage so they survive
 * app restarts, and converts profiles to/from JSON for sharing between
 * devices via the Android share sheet.
 */
class ProfileStore(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
    }

    /** Load all persisted profiles; empty when none were ever saved. */
    fun loadProfiles(): List<RoomProfile> {
        val raw = prefs.getString(KEY_PROFILES, null) ?: return emptyList()
        return try {
            json.decodeFromString<List<RoomProfile>>(raw)
        } catch (e: Exception) {
            emptyList()
        }
    }

    /** Persist the full profile list. */
    fun saveProfiles(profiles: List<RoomProfile>) {
        prefs.edit().putString(KEY_PROFILES, json.encodeToString(profiles)).apply()
    }

    /** Remember which profile was active last so it is restored on launch. */
    fun saveActiveProfileName(name: String?) {
        prefs.edit().putString(KEY_ACTIVE, name).apply()
    }

    fun loadActiveProfileName(): String? = prefs.getString(KEY_ACTIVE, null)

    /** Serialize a single profile into a shareable JSON payload. */
    fun exportProfile(profile: RoomProfile): String = json.encodeToString(profile)

    /** Parse a shared JSON payload back into a profile; null when invalid. */
    fun importProfile(raw: String): RoomProfile? = try {
        json.decodeFromString<RoomProfile>(raw)
    } catch (e: Exception) {
        null
    }

    companion object {
        private const val PREFS_NAME = "acoustical_profiles"
        private const val KEY_PROFILES = "profiles_json"
        private const val KEY_ACTIVE = "active_profile_name"
    }
}
