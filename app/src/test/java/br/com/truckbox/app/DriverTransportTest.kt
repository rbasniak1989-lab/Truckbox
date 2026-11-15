package br.com.truckbox.app

import br.com.truckbox.app.cloud.*
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class DriverTransportTest {
    @Test fun redirectIsNotFollowedWithDeviceCredential() {
        val fake = FakeCloud { _, _ -> 302 to "{\"ok\":true}" }
        val e = assertThrows(DriverCloudException::class.java) { fake.transport().post(DriverEndpoint.GPS, JSONObject()) }
        assertEquals(302, e.status); assertFalse(fake.connections.single().instanceFollowRedirects)
    }
    @Test fun nonJsonEmptyOrOversizedSuccessIsNotAccepted() {
        listOf("", "not-json", "x".repeat(524289), "{\"ok\":false}").forEach { body ->
            assertThrows(Exception::class.java) { FakeCloud { _, _ -> 200 to body }.transport().post(DriverEndpoint.OPERATIONS, JSONObject()) }
        }
    }
    @Test fun fixedEndpointsUseHttpsAndBoundedTimeouts() {
        val fake = FakeCloud { _, _ -> 200 to "{\"ok\":true}" }
        fake.transport().post(DriverEndpoint.OPERATIONS, JSONObject())
        val c = fake.connections.single()
        assertEquals("https", c.url.protocol); assertEquals("wbzrrjufhqfgoctxtlyi.supabase.co", c.url.host)
        assertEquals("/functions/v1/truckbox-app", c.url.path); assertEquals(5000, c.connectTimeout); assertEquals(8000, c.readTimeout)
    }
}
