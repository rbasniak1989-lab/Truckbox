package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.time.Instant
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Respostas de teste do contrato novo. Não são chamadas nem dados da Cloud de produção. */
class CloudCoordinatorTest {
    private var clock=Instant.parse("2026-11-14T12:00:00Z")
    private fun id(n: Int)="10000000-0000-4000-8000-${n.toString().padStart(12,'0')}"
    private val scope=CloudScopeDto(id(1),id(2),id(3),id(4),id(5))
    private val auth=CloudAuthSession("TEST_ACCESS","TEST_REFRESH",id(4),id(5),clock.plusSeconds(3600))
    private val signer=object: DeviceSigner {
        override val deviceId=id(3)
        override val publicKeySpki="TEST_PUBLIC_KEY"
        override fun sign(bytes: ByteArray)=byteArrayOf(1)
    }
    private class Tokens: RefreshTokenStore {
        private var epoch=0L;private var saved: StoredRefresh?=null
        var invalidated: CountDownLatch?=null
        @Synchronized override fun generation()=epoch
        @Synchronized override fun read()=saved
        @Synchronized override fun invalidate(): Long {epoch++;saved=null;invalidated?.countDown();return epoch}
        @Synchronized override fun writeIfCurrent(generation: Long,token: String): Boolean {
            if(epoch!=generation)return false
            saved=StoredRefresh(generation,token);return true
        }
    }
    private inner class Api(var frame: CloudSnapshotDto): CloudApi {
        var error: Exception?=null;var ackError: Exception?=null;var beforeSnapshot: (() -> Unit)?=null;var beforeAck: (() -> Unit)?=null
        var enrollments=0;var acks=0
        var activations=0
        override fun activateTest(code: String,signer: DeviceSigner,coreUid: String): CloudAuthSession {assertEquals("test-core",coreUid);activations++;return auth.copy(accessToken=code,refreshToken=code)}
        override fun signIn(email: String,password: String)=auth
        override fun refresh(refreshToken: String)=auth
        override fun enroll(session: CloudAuthSession,signer: DeviceSigner) {enrollments++}
        override fun snapshot(session: CloudAuthSession,signer: DeviceSigner): CloudSnapshotDto {
            beforeSnapshot?.invoke();error?.let {throw it};return frame
        }
        override fun acknowledge(session: CloudAuthSession,signer: DeviceSigner,version: Long) {beforeAck?.invoke();ackError?.let {throw it};acks++}
        override fun signOut(session: CloudAuthSession) {}
    }
    private fun frame(f: Boolean=true,j: Boolean=true,version: Long=1)=CloudSnapshotDto(
        contractVersion=1,scope=scope,vehicleLabel="Veículo de teste",coreDeviceUid="test-core",
        policy=CloudPolicyDto(scope,version,clock.minusSeconds(60).toString(),clock.plusSeconds(300).toString(),f,j),
        trip=CloudTripDto("trip-1","leg-1","LOADED","Origem teste","Destino teste",20.0,clock.minusSeconds(30).toString()),legs=emptyList(),
        financial=if(f)CloudFinancialDto("LOADED",100000,20000,30000) else null,
        journey=if(j)CloudJourneyDto(clock.toString(),100,0,0,0,86300,0) else null,
        receivedAt=clock.toString(),leaseExpiresAt=clock.plusSeconds(20).toString(),roundTripMillis=100)
    private fun coordinator(api: Api,state: MutableStateFlow<AppState>,tokens: Tokens=Tokens())=
        CloudCoordinator(true,api,{signer},tokens,state,{clock})

