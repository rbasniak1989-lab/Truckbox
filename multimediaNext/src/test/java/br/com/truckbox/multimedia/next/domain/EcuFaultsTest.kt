package br.com.truckbox.multimedia.next.domain

import com.google.gson.JsonParser
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class EcuFaultsTest {
    private val base=Instant.parse("2026-11-15T14:07:00Z")
    private fun page(state:String, last:Long)=JsonParser.parseString(
        """{"faults":[{"mid":136,"identifier_type":"SID","identifier":2,"fmi":5,
        "page2":false,"state":"${state}","occurrences":2,"parameter_hex":"55AA",
        "first":[867,1000,1794750000000],"last":[867,90000,${last}],
        "quality":"SAE_PARSED_OEM_UNCONFIRMED"}]}"""
    ).asJsonObject

    @Test fun truePid194RowsAreParsedAndNotFabricated() {
        val found=parseFaultEntries(page("ACTIVE",base.minusSeconds(15).toEpochMilli()))
        assertEquals(1,found.size)
        assertEquals("MID 136 • SID 2 • FMI 5",found.single().technicalCode)
        assertEquals(2,found.single().occurrences)
        assertTrue(found.single().pendingActive())
        assertTrue(found.single().confirmedActive(base,base.minusSeconds(1),true))
        assertFalse(found.single().confirmedActive(base,base.minusSeconds(60),true))
        assertFalse(found.single().confirmedActive(base,base.minusSeconds(1),false))
    }

    @Test fun silenceDoesNotTurnActiveIntoInactive() {
        val fault=parseFaultEntries(page("ACTIVE",base.minusSeconds(300).toEpochMilli())).single()
        assertTrue(fault.pendingActive())
        assertFalse(fault.confirmedActive(base,base.minusSeconds(1),true))
        val resolved=parseFaultEntries(page("INACTIVE",base.toEpochMilli())).single()
        assertEquals(EcuFaultState.INACTIVE,resolved.state)
        assertFalse(resolved.pendingActive())
    }

    @Test fun invalidCodesAreIgnored() {
        val data=JsonParser.parseString("""{"faults":[{"mid":40,"identifier_type":"PID","identifier":999,
         "fmi":17,"state":"ACTIVE"},{"mid":128,"identifier_type":"SID","identifier":18,
         "fmi":5,"state":"UNKNOWN"}]}""").asJsonObject
        val list=parseFaultEntries(data)
        assertEquals(1,list.size)
        assertEquals(EcuFaultState.UNKNOWN,list.single().state)
        assertNull(list.single().lastSeen)
    }
}
