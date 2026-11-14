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
        assertEquals("/functions/v1/multimedia-next-v1/v1/activate-field-test",reply.url.path)
        assertTrue(reply.sent.toString().contains("\"coreDeviceUid\":\"test-core\""))
        assertFalse(reply.sent.toString().contains(code))
        assertNotNull(reply.getRequestProperty("X-TruckBox-Signature"))
    }
}
