package br.com.truckbox.app.gateway

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import org.json.JSONArray
import org.json.JSONObject

/**
 * Fila duravel de telemetria. O ACK do ESP32 so acontece depois do COMMIT no
 * SQLite (journal + synchronous=FULL). O ACK da Cloud exclui apenas amostras
 * que fizeram parte do lote confirmado.
 *
 * Nunca limpar esta base automaticamente ao iniciar o app ou perder internet.
 */
class TelemetrySpool(context: Context) {
    companion object {
        const val MAX_PENDING_BYTES = 512L * 1024L * 1024L
        private const val MIN_FREE_BYTES = 64L * 1024L * 1024L
        private const val DATABASE_NAME = "truckbox_telemetry_queue.db"
    }

    data class Stats(val count: Long, val bytes: Long) {
        val nearLimit: Boolean get() = bytes >= MAX_PENDING_BYTES * 4 / 5
    }

    data class UploadBatch(
        val deviceUid: String,
        val samples: JSONArray,
        val sequences: List<Long>,
        val payloadBytes: List<Int>,
    ) {
        val maxSeq: Long get() = sequences.maxOrNull() ?: 0L
    }

    class QueuePaused(message: String) : IllegalStateException(message)

    private val appContext = context.applicationContext
    private val helper = object : SQLiteOpenHelper(appContext, DATABASE_NAME, null, 1) {
        override fun onConfigure(db: SQLiteDatabase) {
            super.onConfigure(db)
            // Preferir durabilidade a throughput: ACK ao Core depende deste commit.
            db.execSQL("PRAGMA synchronous=FULL")
        }

        override fun onCreate(db: SQLiteDatabase) {
            db.execSQL(
                """CREATE TABLE samples (
                    device_uid TEXT NOT NULL,
                    seq INTEGER NOT NULL,
                    payload TEXT NOT NULL,
                    bytes INTEGER NOT NULL,
                    PRIMARY KEY(device_uid, seq)
                )"""
            )
            db.execSQL(
                """CREATE TABLE queue_stats (
                    id INTEGER PRIMARY KEY CHECK(id=1),
                    count_items INTEGER NOT NULL,
                    pending_bytes INTEGER NOT NULL
                )"""
            )
            db.execSQL("INSERT INTO queue_stats (id,count_items,pending_bytes) VALUES(1,0,0)")
        }

        override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
            // Sem migracao destrutiva: os dados pendentes nunca podem ser descartados.
            throw IllegalStateException("Migracao da fila de telemetria nao implementada")
        }
    }

    private fun statsIn(db: SQLiteDatabase): Stats {
        db.rawQuery("SELECT count_items, pending_bytes FROM queue_stats WHERE id=1", null).use { c ->
            if (!c.moveToFirst()) throw QueuePaused("Estado da fila Android ausente")
            return Stats(c.getLong(0), c.getLong(1))
        }
    }

    @Synchronized
    fun stats(): Stats = statsIn(helper.readableDatabase)

    /**
     * Persistencia *antes* do ACK Core. Duplicatas de lotes ainda nao
     * confirmados sao aceitas so quando o conteudo armazenado e identico.
     */
    @Synchronized
    fun persist(batch: GatewayBatch, configuredDeviceUid: String): Stats {
        if (configuredDeviceUid.isBlank() || batch.deviceUid.isBlank() ||
            batch.deviceUid != configuredDeviceUid
        ) throw QueuePaused("UID Core/Cloud diferente: configure o dispositivo correto")

        if (batch.count <= 0 || batch.samples.length() != batch.count || batch.maxSeq <= 0) {
            throw QueuePaused("Lote ESP32 invalido; fila mantida no Core")
        }

        val rows = ArrayList<Pair<Long, String>>(batch.samples.length())
        val seen = HashSet<Long>()
        for (i in 0 until batch.samples.length()) {
            val item = batch.samples.optJSONObject(i)
                ?: throw QueuePaused("Amostra nao e objeto JSON")
            val seq = item.optLong("seq", 0L)
            if (seq <= 0L || !seen.add(seq)) {
                throw QueuePaused("Sequencia de telemetria ausente ou repetida")
            }
            rows += seq to item.toString()
        }
        if (rows.maxOf { it.first } != batch.maxSeq) {
            throw QueuePaused("Sequencia maxima do lote nao confere")
        }

        val db = helper.writableDatabase
        db.beginTransaction()
        try {
            val before = statsIn(db)
            var addedCount = 0L
            var addedBytes = 0L
            for ((seq, json) in rows) {
                val previous = db.rawQuery(
                    "SELECT payload FROM samples WHERE device_uid=? AND seq=?",
                    arrayOf(batch.deviceUid, seq.toString())
                ).use { c -> if (c.moveToFirst()) c.getString(0) else null }
                if (previous != null) {
                    if (previous != json) {
                        throw QueuePaused("Conflito de seq $seq: envio bloqueado para nao perder dados")
                    }
                    continue
                }

                val size = json.toByteArray(Charsets.UTF_8).size
                if (before.bytes + addedBytes + size > MAX_PENDING_BYTES) {
                    throw QueuePaused("Fila Android atingiu 512 MB; ESP32 nao sera liberado")
                }
                if (appContext.filesDir.usableSpace < MIN_FREE_BYTES + size) {
                    throw QueuePaused("Pouco espaco no celular; ESP32 nao sera liberado")
                }
                val values = ContentValues().apply {
                    put("device_uid", batch.deviceUid)
                    put("seq", seq)
                    put("payload", json)
                    put("bytes", size)
                }
                db.insertOrThrow("samples", null, values)
                addedBytes += size
                addedCount++
            }
            if (addedCount > 0L) {
                db.execSQL(
                    "UPDATE queue_stats SET count_items=count_items+?, pending_bytes=pending_bytes+? WHERE id=1",
                    arrayOf(addedCount, addedBytes)
                )
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
        }
        return stats()
    }

    /** Entrega o dispositivo mais antigo primeiro, sem misturar UIDs. */
    @Synchronized
    fun nextBatch(limit: Int = 8): UploadBatch? {
        val db = helper.readableDatabase
        val firstUid = db.rawQuery(
            "SELECT device_uid FROM samples ORDER BY rowid ASC LIMIT 1", null
        ).use { c -> if (c.moveToFirst()) c.getString(0) else null } ?: return null

        val samples = JSONArray()
        val sequences = ArrayList<Long>()
        val sizes = ArrayList<Int>()
        db.rawQuery(
            "SELECT seq,payload,bytes FROM samples WHERE device_uid=? ORDER BY seq ASC LIMIT ?",
            arrayOf(firstUid, limit.coerceIn(1, 8).toString())
        ).use { c ->
            while (c.moveToNext()) {
                sequences += c.getLong(0)
                samples.put(JSONObject(c.getString(1)))
                sizes += c.getInt(2)
            }
        }
        if (sequences.isEmpty()) return null
        return UploadBatch(firstUid, samples, sequences, sizes)
    }

    /** Confirma SOMENTE as amostras efetivamente oferecidas a Cloud. */
    @Synchronized
    fun confirmUploaded(batch: UploadBatch, cloudAckSeq: Long): Stats {
        if (batch.sequences.isEmpty() || cloudAckSeq < batch.maxSeq) {
            throw QueuePaused("Cloud ainda nao confirmou todas as amostras")
        }
        val db = helper.writableDatabase
        db.beginTransaction()
        try {
            var removedCount = 0L
            var removedBytes = 0L
            for (i in batch.sequences.indices) {
                val removed = db.delete(
                    "samples", "device_uid=? AND seq=?",
                    arrayOf(batch.deviceUid, batch.sequences[i].toString())
                )
                if (removed == 1) {
                    removedCount++
                    removedBytes += batch.payloadBytes[i]
                }
            }
            if (removedCount > 0) {
                db.execSQL(
                    "UPDATE queue_stats SET count_items=count_items-?, pending_bytes=pending_bytes-? WHERE id=1",
                    arrayOf(removedCount, removedBytes)
                )
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
        }
        return stats()
    }
}
