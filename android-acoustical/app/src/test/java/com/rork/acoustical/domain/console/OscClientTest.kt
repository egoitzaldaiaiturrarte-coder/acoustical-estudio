package com.rork.acoustical.domain.console

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Round-trip tests (encode → decode through [OscClient]) for the OSC string
 * codec.
 *
 * The bug fixed here: the encoder never emitted the NUL terminator when the
 * string length was a multiple of 4 (and used US_ASCII, corrupting UTF-8),
 * while the decoder skipped blocks by a length that assumed the terminator —
 * so a length-4 string desynchronized the whole message parse. The decoder
 * now derives the block size from the NUL's own 4-byte block, decodes as
 * UTF-8, and the encoder always emits the terminator inside the block.
 *
 * The codec methods are `internal` precisely so these JVM tests can exercise
 * them without a UDP socket.
 */
class OscClientTest {

    private val client = OscClient()

    /** Encodes and decodes the message, returning the decoded form. */
    private fun roundTrip(address: String, vararg args: OscArgument): OscMessage? =
        client.decodeOscMessage(client.encodeOscMessage(address, args.toList()))

    /**
     * The core regression: every string length 0-5 and 16/17 must round-trip
     * exactly, and — crucially — a following float argument must decode from
     * its correct block (proof the parser did not desynchronize).
     */
    @Test
    fun `string lengths 0 to 5 and 16/17 - exact round trip, following float intact`() {
        for (n in listOf(0, 1, 2, 3, 4, 5, 16, 17)) {
            val s = "x".repeat(n)
            val msg = roundTrip("/len/$n", OscArgument.StringArg(s), OscArgument.FloatArg(1.5f))
            assertNotNull("length $n: decode failed", msg)
            assertEquals("length $n: address", "/len/$n", msg!!.address)
            assertEquals(2, msg.args.size)
            val str = msg.args[0] as? OscArgument.StringArg
            assertNotNull("length $n: missing string arg", str)
            assertEquals("length $n: string value", s, str!!.value)
            val fl = msg.args[1] as? OscArgument.FloatArg
            assertNotNull("length $n: following float desynced", fl)
            assertEquals("length $n: following float value", 1.5f, fl!!.value, 0f)
        }
    }

    @Test
    fun `utf-8 multibyte - español y emoji sobreviven al round trip`() {
        // 'ó' is 2 bytes in UTF-8 (11 bytes total + 1 NUL = 12, a block
        // multiple of 4 that the old encoder corrupted via US_ASCII→'?' and
        // the old decoder read back byte-by-byte as Latin-1).
        val es = "calibración"
        val msg = roundTrip("/test/es", OscArgument.StringArg(es))
        assertNotNull(msg)
        assertEquals(es, (msg!!.args[0] as OscArgument.StringArg).value)

        // Emoji: surrogate pair in UTF-16, 4 bytes in UTF-8, plus a
        // variation selector (3 bytes) → 7 UTF-8 bytes + NUL = 18, pad 2.
        val emoji = "🎛️"
        val msg2 = roundTrip("/test/emoji", OscArgument.StringArg(emoji))
        assertNotNull(msg2)
        assertEquals(emoji, (msg2!!.args[0] as OscArgument.StringArg).value)
    }

    @Test
    fun `NUL embebido a mitad de la string - el parseo sobrevive (truncado por spec)`() {
        // OSC 1.0 is NUL-terminated, so an embedded 0x00 is ambiguous: the
        // string is truncated at the FIRST zero. What must NOT happen is the
        // parser dying or running off the rails: the message decodes, the
        // address is intact and the (truncated) string stops at the NUL.
        val msg = roundTrip("/nul", OscArgument.StringArg("abc\u0000def"))
        assertNotNull("embedded NUL must not kill the decode", msg)
        assertEquals("/nul", msg!!.address)
        assertEquals(1, msg.args.size)
        assertEquals("abc", (msg.args[0] as OscArgument.StringArg).value)
    }

    @Test
    fun `floats y ints en binario - round trip exacto`() {
        val floats = listOf(0f, 1.5f, -1.5f, 1e-30f, Float.MAX_VALUE, -Float.MAX_VALUE)
        for (v in floats) {
            val msg = roundTrip("/f", OscArgument.FloatArg(v))
            assertNotNull("float $v", msg)
            assertEquals("float $v", v, (msg!!.args[0] as OscArgument.FloatArg).value, 0f)
        }
        val ints = listOf(0, -1, 12345, Int.MAX_VALUE, Int.MIN_VALUE)
        for (v in ints) {
            val msg = roundTrip("/i", OscArgument.IntArg(v))
            assertNotNull("int $v", msg)
            assertEquals("int $v", v, (msg!!.args[0] as OscArgument.IntArg).value)
        }
    }

    @Test
    fun `mensaje mixto multi-argument - todo en su sitio`() {
        // A length-4 string (the historical desync case) followed by mixed
        // args: the block alignment must carry through the whole message.
        val msg = roundTrip(
            "/ch/01/eq/1",
            OscArgument.StringArg("abcd"),
            OscArgument.FloatArg(3.5f),
            OscArgument.IntArg(-7),
            OscArgument.StringArg("banda")
        )
        assertNotNull(msg)
        assertEquals("/ch/01/eq/1", msg!!.address)
        assertEquals(4, msg.args.size)
        assertEquals("abcd", (msg.args[0] as OscArgument.StringArg).value)
        assertEquals(3.5f, (msg.args[1] as OscArgument.FloatArg).value, 0f)
        assertEquals(-7, (msg.args[2] as OscArgument.IntArg).value)
        assertEquals("banda", (msg.args[3] as OscArgument.StringArg).value)
    }

    @Test
    fun `solo direccion sin argumentos - se decodifica vacio`() {
        val msg = roundTrip("/solo/direccion")
        assertNotNull(msg)
        assertEquals("/solo/direccion", msg!!.address)
        assertTrue(msg.args.isEmpty())
    }

    @Test
    fun `mensaje truncado sin terminador - no lanza ni se desboca`() {
        // A well-formed first block whose NUL was chopped off: the decoder
        // must read to the end of the buffer instead of scanning past it.
        val full = client.encodeOscMessage("/t", listOf(OscArgument.StringArg("hello")))
        val truncated = full.copyOfRange(0, full.size - 1) // chop the final pad byte
        val msg = client.decodeOscMessage(truncated)
        assertNotNull(msg)
        assertEquals("/t", msg!!.address)
    }
}
