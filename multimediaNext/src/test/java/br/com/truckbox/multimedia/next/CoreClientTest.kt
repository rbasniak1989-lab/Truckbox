package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownHostException
import java.time.Instant

class CoreClientTest {
    private val now = Instant.parse("2026-11-14T12:00:00Z")
    private class Reply(url: URL, private val code: Int = 200, body: String = "{}") : HttpURLConnection(url) {
        private val bytes = body.toByteArray()
        var closed = false
        override fun connect() {}
        override fun disconnect() { closed = true }
        override fun usingProxy() = false
        override fun getResponseCode() = code
        override fun getInputStream() = ByteArrayInputStream(bytes)
    }
    private fun client(factory: CoreConnectionFactory) = CoreClient(factory,now={now})

    @Test fun dnsFailureFallsBackToIpWithReadOnlyUncachedGetAndNoLivePromotion() {
        val urls = mutableListOf<URL>()
        lateinit var reply: Reply
        val result = client { url ->
            urls += url
            if(url.host == "truckbox.local") throw UnknownHostException()
            Reply(url,body="""{"oilPressureBar":4.4,"speedKph":0} """).also { reply=it }
        }.fetch("truckbox.local")
        assertEquals(listOf("truckbox.local","192.168.4.1"),urls.map { it.host })
        assertTrue(urls.all { it.path=="/api/state" && it.query.startsWith("nocache=") })
        assertEquals("GET",reply.requestMethod)
        assertFalse(reply.instanceFollowRedirects)
        assertFalse(reply.useCaches)
        assertNull(reply.getRequestProperty("Authorization"))
        assertTrue(reply.closed)
        assertEquals(CoreIssue.DNS,result.diagnostics.attempts.first().issue)
        assertTrue(result.diagnostics.responded)
        assertEquals(2,result.diagnostics.recognizedValues)
        assertNotNull(result.response)
        assertNull(result.response!!.values[Instrument.OIL_PRESSURE]!!.first.current(now))
        assertTrue(result.diagnostics.explanation.contains("não comprova"))
    }

    @Test fun selectedIpIsTriedFirstAndDoesNotProbeAnotherHostAfterSuccess() {
        val urls = mutableListOf<URL>()
        val result=client { url -> urls+=url;Reply(url) }.fetch("192.168.4.1")
        assertEquals(listOf("192.168.4.1"),urls.map { it.host })
        assertTrue(result.diagnostics.responded)
        assertEquals(0,result.diagnostics.recognizedValues)
        assertTrue(result.diagnostics.explanation.contains("sem campos"))
    }

    @Test fun noWifiNeverFallsBackToDefaultNetwork() {
        var opens=0
        val result=client { opens++;throw CoreWifiUnavailable() }.fetch("truckbox.local")
        assertEquals(1,opens)
        assertNull(result.response)
        assertEquals(CoreIssue.NO_WIFI,result.diagnostics.attempts.single().issue)
        assertFalse(result.diagnostics.responded)
    }

    @Test fun timeoutAndHttpFailureKeepDistinctCausesWithoutPayload() {
        val result=client { url ->
            if(url.host=="truckbox.local") throw SocketTimeoutException("not exported")
            Reply(url,503,"secret server response is never retained")
        }.fetch("truckbox.local")
        assertNull(result.response)
        assertEquals(listOf(CoreIssue.TIMEOUT,CoreIssue.HTTP),result.diagnostics.attempts.map {it.issue})
        assertEquals(503,result.diagnostics.attempts.last().httpStatus)
        assertFalse(result.diagnostics.toString().contains("secret"))
    }

    @Test fun htmlResponseIsRejectedAndFallbackParsesOnlyExactRootFields() {
        val result=client { url ->
            if(url.host=="truckbox.local") Reply(url,body="<html>login</html>")
            else Reply(url,body="""{"coolantTempC":75,"history":[{"boostBar":99}]}""")
        }.fetch("truckbox.local")
        assertEquals(CoreIssue.FORMAT,result.diagnostics.attempts.first().issue)
        assertEquals(1,result.diagnostics.recognizedValues)
        assertNull(result.response!!.values[Instrument.BOOST]!!.first.value)
    }

    @Test fun redirectsAreNotFollowedAndBothResponsesAreClosed() {
        val replies=mutableListOf<Reply>()
        val result=client { url -> Reply(url,302).also {replies+=it} }.fetch("truckbox.local")
        assertNull(result.response)
        assertEquals(2,replies.size)
        assertTrue(replies.all {it.closed && !it.instanceFollowRedirects})
        assertTrue(result.diagnostics.attempts.all {it.issue==CoreIssue.HTTP && it.httpStatus==302})
    }

    @Test fun oversizedAndNullBodiesFailWithoutBecomingHttpTelemetrySuccess() {
        val result=client { url -> Reply(url,body=if(url.host=="truckbox.local") " ".repeat(1_048_577) else "null") }.fetch("truckbox.local")
        assertNull(result.response)
        assertFalse(result.diagnostics.responded)
        assertTrue(result.diagnostics.attempts.all {it.issue==CoreIssue.FORMAT})
    }

    @Test fun unapprovedHostIsRejectedBeforeAnyNetworkAccess() {
        var opened=false
        try {
            client { url -> opened=true;Reply(url) }.fetch("example.com")
            fail("Host externo aceito")
        } catch (_: IllegalArgumentException) { }
        assertFalse(opened)
    }
}
