package br.com.truckbox.app.gps

import android.location.Location
import java.time.Instant

data class PhoneGpsSample(
    val clientUid: String,val capturedAtMs: Long,val latitude: Double,val longitude: Double,
    val speedKmh: Double?=null,val headingDeg: Double?=null,val altitudeM: Double?=null,val accuracyM: Double?=null,val provider: String,
) {
    val timestamp: String get()=Instant.ofEpochMilli(capturedAtMs).toString()
    fun valid(): Boolean=clientUid.length in 1..200&&capturedAtMs>0&&latitude.isFinite()&&latitude in -90.0..90.0&&
        longitude.isFinite()&&longitude in -180.0..180.0&&provider.length in 1..40&&
        (speedKmh==null||(speedKmh.isFinite()&&speedKmh>=0))&&(headingDeg==null||(headingDeg.isFinite()&&headingDeg in 0.0..360.0))&&
        (altitudeM==null||altitudeM.isFinite())&&(accuracyM==null||(accuracyM.isFinite()&&accuracyM in 0.0..10000.0))
    companion object {
        @Suppress("DEPRECATION")
        fun fromLocation(location: Location,installId: String,nowMs: Long,elapsedNanos: Long): PhoneGpsSample? {
            if(location.isFromMockProvider||location.elapsedRealtimeNanos<=0||location.elapsedRealtimeNanos>elapsedNanos||
                elapsedNanos-location.elapsedRealtimeNanos>30_000_000_000L||location.time<=0||location.time>nowMs+60000)return null
            return PhoneGpsSample("$installId:${location.time}",location.time,location.latitude,location.longitude,
                if(location.hasSpeed())location.speed.toDouble()*3.6 else null,
                if(location.hasBearing())location.bearing.toDouble() else null,
                if(location.hasAltitude())location.altitude else null,
                if(location.hasAccuracy())location.accuracy.toDouble() else null,
                location.provider ?: return null).takeIf {it.valid()}
        }
    }
}
