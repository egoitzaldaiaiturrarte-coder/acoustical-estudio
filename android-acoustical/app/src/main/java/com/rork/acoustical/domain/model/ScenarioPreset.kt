package com.rork.acoustical.domain.model

import kotlinx.serialization.Serializable

/**
 * Pre-configured acoustic scenarios for different environments.
 * Each preset tunes the engine parameters optimally for its context.
 */
@Serializable
data class ScenarioPreset(
    val id: String,
    val name: String,
    val description: String,
    val targetSpl: Float,
    val maxGainDb: Float,
    val bandCount: BandCount,
    val smoothingFactor: Float,
    val noiseSubtractionEnabled: Boolean,
    val analysisInterval: AnalysisInterval,
    val recommendedDelayMs: Float
) {
    companion object {
        val CONCERT = ScenarioPreset(
            id = "CONCERT",
            name = "Concierto",
            description = "Sala grande, PA de alta potencia. Prioridad a SPL alto y corrección agresiva.",
            targetSpl = 95f,
            maxGainDb = 15f,
            bandCount = BandCount.BANDS_31,
            smoothingFactor = 0.15f,
            noiseSubtractionEnabled = true,
            analysisInterval = AnalysisInterval.FAST,
            recommendedDelayMs = 25f
        )

        val THEATER = ScenarioPreset(
            id = "THEATER",
            name = "Teatro",
            description = "Voz y música teatral. Corrección suave priorizando inteligibilidad.",
            targetSpl = 72f,
            maxGainDb = 8f,
            bandCount = BandCount.BANDS_16,
            smoothingFactor = 0.4f,
            noiseSubtractionEnabled = true,
            analysisInterval = AnalysisInterval.NORMAL,
            recommendedDelayMs = 35f
        )

        val STUDIO = ScenarioPreset(
            id = "STUDIO",
            name = "Estudio",
            description = "Control de grabación. Máxima resolución espectral con corrección mínima.",
            targetSpl = 78f,
            maxGainDb = 6f,
            bandCount = BandCount.BANDS_31,
            smoothingFactor = 0.6f,
            noiseSubtractionEnabled = true,
            analysisInterval = AnalysisInterval.BALANCED,
            recommendedDelayMs = 25f
        )

        val OUTDOOR = ScenarioPreset(
            id = "OUTDOOR",
            name = "Exterior",
            description = "Festival o evento al aire libre. Sin paredes, menos reverberación.",
            targetSpl = 97f,
            maxGainDb = 12f,
            bandCount = BandCount.BANDS_16,
            smoothingFactor = 0.2f,
            noiseSubtractionEnabled = false,
            analysisInterval = AnalysisInterval.FAST,
            recommendedDelayMs = 50f
        )

        val CINEMA = ScenarioPreset(
            id = "CINEMA",
            name = "Cine",
            description = "Sala de cine con sistema envolvente. Calibración precisa por banda.",
            targetSpl = 85f,
            maxGainDb = 10f,
            bandCount = BandCount.BANDS_16,
            smoothingFactor = 0.35f,
            noiseSubtractionEnabled = true,
            analysisInterval = AnalysisInterval.NORMAL,
            recommendedDelayMs = 45f
        )

        val CONFERENCE = ScenarioPreset(
            id = "CONFERENCE",
            name = "Conferencia",
            description = "Voz hablada, sistema de megafonía. Inteligibilidad máxima.",
            targetSpl = 70f,
            maxGainDb = 6f,
            bandCount = BandCount.BANDS_10,
            smoothingFactor = 0.5f,
            noiseSubtractionEnabled = true,
            analysisInterval = AnalysisInterval.NORMAL,
            recommendedDelayMs = 30f
        )

        val CUSTOM = ScenarioPreset(
            id = "CUSTOM",
            name = "Personalizado",
            description = "Configuración manual sin preset aplicado.",
            targetSpl = 75f,
            maxGainDb = 12f,
            bandCount = BandCount.BANDS_10,
            smoothingFactor = 0.3f,
            noiseSubtractionEnabled = true,
            analysisInterval = AnalysisInterval.NORMAL,
            recommendedDelayMs = 25f
        )

        val all: List<ScenarioPreset> = listOf(
            CONCERT, THEATER, STUDIO, OUTDOOR, CINEMA, CONFERENCE, CUSTOM
        )

        fun byId(id: String): ScenarioPreset = all.firstOrNull { it.id == id } ?: CUSTOM
    }
}
