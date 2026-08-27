package br.com.truckbox.app.repository

import android.content.Context
import android.os.SystemClock
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class RawRingBuffer(
    private val maxAgeMs: Long = 120_000L,
    private val maxFrames: Int = 250_000,
) {
    private data class Entry(val receivedAtMs: Long, val line: String)
    private val q = ArrayDeque<Entry>()

    @Synchronized
    fun add(line: String) {
        val now = SystemClock.elapsedRealtime()
        q.add(Entry(now, line))
        trim(now)
    }

    @Synchronized
    fun size(): Int = q.size

    @Synchronized
    fun snapshot(): List<String> = q.map { it.line }

    private fun trim(now: Long) {
        while (q.isNotEmpty() && (now - q.first().receivedAtMs > maxAgeMs || q.size > maxFrames)) q.removeFirst()
    }

    fun save(context: Context, tag: String): File {
        val lines = snapshot()
        val dir = File(context.getExternalFilesDir(null), "field_logs").apply { mkdirs() }
        val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val safe = tag.replace(Regex("[^A-Za-z0-9_-]"), "_").take(32)
        val file = File(dir, "TruckBox_${stamp}_${safe}.tb1.log")
        file.bufferedWriter().use { out -> lines.forEach { out.appendLine(it) } }
        return file
    }
}
