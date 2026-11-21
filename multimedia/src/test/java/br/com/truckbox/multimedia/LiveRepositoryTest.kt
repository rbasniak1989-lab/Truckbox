package br.com.truckbox.multimedia

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.Executor

class LiveRepositoryTest {
    private val epoch = 1795262400000L
    private val immediate=Executor { it.run() }
    private fun local()=JSONObject("""{"ok":true,"diagnostic_partial":true,"live_contract":"TBX_CORE_LIVE_V1","device_uid":"core-A","rpm":1500,"gear":12,"actualTorquePct":32.811199,"torqueNm":867.2,"fuel_pressure_bar":4.4505}""")
    private fun cloud()=JSONObject("""{"ok":true,"online":true,"rpm":1000,"gear":4,"trip_distance_km":20,"trip_fuel_l":4,"trip_average_kml":5,"trip":{"status":"ACTIVE","origin":"A","destination":"B"},"operation":{"mode":"LOADED"}}""")
        .put("latest",JSONObject().put("ts",java.time.Instant.ofEpochMilli(epoch).toString()))
    private fun diagnostics()=JSONObject("""{"ok":true,"device_uid":"core-A","next_offset":32,"faults":[{"mid":130,"identifier_type":"PID","identifier":37,"fmi":1,"state":"INACTIVE","occurrences":0,"parameter_hex":"25E100","last":[610,1000,null]}]}""")
    @Test fun coreLiveAndTripCloudAreMergedWithoutReplacingSensors() {
        var now=0L;var cloudCalls=0;var diagnosticCalls=0
        val repo=LiveRepository("192.168.4.1",::parseLiveState,{u,_,_->
            when {u.contains("/api/live")->local();u.contains("/api/diagnostics")-> {diagnosticCalls++;diagnostics()};else->{cloudCalls++;cloud()} }
        },immediate,{epoch+now})
        repo.fetch();now=1000;val s=repo.fetch()!!
        assertEquals(12,s.currentGear);assertEquals(1500.0,s.rpm!!,.0001)
        assertEquals(32.811199,s.torquePct!!,.000001);assertEquals(4.4505,s.fuelPressureBar!!,.000001)
        assertEquals(20.0,s.tripDistanceKm!!,.0001);assertEquals(4.0,s.tripFuelLiters!!,.0001)
        assertEquals(0,s.ecuFaults.single().occurrences);assertTrue(s.diagnosticsAvailable)
        repeat(4){now+=1000;repo.fetch()};assertEquals(1,cloudCalls);assertEquals(1,diagnosticCalls)
    }
    @Test fun noInternetDoesNotBlockLocalReadingsOrEraseFaultHistory() {
        var now=0L;var online=true
        val repo=LiveRepository("192.168.4.1",::parseLiveState,{u,_,_->
            when {u.contains("/api/live")->local();u.contains("/api/diagnostics")->if(online)diagnostics() else null;else->if(online)cloud() else null}
        },immediate,{epoch+now})
        repo.fetch();now=1;repo.fetch();online=false;now=31000;repo.fetch();now=31001
        val s=repo.fetch()!!;assertTrue(s.online);assertEquals(12,s.currentGear)
        assertNull(s.tripDistanceKm);assertFalse(s.tripCloudAvailable);assertFalse(s.diagnosticsAvailable)
        assertEquals(1,s.ecuFaults.size)
    }
    @Test fun oldDiagnosticOnlyStateIsNotAcceptedAsAnInstrumentPayload() {
        var now=0L
        val repo=LiveRepository("192.168.4.1",::parseLiveState,{u,_,_->
            when {u.contains("/api/live")->null;u.contains("/api/state")->JSONObject("""{"ok":true,"diagnostic_partial":true,"device_uid":"core-A","heap_free":40144}""");else->cloud()}
        },immediate,{epoch+now})
        assertNull(repo.fetch());now=1;assertEquals("CLOUD",repo.fetch()!!.source)
    }
    @Test fun tripParserNeverUsesLifetimeCountersOrOldHistory() {
        val j=JSONObject("""{"fuelLitersTest":9000,"distanceTestKm":1000,"history":[{"trip_fuel_l":99,"trip_distance_km":999}]}""")
        assertNull(cloudTripSummary(j));j.put("trip",JSONObject("""{"status":"ACTIVE"}"""))
        val trip=cloudTripSummary(j)!!;assertNull(trip.fuel);assertNull(trip.distance);assertNull(trip.average)
        j.put("trip_fuel_l",0);j.put("trip_distance_km",0);assertEquals(0.0,cloudTripSummary(j)!!.fuel!!,.0)
    }
    @Test fun replayWithRecentReceiptNeverResurrectsOldTelemetry() {
        val response = cloud()
        response.getJSONObject("latest").put("updated_at",java.time.Instant.ofEpochMilli(epoch+60_000).toString())
        val repo=LiveRepository("192.168.4.1",::parseLiveState,{u,_,_->
            if(u.contains("/api/live")) local() else response
        },immediate,{epoch+60_000})
        repo.fetch();val s=repo.fetch()!!
        assertEquals(1500.0,s.rpm!!,.0001)
        assertFalse(s.tripCloudAvailable)
        assertNull(s.tripFuelLiters)
    }
    @Test fun onlyMatchingTripCanUseMeasuredPid250() {
        val j=cloud()
        j.getJSONObject("trip").put("started_at",java.time.Instant.ofEpochMilli(epoch-1000).toString())
            .put("start_odometer_km",12345)
        val raw = JSONObject("""{"source":"REVB1_LINK_LITE","fuel_quality":"CONFIRMED_ECU_PID250","trip_fuel_l":5,"lite_start_odometer_km":12345}""")
            .put("lite_reference_ms",epoch).put("lite_pid250_reference_ms",epoch)
        j.getJSONObject("latest").put("data",JSONObject().put("raw",raw))
        val measured=cloudTripSummary(j)!!
        assertTrue(measured.fuelConfirmed); assertEquals(5.0,measured.fuel!!,0.0001)
        assertEquals(4.0,measured.average!!,0.0001)
        raw.put("lite_pid250_reference_ms",epoch+10_000)
        val estimated=cloudTripSummary(j)!!
        assertFalse(estimated.fuelConfirmed); assertEquals(4.0,estimated.fuel!!,0.0001)
    }
}
