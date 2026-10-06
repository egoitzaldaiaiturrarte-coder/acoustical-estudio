package com.rork.acoustical.service

import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM tests for the pure functions extracted from [PhoneSyncManager]
 * (top-level `internal` in the same file): [isNewerVersion],
 * [githubRepoOf] and the sync-payload classification
 * [SyncPayloadDecision.evaluate].
 */
class PhoneSyncParsersTest {

    // --- isNewerVersion ---------------------------------------------------

    @Test
    fun `version decimal - 1.10 es mayor que 1.9 (no comparacion alfabetica)`() {
        assertTrue(isNewerVersion("1.10", "1.9"))
        assertFalse(isNewerVersion("1.9", "1.10"))
    }

    @Test
    fun `version igual - no es nueva (incluido el relleno con ceros)`() {
        assertFalse(isNewerVersion("1.2.3", "1.2.3"))
        assertFalse(isNewerVersion("1.2.3", "1.2.3.0"))
        assertFalse(isNewerVersion("1.2", "1.2"))
    }

    @Test
    fun `sin version instalada - cualquier candidata es nueva`() {
        assertTrue(isNewerVersion("0.1", null))
        assertTrue(isNewerVersion("0.1", ""))
    }

    @Test
    fun `el menor componente diferencia decide`() {
        assertTrue(isNewerVersion("2.0", "1.99.9"))
        assertFalse(isNewerVersion("1.9.8", "1.9.9"))
        assertTrue(isNewerVersion("1.9.9", "1.9.8"))
    }

    // --- githubRepoOf ------------------------------------------------------

    @Test
    fun `githubRepoOf - urls github.com con y sin sufijo .git`() {
        assertEquals("usuario/repo", githubRepoOf("https://github.com/usuario/repo"))
        assertEquals("usuario/repo", githubRepoOf("github.com/usuario/repo.git"))
        assertEquals("usuario/repo", githubRepoOf("github.com/usuario/repo"))
    }

    @Test
    fun `githubRepoOf - formato corto usuario/repo`() {
        assertEquals("acoustical/estudio", githubRepoOf("acoustical/estudio"))
    }

    @Test
    fun `githubRepoOf - un enlace directo no es un repositorio`() {
        assertNull(githubRepoOf("https://example.com/dl/Setup-1.2.0.exe"))
        assertNull(githubRepoOf("Setup-1.2.0.exe"))
    }

    // --- SyncPayloadDecision (parse del JSON de sync) ----------------------

    @Test
    fun `payload no valido - se descarta sin excepcion`() {
        assertNull(SyncPayloadDecision.evaluate("esto no es json"))
        assertNull(SyncPayloadDecision.evaluate(""))
        // Valid JSON but not an object: not a sync payload either.
        assertNull(SyncPayloadDecision.evaluate("[1,2]"))
        assertNull(SyncPayloadDecision.evaluate("\"hola\""))
    }

    @Test
    fun `acked sin sendToPc - es la confirmacion del PC (no hay config pendiente)`() {
        val d = SyncPayloadDecision.evaluate(
            """{"type":"sync","ok":true,"acked":true,"config":{"maxGainDb":6}}"""
        )
        assertNotNull("ack payload must be classified", d)
        assertTrue(d!!.isAck)
        assertNull(d.pendingConfig)
    }

    @Test
    fun `acked junto a sendToPc - no es una confirmacion (es estado telefono->PC)`() {
        val d = SyncPayloadDecision.evaluate("""{"acked":true,"sendToPc":true,"config":{}}""")
        assertNotNull(d)
        assertFalse(d!!.isAck)
    }

    @Test
    fun `config en el payload - se extrae como pendiente de aplicar`() {
        val d = SyncPayloadDecision.evaluate("""{"type":"sync","config":{"maxGainDb":6.0}}""")
        assertNotNull(d)
        assertFalse(d!!.isAck)
        val cfg = d.pendingConfig
        assertNotNull(cfg)
        assertEquals(6.0, cfg!!["maxGainDb"]!!.jsonPrimitive.double)
    }
}
