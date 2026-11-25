package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import com.google.gson.Gson
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.KeyPairGenerator
import java.security.Signature
import java.util.Base64
import java.time.Instant

class CloudHttpClientTest {
    private val now=Instant.parse("2026-11-14T12:00:00Z")
    private val config=CloudConfig("https://example.invalid","sb_publishable_"+"test".repeat(8))
    private val auth=CloudAuthSession("TEST_ACCESS","TEST_REFRESH","10000000-0000-4000-8000-000000000001","10000000-0000-4000-8000-000000000002",now.plusSeconds(3600))
    private class Reply(url: URL,private val status: Int=200,private val body: (Reply)->String): HttpURLConnection(url) {
        val sent=ByteArrayOutputStream();var closed=false
        override fun connect() {}
        override fun disconnect() {closed=true}
        override fun usingProxy()=false
        override fun getResponseCode()=status
        override fun getInputStream()=ByteArrayInputStream(body(this).toByteArray())
        override fun getErrorStream()=if(status>=400) ByteArrayInputStream(body(this).toByteArray()) else null
        override fun getOutputStream()=sent
    }
    private class Signer: DeviceSigner {
        val keys=KeyPairGenerator.getInstance("RSA").apply {initialize(2048)}.generateKeyPair()
        override val deviceId="10000000-0000-4000-8000-000000000003"
        override val publicKeySpki=Base64.getEncoder().encodeToString(keys.public.encoded)
        override fun sign(bytes: ByteArray)=Signature.getInstance("SHA256withRSA").run {initSign(keys.private);update(bytes);sign()}
    }
    private data class Vector(val url: String,val method: String,val deviceId: String,val time: String,val nonce: String,val body: String,val token: String,val canonical: String)
    @Test fun proofMatchesTheIndependentSharedProtocolVectorAcrossGatewayPaths() {
        val v=Gson().fromJson(File("../backend-proposal/v1/cloud/proof-vector.json").readText(),Vector::class.java)
        listOf(v.url,v.url.replace("/functions/v1/","/"),v.url.replace("/functions/v1/multimedia-next-v1","")).forEach {url->
            assertEquals(v.canonical,String(cloudProof(URL(url),v.method,v.deviceId,v.time,v.nonce,v.body.toByteArray(),v.token)))
        }
    }
    @Test fun snapshotUsesSignedHttpsAndRequiresTheResponseNonce() {
        val signer=Signer();lateinit var reply: Reply
        val client=CloudHttpClient(config,CloudConnectionFactory {url-> Reply(url){r->"""{"contractVersion":1,"requestNonce":"${r.getRequestProperty("X-TruckBox-Nonce")}"}"""}.also {reply=it}},now={now})
        assertEquals(1,client.snapshot(auth,signer).contractVersion)
        assertEquals("https",reply.url.protocol);assertEquals("GET",reply.requestMethod)
        assertEquals("Bearer TEST_ACCESS",reply.getRequestProperty("Authorization"));assertEquals(config.publishableKey,reply.getRequestProperty("apikey"))
        assertFalse(reply.instanceFollowRedirects);assertFalse(reply.useCaches);assertTrue(reply.closed)
        val proof=cloudProof(reply.url,"GET",signer.deviceId,reply.getRequestProperty("X-TruckBox-Time"),reply.getRequestProperty("X-TruckBox-Nonce"),ByteArray(0),auth.accessToken)
        val valid=Signature.getInstance("SHA256withRSA").run {initVerify(signer.keys.public);update(proof);verify(Base64.getDecoder().decode(reply.getRequestProperty("X-TruckBox-Signature")))}
        assertTrue(valid)
    }
    @Test fun replayedNonceAndRedirectsCannotBecomeCloudSnapshots() {
        val signer=Signer()
        val replay=CloudHttpClient(config,CloudConnectionFactory {url->Reply(url){"""{"contractVersion":1,"requestNonce":"old"}"""}},now={now})
        assertThrows(IllegalArgumentException::class.java){replay.snapshot(auth,signer)}
        var opens=0
        val redirect=CloudHttpClient(config,CloudConnectionFactory {url->opens++;Reply(url,302){""}},now={now})
        assertEquals(302,assertThrows(CloudHttpError::class.java){redirect.snapshot(auth,signer)}.status)
        assertEquals(1,opens)
    }
    @Test fun policyAckSignsExactUtf8BodyAndChecksConfirmedVersionAndNonce() {
        val signer=Signer();lateinit var reply: Reply
        val client=CloudHttpClient(config,CloudConnectionFactory {url->Reply(url){r->"""{"contractVersion":1,"version":7,"status":"applied","requestNonce":"${r.getRequestProperty("X-TruckBox-Nonce")}"}"""}.also {reply=it}},now={now})
        client.acknowledge(auth,signer,7)
        assertEquals("POST",reply.requestMethod);assertEquals("{\"version\":7}",reply.sent.toString())
        assertEquals("/functions/v1/multimedia-next-v1/v1/policy/ack",reply.url.path)
    }
    @Test fun authErrorsDoNotRetainTheServerPayloadOrPassword() {
        val client=CloudHttpClient(config,CloudConnectionFactory {url->Reply(url,400){"do not retain response or user secrets"}},now={now})
        val error=assertThrows(CloudHttpError::class.java){client.signIn("test@example.invalid","TEST_ONLY")}
        assertEquals(400,error.status);assertFalse(error.toString().contains("secrets"));assertFalse(error.toString().contains("TEST_ONLY"))
    }
    @Test fun cleartextCloudEndpointIsRejectedBeforeOpeningTheConnection() {
        assertThrows(IllegalArgumentException::class.java){CloudHttpClient(CloudConfig("http://example.invalid",config.publishableKey))}
    }
    @Test fun fieldActivationUsesSignedOpaqueCredentialAndRequiresTheRealServerIdentity() {
        val signer=Signer();val code="tbxft_"+"a".repeat(64);lateinit var reply: Reply
        val client=CloudHttpClient(config,CloudConnectionFactory {url->Reply(url){r->"""{"contractVersion":1,"userId":"${auth.userId}","sessionId":"${auth.sessionId}","expiresAt":"${now.plusSeconds(3600)}","requestNonce":"${r.getRequestProperty("X-TruckBox-Nonce")}"}"""}.also {reply=it}},now={now})
        val session=client.activateTest(code,signer,"test-core")
        assertEquals(code,session.refreshToken);assertEquals(auth.userId,session.userId)
        assertEquals("/functions/v1/multimedia-trip-homolog-v1/v1/activate-field-test",reply.url.path)
        assertTrue(reply.sent.toString().contains("\"coreDeviceUid\":\"test-core\""))
        assertFalse(reply.sent.toString().contains(code))
        assertNotNull(reply.getRequestProperty("X-TruckBox-Signature"))
    }

