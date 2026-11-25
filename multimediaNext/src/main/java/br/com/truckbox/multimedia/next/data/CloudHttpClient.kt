package br.com.truckbox.multimedia.next.data

import com.google.gson.Gson
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.time.Instant
import java.util.Base64
import java.util.UUID

interface DeviceSigner {
    val deviceId: String
    val publicKeySpki: String
    fun sign(bytes: ByteArray): ByteArray
}
fun interface CloudConnectionFactory { fun open(url: URL): HttpURLConnection }
class CloudHttpError(val status: Int,val code: String="request_failed"): IOException("Cloud request failed")
data class CloudErrorDto(val error: String?=null)
data class PasswordRequest(val email: String,val password: String)
data class RefreshRequest(@com.google.gson.annotations.SerializedName("refresh_token") val refreshToken: String)
data class EnrollmentRequest(val publicKeySpki: String,val label: String="TruckBox Multimídia")
data class PolicyAckRequest(val version: Long)
data class PolicyAckDto(val contractVersion: Int?=null,val version: Long?=null,val status: String?=null,val requestNonce: String?=null)
data class FieldActivationRequest(val publicKeySpki: String,val coreDeviceUid: String)
data class FieldSessionDto(val contractVersion: Int?=null,val userId: String?=null,val sessionId: String?=null,val expiresAt: String?=null,val requestNonce: String?=null)
fun isFieldTestToken(token: String)=Regex("tbxft_[0-9a-f]{64}").matches(token)

private const val ACCOUNT_CHANNEL = "multimedia-next-v1"
private const val FIELD_CHANNEL = "multimedia-trip-homolog-v1"
private const val TEST_ACCESS_DENIED = "test_access_denied"

private fun cloudChannel(token: String, legacyFieldChannel: Boolean = false) =
    if (isFieldTestToken(token) && !legacyFieldChannel) FIELD_CHANNEL else ACCOUNT_CHANNEL

private fun functionPath(channel: String) = "/functions/v1/$channel/v1/"

private fun cloudPath(session: CloudAuthSession, endpoint: String) =
    functionPath(cloudChannel(session.accessToken, session.legacyFieldChannel)) + endpoint

fun hash256(bytes: ByteArray): String=MessageDigest.getInstance("SHA-256").digest(bytes).joinToString(""){"%02x".format(it)}
fun cloudProof(url: URL,method: String,device: String,time: String,nonce: String,body: ByteArray,token: String): ByteArray {
    val channel=cloudChannel(token)
    val path=when {url.path.startsWith("/functions/v1/")->url.path.removePrefix("/functions/v1")
        url.path.startsWith("/v1/")->"/$channel"+url.path
        else->url.path}
    return listOf("truckbox-next-v1",device.lowercase(),method,path+(url.query?.let {"?$it"} ?: ""),time,nonce.lowercase(),hash256(body),hash256(token.toByteArray(Charsets.UTF_8))).joinToString("\n").toByteArray(Charsets.UTF_8)
}

