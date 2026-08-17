package com.rork.acoustical.domain.audio

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.location.Geocoder
import android.os.Looper
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.util.Locale
import kotlin.coroutines.resume

/**
 * Provides geolocation-based acoustic adjustments.
 *
 * Uses GPS/network location to detect the environment (indoor/outdoor, altitude)
 * and recommends acoustic parameters accordingly:
 *
 * - Altitude affects air absorption (speed of sound, high-freq attenuation)
 * - Outdoor locations have less reverberation, need higher SPL targets
 * - Urban areas have higher background noise
 * - Indoor venues benefit from more aggressive correction
 */
class LocationProvider(private val context: Context) {

    data class GeoAcousticInfo(
        val latitude: Double = 0.0,
        val longitude: Double = 0.0,
        val altitude: Double = 0.0,
        val label: String = "Ubicación desconocida",
        val isOutdoor: Boolean = false,
        val isUrban: Boolean = false,
        val recommendedSpl: Float = 75f,
        val recommendedDelayMs: Float = 25f,
        val altitudeCorrectionDb: Float = 0f,
        val speedOfSound: Float = 343f,
        val hasFix: Boolean = false
    )

    private var lastInfo: GeoAcousticInfo = GeoAcousticInfo()
    private var lastLocationTime: Long = 0L

    /**
     * Check if location permissions are granted.
     */
    fun hasPermission(): Boolean {
        return ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun hasFinePermission(): Boolean {
        return ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }

    /**
     * Get the last known location from GPS or network provider.
     */
    @SuppressLint("MissingPermission")
    suspend fun getCurrentLocation(): GeoAcousticInfo {
        if (!hasPermission()) return lastInfo

        return withContext(Dispatchers.IO) {
            val locationManager = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
                ?: return@withContext lastInfo

            val provider = when {
                locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER) && hasFinePermission() ->
                    LocationManager.GPS_PROVIDER
                locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER) ->
                    LocationManager.NETWORK_PROVIDER
                else -> return@withContext lastInfo
            }

            @Suppress("DEPRECATION")
            val location: Location? = suspendCancellableCoroutine { cont ->
                val listener = object : android.location.LocationListener {
                    override fun onLocationChanged(loc: Location) {
                        locationManager.removeUpdates(this)
                        if (cont.isActive) cont.resume(loc)
                    }

                    override fun onProviderEnabled(p: String) {}
                    override fun onProviderDisabled(p: String) {
                        if (cont.isActive) cont.resume(null)
                    }

                    @Deprecated("Deprecated in Java")
                    override fun onStatusChanged(p: String?, s: Int, e: android.os.Bundle?) {}
                }

                try {
                    locationManager.requestSingleUpdate(provider, listener, Looper.getMainLooper())
                } catch (e: SecurityException) {
                    if (cont.isActive) cont.resume(null)
                }

                cont.invokeOnCancellation {
                    try {
                        locationManager.removeUpdates(listener)
                    } catch (_: Exception) {}
                }

                // Also check last known location as a quick fallback
                try {
                    val lastKnown = locationManager.getLastKnownLocation(provider)
                    if (lastKnown != null && cont.isActive) {
                        // Use last known immediately but don't cancel — wait for a fresh fix
                        // unless we already have recent data
                        if (System.currentTimeMillis() - lastKnown.time > 60_000) {
                            // Stale, let the fresh update come
                        } else {
                            locationManager.removeUpdates(listener)
                            cont.resume(lastKnown)
                        }
                    }
                } catch (e: SecurityException) {
                    // Continue waiting for single update
                }
            }

            if (location != null) {
                lastInfo = computeAcousticInfo(location)
                lastLocationTime = System.currentTimeMillis()
            }

            lastInfo
        }
    }

    /**
     * Compute acoustic parameters from a GPS location.
     */
    private fun computeAcousticInfo(loc: Location): GeoAcousticInfo {
        val altitude = if (loc.hasAltitude()) loc.altitude else 0.0

        // Speed of sound varies with temperature and altitude
        // v = 331.3 * sqrt(1 + T/273.15) m/s
        // At higher altitude, temperature drops ~6.5°C per 1000m
        val seaLevelTemp = 20.0
        val tempC = seaLevelTemp - (altitude / 1000.0) * 6.5
        val tempK = tempC + 273.15
        val speedOfSound = (331.3 * kotlin.math.sqrt(1.0 + tempK / 273.15)).toFloat()

        // Air absorption increases with altitude and affects high frequencies
        // Approximate correction: ~0.5 dB per 1000m altitude at 8 kHz
        val altitudeCorrectionDb = (altitude / 1000.0 * 0.5).toFloat()

        // Try reverse geocoding to get a label and detect environment
        var label = "Lat %.4f, Lng %.4f".format(loc.latitude, loc.longitude)
        var isOutdoor = false
        var isUrban = false
        var recommendedSpl = 75f
        var recommendedDelayMs = 25f

        try {
            val geocoder = Geocoder(context, Locale.getDefault())
            @Suppress("DEPRECATION")
            val addresses = geocoder.getFromLocation(loc.latitude, loc.longitude, 1)
            val addr = addresses?.firstOrNull()
            if (addr != null) {
                val parts = mutableListOf<String>()
                addr.locality?.let { parts.add(it) }
                addr.subAdminArea?.let { if (it !in parts) parts.add(it) }
                addr.adminArea?.let { if (it !in parts) parts.add(it) }
                addr.countryName?.let { parts.add(it) }
                label = parts.joinToString(", ")

                // Heuristic: if country code and locality present, it's likely urban
                isUrban = addr.locality != null
            }
        } catch (e: Exception) {
            // Geocoder may fail without network — use coordinates as label
        }

        // Altitude-based outdoor detection
        // Very low altitude near sea level could be indoor (underground studios)
        // Very high altitude is definitely outdoor
        isOutdoor = altitude > 50.0 || altitude < -5.0

        // Recommended parameters based on environment
        recommendedSpl = when {
            altitude > 1000 -> 90f   // High altitude outdoor — thinner air, less absorption
            isUrban && !isOutdoor -> 75f  // Indoor urban
            isOutdoor -> 85f        // General outdoor
            else -> 75f
        }

        // Outdoor environments need less delay (no reflections)
        // Indoor with high altitude needs more (large venues)
        recommendedDelayMs = when {
            isOutdoor -> 35f
            altitude > 1000 -> 30f
            else -> 25f
        }

        return GeoAcousticInfo(
            latitude = loc.latitude,
            longitude = loc.longitude,
            altitude = altitude,
            label = label,
            isOutdoor = isOutdoor,
            isUrban = isUrban,
            recommendedSpl = recommendedSpl,
            recommendedDelayMs = recommendedDelayMs,
            altitudeCorrectionDb = altitudeCorrectionDb,
            speedOfSound = speedOfSound,
            hasFix = true
        )
    }

    fun getLastInfo(): GeoAcousticInfo = lastInfo

    fun isStale(): Boolean {
        return System.currentTimeMillis() - lastLocationTime > 5 * 60 * 1000
    }
}
