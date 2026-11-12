package br.com.truckbox.app.diagnostics
import org.junit.Assert.*
import org.junit.Test
import org.json.JSONArray
import org.json.JSONObject

class DiagnosticMergeTest {
    private fun fault(mid: Int, state: String, boot: Int, time: Int) = JSONObject().put("mid", mid)
        .put("identifier_type", "SID").put("identifier", 18).put("fmi", 5).put("state", state)
        .put("occurrences", if (state == "INACTIVE") 0 else JSONObject.NULL)
        .put("first", JSONArray(listOf(boot, time, JSONObject.NULL))).put("last", JSONArray(listOf(boot, time, JSONObject.NULL)))
    @Test fun olderPagesDoNotReviveFaultsAndModulesStaySeparate() {
        val current = JSONArray().put(fault(128, "INACTIVE", 306, 5000)).put(fault(136, "ACTIVE", 306, 4000))
        val old = JSONArray().put(fault(128, "ACTIVE", 290, 1000))
        val merged = DiagnosticMerge.faults(current, old)
        assertEquals(2, merged.length()); assertEquals("INACTIVE", merged.getJSONObject(0).getString("state"))
        assertEquals(290L, merged.getJSONObject(0).getJSONArray("first").getLong(0))
        assertEquals(0, merged.getJSONObject(0).getInt("occurrences"))
        assertTrue(merged.getJSONObject(1).isNull("occurrences"))
        assertEquals(2, DiagnosticMerge.faults(merged, JSONArray()).length())
    }
    @Test fun counterPreferenceNeedsSameTripFreshDataAndValidDelta() {
        val value = JSONObject().put("trip_client_uid", "trip-1").put("ecu_reference_fresh", true)
            .put("interval_quality", "DELTA_ECU_EM_VALIDACAO").put("ecu_trip_fuel_l", 12.25)
        assertEquals(12.25, DiagnosticMerge.fuelReference(value, "trip-1")!!, 0.0)
        assertNull(DiagnosticMerge.fuelReference(value, "trip-2"))
        value.put("ecu_reference_fresh", false); assertNull(DiagnosticMerge.fuelReference(value, "trip-1"))
        value.put("ecu_reference_fresh", true).put("ecu_trip_fuel_l", JSONObject.NULL)
        assertNull(DiagnosticMerge.fuelReference(value, "trip-1"))
        value.put("ecu_trip_fuel_l", 0.0); assertEquals(0.0, DiagnosticMerge.fuelReference(value, "trip-1")!!, 0.0)
        value.put("interval_quality", "RESET_OR_ECU_REPLACEMENT"); assertNull(DiagnosticMerge.fuelReference(value, "trip-1"))
    }
}