    @Test fun enrolledFieldSessionUsesTripChannelForEverySignedOperation() {
        val signer = Signer()
        val code = "tbxft_" + "b".repeat(64)
        val field = auth.copy(accessToken = code, refreshToken = code)
        val expected = listOf("enroll", "state", "policy/ack", "logout")
        val trip = "10000000-0000-4000-8000-000000000011"
        val requests = mutableListOf<Reply>()
        val client = CloudHttpClient(config, CloudConnectionFactory { url ->
            Reply(url) { r ->
                val nonce = r.getRequestProperty("X-TruckBox-Nonce")
                when (url.path.substringAfter("/multimedia-trip-homolog-v1/v1/")) {
                    "state" -> """{"contractVersion":1,"requestNonce":"$nonce",
                        "route":{"id":"10000000-0000-4000-8000-000000000010","tripId":"$trip",
                            "provider":"HERE","profile":"TRUCK","distanceM":43000,"durationSec":2700,
                            "polyline":"_p~iF~ps|U_ulLnnqC","validUntil":"2026-11-14T16:00:00+00:00",
                            "tolls":[{"name":"Praça de teste","lat":38.5,"lon":-120.2,"lowCents":1000}]},
                        "fuelRange":{"remainingLiters":612.4,"autonomyKm":525.1,"quality":"APPROXIMATE"},
                        "journey":{"reference":"2026-11-14T12:00:00Z"}}"""
                    "policy/ack" -> """{"contractVersion":1,"version":7,"status":"applied","requestNonce":"$nonce"}"""
                    else -> "{}"
                }
            }.also { requests += it }
        }, now = { now })
        client.enroll(field, signer)
        val snapshot = client.snapshot(field, signer)
        val route = requireNotNull(snapshot.route).domain(trip, now)
        assertEquals("HERE", route.provider)
        assertEquals(43000.0, route.distanceM, 0.0)
        assertEquals(2700L, route.durationSeconds)
        assertEquals(2, route.points.size)
        assertEquals("Praça de teste", route.tolls.single().name)
        assertNotNull(route.tolls.single().position)
        assertEquals(612.4, snapshot.fuelRange?.remainingLiters)
        assertNotNull(snapshot.journey)
        client.acknowledge(field, signer, 7)
        client.signOut(field)
        assertEquals(expected.map { "/functions/v1/multimedia-trip-homolog-v1/v1/$it" }, requests.map { it.url.path })

        for (request in requests) {
            val authHeader = request.getRequestProperty("Authorization")
            assertTrue(authHeader == "Bearer $code")
            assertFalse(request.instanceFollowRedirects)
            val target = request.url.path.removePrefix("/functions/v1")
            val canonical = listOf("truckbox-next-v1", signer.deviceId, request.requestMethod, target,
                request.getRequestProperty("X-TruckBox-Time"), request.getRequestProperty("X-TruckBox-Nonce"),
                hash256(request.sent.toByteArray()), hash256(code.toByteArray())).joinToString("\n")
            val signature = Base64.getDecoder().decode(request.getRequestProperty("X-TruckBox-Signature"))
            assertTrue(Signature.getInstance("SHA256withRSA").run {
                initVerify(signer.keys.public)
                update(canonical.toByteArray())
                verify(signature)
            })
        }
    }