/** Auth e dados usam HTTPS no servidor compilado; nenhuma credencial segue para redirects ou Core. */
class CloudHttpClient(
    private val config: CloudConfig,
    private val connections: CloudConnectionFactory=CloudConnectionFactory {it.openConnection() as HttpURLConnection},
    private val gson: Gson=Gson(),
    private val now: ()->Instant=Instant::now,
): CloudApi {
    private var testSigner: DeviceSigner?=null
    init {
        if(config.configured) {
            val url=URL(config.url)
            require(url.protocol=="https" && url.userInfo==null && url.port==-1 && url.path.isEmpty() && url.query==null && url.ref==null)
            require(Regex("sb_publishable_[A-Za-z0-9_-]{16,}").matches(config.publishableKey))
        }
    }
    override fun signIn(email: String,password: String)=auth("password",gson.toJson(PasswordRequest(email,password)))
    override fun activateTest(code: String,signer: DeviceSigner,coreUid: String): CloudAuthSession {
        require(isFieldTestToken(code));var nonce: String?=null
        val body = gson.toJson(FieldActivationRequest(signer.publicKeySpki, coreUid)).toByteArray(Charsets.UTF_8)
        var legacy = false
        val reply = try {
            request(functionPath(FIELD_CHANNEL) + "activate-field-test", "POST", body, code, signer) { nonce = it }
        } catch (error: CloudHttpError) {
            if (error.status != 403 || error.code != TEST_ACCESS_DENIED) throw error
            // O canal anterior ainda autentica os acessos de campo sem pareamento de viagem.
            legacy = true
            request(functionPath(ACCOUNT_CHANNEL) + "activate-field-test", "POST", body, code, signer) { nonce = it }
        }
        val dto=gson.fromJson(reply,FieldSessionDto::class.java)
        require(dto.contractVersion==1 && nonce!=null && dto.requestNonce==nonce && dto.userId?.let(UUID_PATTERN::matches)==true && dto.sessionId?.let(UUID_PATTERN::matches)==true)
        val expiry=Instant.parse(requireNotNull(dto.expiresAt));require(expiry.isAfter(now()) && !expiry.isAfter(now().plusSeconds(72*3600)))
        testSigner=signer
        return CloudAuthSession(code,code,dto.userId!!,dto.sessionId!!,expiry,legacyFieldChannel=legacy)
    }
    override fun refresh(refreshToken: String)=auth("refresh_token",gson.toJson(RefreshRequest(refreshToken)))
    private fun auth(grant: String,body: String): CloudAuthSession {
        val response=request("/auth/v1/token?grant_type=$grant","POST",body.toByteArray(Charsets.UTF_8))
        val dto=gson.fromJson(response,AuthTokenDto::class.java)
        val access=requireNotNull(dto.accessToken);val refresh=requireNotNull(dto.refreshToken)
        require(access.length<=16384 && refresh.length in 8..4096)
        val parts=access.split('.');require(parts.size==3)
        val claims=gson.fromJson(String(Base64.getUrlDecoder().decode(parts[1]),Charsets.UTF_8),AuthClaimsDto::class.java)
        require(claims.sub==dto.user?.id && claims.sub?.let(UUID_PATTERN::matches)==true && claims.sessionId?.let(UUID_PATTERN::matches)==true)
        val expires=Instant.ofEpochSecond(requireNotNull(claims.exp));require(expires.isAfter(now()))
        return CloudAuthSession(access,refresh,claims.sub!!,claims.sessionId!!,expires)
    }
    override fun enroll(session: CloudAuthSession,signer: DeviceSigner) {
        if(isFieldTestToken(session.accessToken))testSigner=signer
        request(cloudPath(session,"enroll"),"POST",gson.toJson(EnrollmentRequest(signer.publicKeySpki)).toByteArray(Charsets.UTF_8),session.accessToken,signer)
    }
    override fun snapshot(session: CloudAuthSession,signer: DeviceSigner): CloudSnapshotDto {
        val started=System.nanoTime()
        var nonce: String?=null
        val dto=gson.fromJson(request(cloudPath(session,"state"),"GET",token=session.accessToken,signer=signer,onNonce={nonce=it}),CloudSnapshotDto::class.java)
        require(nonce!=null && dto.requestNonce==nonce)
        return dto.copy(roundTripMillis=((System.nanoTime()-started)/1_000_000).coerceAtLeast(0))
    }
    override fun acknowledge(session: CloudAuthSession,signer: DeviceSigner,version: Long) {
        var nonce: String?=null
        val r=gson.fromJson(request(cloudPath(session,"policy/ack"),"POST",gson.toJson(PolicyAckRequest(version)).toByteArray(Charsets.UTF_8),session.accessToken,signer,onNonce={nonce=it}),PolicyAckDto::class.java)
        require(r.contractVersion==1 && r.version==version && r.status=="applied" && nonce!=null && r.requestNonce==nonce)
    }
    override fun signOut(session: CloudAuthSession) {
        if(isFieldTestToken(session.accessToken)) {
            request(cloudPath(session,"logout"),"POST",token=session.accessToken,signer=requireNotNull(testSigner))
            testSigner=null
        } else request("/auth/v1/logout?scope=local","POST",token=session.accessToken)
    }

    private fun request(path: String,method: String,body: ByteArray=ByteArray(0),token: String?=null,signer: DeviceSigner?=null,onNonce: (String)->Unit = {}): String {
        check(config.configured)
        require(path.startsWith("/auth/v1/") ||
            token != null && signer != null && (path.startsWith(functionPath(ACCOUNT_CHANNEL)) ||
                isFieldTestToken(token) && path.startsWith(functionPath(FIELD_CHANNEL))))
        val url=URL(config.url+path)
        val connection=connections.open(url)
        return try {
            connection.connectTimeout=5000;connection.readTimeout=8000
            connection.requestMethod=method;connection.instanceFollowRedirects=false;connection.useCaches=false
            connection.setRequestProperty("apikey",config.publishableKey)
            connection.setRequestProperty("Accept","application/json")
            connection.setRequestProperty("Cache-Control","no-cache, no-store")
            token?.let {connection.setRequestProperty("Authorization","Bearer $it")}
            if(signer!=null) {
                require(token!=null && UUID_PATTERN.matches(signer.deviceId))
                val time=now().toEpochMilli().toString();val nonce=UUID.randomUUID().toString()
                onNonce(nonce)
                connection.setRequestProperty("X-TruckBox-Device",signer.deviceId)
                connection.setRequestProperty("X-TruckBox-Time",time)
                connection.setRequestProperty("X-TruckBox-Nonce",nonce)
                connection.setRequestProperty("X-TruckBox-Signature",Base64.getEncoder().encodeToString(signer.sign(cloudProof(url,method,signer.deviceId,time,nonce,body,token))))
            }
            if(method=="POST") {
                connection.doOutput=true;connection.setRequestProperty("Content-Type","application/json")
                connection.setFixedLengthStreamingMode(body.size)
                connection.outputStream.use {it.write(body)}
            }
            val status=connection.responseCode
            if (status !in 200..299) {
                if (status == 403 && path == functionPath(FIELD_CHANNEL) + "activate-field-test") {
                    val code = runCatching {
                        connection.errorStream?.let { readResponse(it, 256) }
                            ?.let { gson.fromJson(it, CloudErrorDto::class.java)?.error }
                    }.getOrNull()
                    if (code == TEST_ACCESS_DENIED) throw CloudHttpError(status, TEST_ACCESS_DENIED)
                }
                throw CloudHttpError(status)
            }
            if(status==204) return ""
            readResponse(connection.inputStream, 524_288) ?: throw CloudHttpError(502, "invalid_response")
        } finally {connection.disconnect()}
    }

    private fun readResponse(input: InputStream, maxBytes: Int): String? = input.use {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(minOf(4_096, maxBytes + 1))
        while (out.size() <= maxBytes) {
            val read = it.read(buffer, 0, minOf(buffer.size, maxBytes + 1 - out.size()))
            if (read < 0) break
            out.write(buffer, 0, read)
        }
        if (out.size() in 1..maxBytes) out.toString(Charsets.UTF_8.name()) else null
    }
}
