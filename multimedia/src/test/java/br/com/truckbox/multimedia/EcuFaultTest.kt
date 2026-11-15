package br.com.truckbox.multimedia

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class EcuFaultTest {
    private fun page(mid: Int, type: String, number: Int, fmi: Int, state: String, count: String) = JSONObject(
        """{"faults":[{"mid":$mid,"identifier_type":"$type","identifier":$number,"fmi":$fmi,"state":"$state","occurrences":$count,"parameter_hex":"12B501","last":[610,1000,null]}]}""")
    @Test fun engineWaterCircuitIsNotWaterDetection() {
        val f = parseEcuFaults(page(128,"SID",18,5,"ACTIVE","1")).single()
        assertEquals(1,f.occurrences);assertEquals("ATIVA",f.stateText())
        assertTrue(f.candidateDescription()!!.contains("Não confirma água"));assertNull(f.lastUnixMs)
    }
    @Test fun inactiveZeroAndMissingCounterStayDistinct() {
        assertEquals(0,parseEcuFaults(page(130,"PID",37,1,"INACTIVE","0")).single().occurrences)
        assertNull(parseEcuFaults(page(136,"SID",2,5,"ACTIVE","null")).single().occurrences)
    }
    @Test fun modulesAndUnknownExtendedCodesArePreserved() {
        val f = parseEcuFaults(page(223,"PSID",439,15,"UNKNOWN","null")).single()
        assertNull(f.candidateDescription());assertTrue(f.technicalCode().contains("439"))
        assertTrue(parseEcuFaults(page(10,"PID",37,1,"ACTIVE","1")).isEmpty())
    }
    @Test fun absenceDoesNotResolveAndTransitionsDoNotDuplicate() {
        val c=EcuFaultCache();val active=parseEcuFaults(page(130,"PID",37,1,"ACTIVE","1"))
        c.update("core-A",active);assertEquals("ACTIVE",c.update("core-A",emptyList()).single().state)
        val inactive=parseEcuFaults(page(130,"PID",37,1,"INACTIVE","0"))
        assertEquals("INACTIVE",c.update("core-A",inactive).single().state)
        assertEquals(1,c.snapshot().size);assertTrue(c.update("core-B",emptyList()).isEmpty())
    }
}