    @Test fun legacyFieldAccessRetainsItsOwnAuthenticatedChannel() {
        val code = "tbxft_" + "c".repeat(64)
        val requests = mutableListOf<Reply>()
        val client = CloudHttpClient(config, CloudConnectionFactory { url ->
            val supportsCredential = url.path.contains("/multimedia-next-v1/")
            Reply(url, if (supportsCredential) 200 else 403) { response ->
                val nonce = response.getRequestProperty("X-TruckBox-Nonce")
                if (supportsCredential) """{"contractVersion":1,"userId":"${auth.userId}",
                    "sessionId":"${auth.sessionId}","expiresAt":"${auth.expiresAt}","requestNonce":"$nonce"}"""
                else """{"error":"test_access_denied"}"""
            }.also { requests += it }
        }, now = { now })
        val signer = Signer()
        val session = client.activateTest(code, signer, "test-core")
        assertTrue(session.legacyFieldChannel)
        assertTrue(session.refreshToken == code)
        client.snapshot(session, signer)
        assertEquals(listOf("multimedia-trip-homolog-v1/v1/activate-field-test", "multimedia-next-v1/v1/activate-field-test",
            "multimedia-next-v1/v1/state"), requests.map { it.url.path.removePrefix("/functions/v1/") })
        assertEquals(3, requests.map { it.getRequestProperty("X-TruckBox-Nonce") }.toSet().size)
    }

    @Test fun activationDoesNotRetryProofErrorsOrDeviceKeyConflictsOnAnotherChannel() {
        val signer = Signer()
        val code = "tbxft_" + "d".repeat(64)
        for ((status, error) in listOf(401 to "invalid_device_proof", 403 to "device_key_conflict")) {
            var opened = 0
            val client = CloudHttpClient(config, CloudConnectionFactory { url ->
                opened++
                Reply(url, status) { """{"error":"$error"}""" }
            }, now = { now })
            assertEquals(status, assertThrows(CloudHttpError::class.java) { client.activateTest(code, signer, "test-core") }.status)
            assertEquals(1, opened)
        }
    }
}