    @Test fun fourPermissionMatricesAreAppliedIndependentlyAndAckedOnce()=runBlocking {
        for(f in listOf(false,true))for(j in listOf(false,true)) {
            val state=MutableStateFlow(AppState());val api=Api(frame(f,j));val c=coordinator(api,state)
            c.signIn("test@example.invalid","TEST_ONLY")
            assertEquals(f,state.value.financialAllowed);assertEquals(j,state.value.journeyAllowed)
            assertEquals(f,state.value.financial!=null);assertEquals(j,state.value.journey!=null)
            assertEquals(mapOf((false to false) to "Módulos opcionais não liberados pela Cloud",(true to false) to "Financeiro liberado pela Cloud",
                (false to true) to "Jornada liberada pela Cloud",(true to true) to "Financeiro e Jornada liberados pela Cloud")[f to j],state.value.policyLabel)
            assertNotNull(state.value.trip);assertNull(state.value.trip!!.consumption)
            assertEquals(clock.plusMillis(19900),state.value.policyExpiresAt)
            c.refresh();assertEquals(1,api.enrollments);assertEquals(1,api.acks)
        }
    }
    @Test fun offlineClearsProtectedContentButPreservesCoreInstrumentReadings()=runBlocking {
        val core=Reading(38.0,"°C",Origin.CORE,clock,clock,Quality.PROVISIONAL,"J1587/MID:128/PID:105")
        val state=MutableStateFlow(AppState());val api=Api(frame());val c=coordinator(api,state)
        c.signIn("test@example.invalid","TEST_ONLY")
        state.value=state.value.copy(readings=mapOf(Instrument.INTAKE to InstrumentValue(core)),coreDeviceUid="test-core")
        api.error=IOException("TEST offline");c.refresh()
        assertEquals(ConnectionState.OFFLINE,state.value.cloud)
        assertNull(state.value.financial);assertNull(state.value.journey);assertFalse(state.value.financialAllowed);assertFalse(state.value.journeyAllowed)
        assertEquals(core,state.value.readings[Instrument.INTAKE]!!.first)
    }
    @Test fun offlineOperationalCacheKeepsAgeAndClearsOnLogout() = runBlocking {
        val state = MutableStateFlow(AppState())
        val api = Api(frame().copy(fuelRange = FuelRangeDto(capacityLiters = 720.0, remainingLiters = 480.0,
            autonomyKm = 805.0, quality = "APPROXIMATE", updatedAt = clock.minusSeconds(50).toString())))
        val c = coordinator(api, state)
        c.signIn("test@example.invalid", "TEST_ONLY")
        assertEquals(480.0, state.value.fuelRange?.remainingLiters)
        api.error = IOException("TEST offline")
        c.refresh()
        assertEquals(ConnectionState.OFFLINE, state.value.cloud)
        assertEquals(805.0, state.value.redacted(clock.plusSeconds(21)).fuelRange?.autonomyKm)
        assertEquals(clock.minusSeconds(50), state.value.fuelRange?.updatedAt)
        assertNull(state.value.redacted(clock.plusSeconds(301)).fuelRange)
        assertNull(state.value.financial)
        c.signOut()
        assertNull(state.value.fuelRange)
    }
    @Test fun staleLeaseAndHighRoundTripCannotPublishProtectedValues()=runBlocking {
        for(bad in listOf(frame().copy(roundTripMillis=20000),frame().copy(receivedAt=clock.minusSeconds(61).toString()))) {
            val state=MutableStateFlow(AppState());val c=coordinator(Api(bad),state)
            c.signIn("test@example.invalid","TEST_ONLY")
            assertNull(state.value.financial);assertNull(state.value.journey);assertFalse(state.value.financialAllowed)
        }
    }
    @Test fun finalCompositionGuardHidesContentAtTheShortLeaseBoundary()=runBlocking {
        val state=MutableStateFlow(AppState());val c=coordinator(Api(frame()),state)
        c.signIn("test@example.invalid","TEST_ONLY")
        assertNotNull(state.value.redacted(clock.plusSeconds(19)).financial)
        assertNull(state.value.redacted(clock.plusMillis(19900)).financial)
        assertNull(state.value.redacted(clock.plusSeconds(20)).journey)
    }
    @Test fun revokedPolicyClearsValuesBeforeWaitingForTheRemoteAck()=runBlocking {
        val state=MutableStateFlow(AppState());val api=Api(frame());val c=coordinator(api,state)
        c.signIn("test@example.invalid","TEST_ONLY")
        api.frame=frame(false,false,2)
        api.beforeAck={assertNull(state.value.financial);assertNull(state.value.journey);assertFalse(state.value.financialAllowed)}
        c.refresh();assertEquals(2,api.acks);assertEquals(2L,state.value.policyVersion)
    }
    @Test fun anUnconfirmedAckAndAnotherSessionCannotPublishThePayload()=runBlocking {
        for(api in listOf(Api(frame()).apply {ackError=IOException("TEST ACK timeout")},Api(frame().copy(scope=scope.copy(sessionId=id(9)))))) {
            val state=MutableStateFlow(AppState());val c=coordinator(api,state)
            c.signIn("test@example.invalid","TEST_ONLY")
            assertNull(state.value.financial);assertNull(state.value.trip);assertFalse(state.value.journeyAllowed)
        }
    }
    @Test fun aLateResponseCannotRestoreTokensOrValuesAfterSignOut()=runBlocking {
        val state=MutableStateFlow(AppState());val api=Api(frame());val tokens=Tokens();val c=coordinator(api,state,tokens)
        val entered=CountDownLatch(1);val release=CountDownLatch(1)
        api.beforeSnapshot={entered.countDown();check(release.await(5,TimeUnit.SECONDS))}
        val login=async(Dispatchers.Default){c.signIn("test@example.invalid","TEST_ONLY")}
        assertTrue(withContext(Dispatchers.IO){entered.await(5,TimeUnit.SECONDS)})
        val invalidated=CountDownLatch(1);tokens.invalidated=invalidated
        val logout=async(Dispatchers.Default){c.signOut()}
        assertTrue(withContext(Dispatchers.IO){invalidated.await(5,TimeUnit.SECONDS)})
        release.countDown();login.await();logout.await()
        assertNull(tokens.read());assertNull(state.value.financial);assertNull(state.value.trip);assertFalse(c.ui.value.authenticated)
    }
    @Test fun aNewCloudVehicleClearsPreviousCoreSamplesBeforeTheNextSensorPoll()=runBlocking {
        val state=MutableStateFlow(AppState());val api=Api(frame());val c=coordinator(api,state)
        c.signIn("test@example.invalid","TEST_ONLY")
        state.value=state.value.copy(coreDeviceUid="test-core",readings=mapOf(Instrument.INTAKE to InstrumentValue(Reading(38.0,"°C",Origin.CORE))))
        val nextScope=scope.copy(vehicleId=id(9))
        api.frame=frame().copy(scope=nextScope,policy=frame().policy!!.copy(scope=nextScope),coreDeviceUid="another-core")
        c.refresh()
        assertEquals("another-core",state.value.expectedCoreUid);assertTrue(state.value.readings.isEmpty());assertNull(state.value.coreDeviceUid)
    }
    @Test fun disabledCloudDoesNotGenerateKeysOrInterfereWithCore()=runBlocking {
        val state=MutableStateFlow(AppState(core=ConnectionState.SENSORS_RECEIVED))
        val c=CloudCoordinator(false,Api(frame()),{error("Disabled Cloud must not generate a key")},Tokens(),state,{clock})
        c.refresh();c.signIn("test@example.invalid","TEST_ONLY")
        assertEquals(ConnectionState.SENSORS_RECEIVED,state.value.core);assertFalse(c.ui.value.configured)
    }
    @Test fun temporaryAccessActivatesOnTheCoreAndRestoresTheEncryptedStoredCredential()=runBlocking {
        val state=MutableStateFlow(AppState(coreDeviceUid="test-core"));val tokens=Tokens();val api=Api(frame())
        val c=coordinator(api,state,tokens);val code="tbxft_"+"a".repeat(64)
        c.activateTest(code)
        assertEquals(1,api.activations);assertEquals(code,tokens.read()!!.token)
        assertTrue(state.value.financialAllowed);assertTrue(state.value.journeyAllowed)
        state.value=state.value.copy(coreDeviceUid="test-core")
        coordinator(api,state,tokens).refresh()
        assertEquals(2,api.activations);assertTrue(state.value.financialAllowed)
    }
    @Test fun temporaryProofFailureKeepsTheStoredFieldCredential() = runBlocking {
        val state = MutableStateFlow(AppState(coreDeviceUid = "test-core"))
        val tokens = Tokens()
        val api = Api(frame())
        val c = coordinator(api, state, tokens)
        val code = "tbxft_" + "b".repeat(64)
        c.activateTest(code)
        val activeGeneration = tokens.generation()

        api.error = CloudHttpError(401, "invalid_device_proof")
        c.refresh()
        assertEquals(ConnectionState.AUTH_REQUIRED, state.value.cloud)
        assertNull(state.value.trip)
        assertNull(state.value.financial)
        assertEquals(activeGeneration, tokens.generation())
        assertTrue(tokens.read()?.token == code)

        api.error = null
        state.value = state.value.copy(coreDeviceUid = "test-core")
        c.refresh()
        assertEquals(ConnectionState.CLOUD_AUTHENTICATED, state.value.cloud)
        assertEquals(2, api.activations)
        assertTrue(tokens.read()?.token == code)
    }
    @Test fun gpsKeepsItsRealCaptureTimeAndIsClearedOnCloudFailure()=runBlocking {
        val gps=CloudGpsDto(-15.0,-47.0,clock.minusSeconds(3600).toString(),"TEST_GNSS","TEST_PROVIDER",12.5,"RECEIVED_NOT_PHYSICALLY_VERIFIED")
        val state=MutableStateFlow(AppState());val api=Api(frame().copy(gps=gps));val c=coordinator(api,state)
        c.signIn("test@example.invalid","TEST_ONLY")
        assertEquals(clock.minusSeconds(3600),state.value.gps!!.sampledAt)
        assertEquals(-15.0,state.value.gps!!.latitude,0.0)
        api.error=IOException("TEST offline");c.refresh();assertNull(state.value.gps)
        assertThrows(IllegalArgumentException::class.java){gps.copy(latitude=999.0).domain(clock)}
        Unit
    }
}
