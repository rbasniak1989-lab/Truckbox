package br.com.truckbox.app.parser

data class J1939Frame(
    val timestampMs: Long,
    val canId: Long,
    val pgn: Int,
    val sa: Int,
    val dlc: Int,
    val data: ByteArray,
)

object Tb1LineParser {
    fun parse(line: String): J1939Frame? {
        val p = line.trim().split(',')
        if (p.size != 7 || p[0] != "TB1") return null
        return try {
            val ts = p[1].toLong()
            val canId = p[2].toLong(16)
            val pgn = p[3].toInt()
            val sa = p[4].toInt()
            val dlc = p[5].toInt()
            if (dlc !in 0..8) return null
            val hex = p[6].trim()
            if (hex.length < dlc * 2) return null
            val bytes = ByteArray(dlc) { i -> hex.substring(i * 2, i * 2 + 2).toInt(16).toByte() }
            J1939Frame(ts, canId, pgn, sa, dlc, bytes)
        } catch (_: Throwable) {
            null
        }
    }
}

internal fun ByteArray.u8(i: Int): Int = this[i].toInt() and 0xFF
internal fun ByteArray.le16(i: Int): Int = u8(i) or (u8(i + 1) shl 8)
internal fun ByteArray.le32(i: Int): Long =
    u8(i).toLong() or (u8(i + 1).toLong() shl 8) or (u8(i + 2).toLong() shl 16) or (u8(i + 3).toLong() shl 24)
