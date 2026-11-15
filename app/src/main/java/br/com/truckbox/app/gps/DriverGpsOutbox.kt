package br.com.truckbox.app.gps

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteOpenHelper
import android.database.sqlite.SQLiteDatabase
import com.google.gson.Gson

data class QueuedGps(val scope: String,val sample: PhoneGpsSample)
enum class GpsEnqueue { INSERTED,DUPLICATE,FULL }

/** Private, durable queue. Stable client UID; remove only after exact Cloud ACK. */
class DriverGpsOutbox(context: Context,private val limit: Int=100000): SQLiteOpenHelper(context.applicationContext,"truckbox_driver_gps_v1.db",null,1) {
    private val gson=Gson()
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE points(scope TEXT NOT NULL,client_uid TEXT NOT NULL,captured_ms INTEGER NOT NULL,sample_json TEXT NOT NULL,PRIMARY KEY(scope,client_uid))")
        db.execSQL("CREATE INDEX points_capture ON points(scope,captured_ms)")
    }
    override fun onUpgrade(db: SQLiteDatabase,oldVersion: Int,newVersion: Int) {error("GPS database migration required")}
    @Synchronized fun enqueue(scope: String,sample: PhoneGpsSample): GpsEnqueue {
        require(scope.isNotBlank()&&sample.valid())
        val db=writableDatabase;db.beginTransaction()
        try {
            db.rawQuery("SELECT 1 FROM points WHERE scope=? AND client_uid=?",arrayOf(scope,sample.clientUid)).use {if(it.moveToFirst())return GpsEnqueue.DUPLICATE}
            if(countAll()>=limit)return GpsEnqueue.FULL
            db.insertOrThrow("points",null,ContentValues().apply {put("scope",scope);put("client_uid",sample.clientUid);put("captured_ms",sample.capturedAtMs);put("sample_json",gson.toJson(sample))})
            db.setTransactionSuccessful();return GpsEnqueue.INSERTED
        } finally {db.endTransaction()}
    }
    @Synchronized fun countAll(): Int=readableDatabase.rawQuery("SELECT COUNT(*) FROM points",null).use {it.moveToFirst();it.getInt(0)}
    @Synchronized fun count(scope: String): Int=readableDatabase.rawQuery("SELECT COUNT(*) FROM points WHERE scope=?",arrayOf(scope)).use {it.moveToFirst();it.getInt(0)}
    @Synchronized fun next(scope: String,newest: Boolean=false): QueuedGps? {
        val order=if(newest)"DESC" else "ASC"
        return readableDatabase.rawQuery("SELECT sample_json FROM points WHERE scope=? ORDER BY captured_ms $order,client_uid LIMIT 1",arrayOf(scope)).use {
            if(!it.moveToFirst())null else QueuedGps(scope,gson.fromJson(it.getString(0),PhoneGpsSample::class.java).also {s->require(s.valid())})
        }
    }
    @Synchronized fun acknowledge(item: QueuedGps): Boolean=writableDatabase.delete("points","scope=? AND client_uid=?",arrayOf(item.scope,item.sample.clientUid))==1
}
