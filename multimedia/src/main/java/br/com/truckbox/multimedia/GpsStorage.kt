package br.com.truckbox.multimedia

import android.content.ContentValues
import android.content.Context
import android.database.DatabaseUtils
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.AtomicFile
import android.util.Base64
import androidx.core.database.sqlite.transaction
import org.json.JSONObject
import java.io.File
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

private const val KEY_ALIAS = "truckbox-multimedia-gps"
private const val CIPHER = "AES/GCM/NoPadding"

internal class GpsSettings(context: Context) {
    private val preferences = context.getSharedPreferences("truckbox_multimedia", Context.MODE_PRIVATE)
    private val encrypted = AtomicFile(File(context.noBackupFilesDir, "gps-credentials.json"))

    var enabled: Boolean
        get() = preferences.getBoolean("gps_sharing_enabled", false)
        set(value) { preferences.edit().putBoolean("gps_sharing_enabled", value).apply() }

    val installId: String
        get() = preferences.getString("gps_install_id", null) ?: UUID.randomUUID().toString().also {
            preferences.edit().putString("gps_install_id", it).apply()
        }

    val deviceUid: String
        get() = readConfig()?.optString("device_uid")?.takeIf { it.isNotBlank() } ?: "TBX-FH540-GPS01"

    private fun readConfig(): JSONObject? = runCatching { JSONObject(encrypted.readFully().decodeToString()) }.getOrNull()

    fun credentials(): GpsCredentials? = runCatching {
        val config = readConfig() ?: return null
        val uid = config.getString("device_uid")
        val key = keyStore().getKey(KEY_ALIAS, null) as? SecretKey ?: return null
        val cipher = Cipher.getInstance(CIPHER)
        cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, Base64.decode(config.getString("iv"), Base64.DEFAULT)))
        cipher.updateAAD(uid.toByteArray(Charsets.UTF_8))
        val token = cipher.doFinal(Base64.decode(config.getString("token"), Base64.DEFAULT)).decodeToString()
        GpsCredentials(uid, token).takeIf(GpsCredentials::isValid)
    }.getOrNull()

    fun save(credentials: GpsCredentials): Boolean = runCatching {
        val key = keyStore().getKey(KEY_ALIAS, null) as? SecretKey ?: KeyGenerator.getInstance(
            KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore",
        ).apply {
            init(KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build())
        }.generateKey()
        val cipher = Cipher.getInstance(CIPHER)
        cipher.init(Cipher.ENCRYPT_MODE, key)
        cipher.updateAAD(credentials.deviceUid.toByteArray(Charsets.UTF_8))
        val data = JSONObject().apply {
            put("device_uid", credentials.deviceUid)
            put("iv", Base64.encodeToString(cipher.iv, Base64.NO_WRAP))
            put("token", Base64.encodeToString(cipher.doFinal(credentials.token.toByteArray(Charsets.UTF_8)), Base64.NO_WRAP))
        }.toString().toByteArray(Charsets.UTF_8)
        val stream = encrypted.startWrite()
        try {
            stream.write(data)
            encrypted.finishWrite(stream)
        } catch (failure: Exception) {
            encrypted.failWrite(stream)
            throw failure
        }
    }.isSuccess

    private fun keyStore() = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
}

internal class GpsOutbox(context: Context) : SQLiteOpenHelper(
    context, File(context.noBackupFilesDir, "gps-points.db").absolutePath, null, 1,
) {
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("""create table points (
            device_uid text not null, client_uid text not null, captured_ms integer not null, payload text not null,
            primary key (device_uid, client_uid)
        )""")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        error("Migração da fila de GPS indisponível")
    }

    fun enqueue(deviceUid: String, point: GpsPoint): Boolean = writableDatabase.transaction {
        if (DatabaseUtils.queryNumEntries(this, "points") >= GPS_QUEUE_LIMIT) return@transaction false
        val row = ContentValues().apply {
            put("device_uid", deviceUid)
            put("client_uid", point.clientUid)
            put("captured_ms", point.capturedAt)
            put("payload", point.json)
        }
        insertWithOnConflict("points", null, row, SQLiteDatabase.CONFLICT_IGNORE)
        true
    }

    fun oldest(deviceUid: String): GpsPoint? = readableDatabase.query(
        "points", arrayOf("client_uid", "captured_ms", "payload"), "device_uid=?", arrayOf(deviceUid),
        null, null, "rowid", "1",
    ).use { cursor ->
        if (cursor.moveToFirst()) GpsPoint(cursor.getString(0), cursor.getLong(1), cursor.getString(2)) else null
    }

    fun pending(deviceUid: String): Long = DatabaseUtils.queryNumEntries(
        readableDatabase, "points", "device_uid=?", arrayOf(deviceUid),
    )

    fun acknowledge(deviceUid: String, point: GpsPoint) {
        writableDatabase.delete("points", "device_uid=? and client_uid=?", arrayOf(deviceUid, point.clientUid))
    }
}
