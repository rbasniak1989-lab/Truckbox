package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.domain.*
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class PolicyTest {
    private val now=Instant.parse("2026-11-13T12:00:00Z")
    private val scope=IdentityScope("tenant","vehicle","device","user","session")
    private val views=ProtectedViews(FinancialView(10000,1000,2000),JourneyView(now,journey24h(emptyList(),now)))
    private fun policy(f: Boolean,j: Boolean,v: Long=1)=CloudPolicy(scope,v,now.minusSeconds(1),now.plusSeconds(60),f,j)
    @Test fun fourIndependentMatricesAreEnforcedBeforeStateExposure() {
        listOf(false,true).forEach { f -> listOf(false,true).forEach { j ->
            val session=PolicySession(scope)
            assertTrue(session.applyFromCloud(policy(f,j),now))
            session.acceptResponse(scope,1,views,now)
            assertEquals(f,session.snapshot(now).financial!=null)
            assertEquals(j,session.snapshot(now).journey!=null)
        } }
    }
    @Test fun unknownOfflineExpiredAndClockBackDenyAndEraseViews() {
        val session=PolicySession(scope)
        assertEquals(false to false,session.allowed(now))
        session.applyFromCloud(policy(true,true),now);session.acceptResponse(scope,1,views,now)
        session.disconnect();assertEquals(ProtectedViews(),session.snapshot(now))
        session.applyFromCloud(policy(true,true),now);session.acceptResponse(scope,1,views,now)
        assertEquals(ProtectedViews(),session.snapshot(now.plusSeconds(60)))
        assertEquals(false to false,session.allowed(now.minusSeconds(10)))
    }
    @Test fun revocationDiscardsCacheAndRejectsLateResponseAndPolicyReplay() {
        val session=PolicySession(scope)
        session.applyFromCloud(policy(true,true),now);session.acceptResponse(scope,1,views,now)
        session.applyFromCloud(policy(false,false,2),now)
        session.acceptResponse(scope,1,views,now)
        assertEquals(ProtectedViews(),session.snapshot(now))
        assertFalse(session.applyFromCloud(policy(true,true),now))
        assertFalse(session.applyFromCloud(policy(true,true,2),now))
    }
    @Test fun scopeIncludesTenantVehicleDeviceUserAndSession() {
        val variations=listOf(scope.copy(tenant="other"),scope.copy(vehicle="other"),scope.copy(device="other"),scope.copy(user="other"),scope.copy(session="other"))
        variations.forEach { other ->
            val session=PolicySession(scope);session.applyFromCloud(policy(true,true),now);session.acceptResponse(scope,1,views,now)
            session.changeIdentity(other)
            assertFalse(session.applyFromCloud(policy(true,true),now))
            session.acceptResponse(scope,1,views,now)
            assertEquals(ProtectedViews(),session.snapshot(now))
        }
    }
}
