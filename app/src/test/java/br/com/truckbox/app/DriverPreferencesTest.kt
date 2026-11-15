package br.com.truckbox.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class DriverPreferencesTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val tokens = MemoryTokens()
    private lateinit var prefs: TruckBoxPreferences
    @Before fun setup() {
        context.getSharedPreferences("truckbox", 0).edit().clear().commit()
        prefs = TruckBoxPreferences(context, tokens)
    }
    @Test fun newInstallHasNoCredentialsOrInventedVehicle() {
        assertFalse(prefs.cloudCredentials().configured); assertFalse(prefs.gpsCredentials().configured)
        assertNull(prefs.cloudCredentials().vehicleId)
    }
    @Test fun codeConfiguresBothCloudClientsAndKeepsTokenOutsidePlainPreferences() {
        prefs.importCloudCode(code())
        assertEquals(TEST_CREDENTIALS, prefs.cloudCredentials()); assertEquals(TEST_CREDENTIALS, prefs.gpsCredentials())
        assertFalse(context.getSharedPreferences("truckbox", 0).all.values.any { it.toString().contains(TEST_CREDENTIALS.token) })
    }
    @Test fun malformedOrTabletCodeCannotReplaceExistingConfiguration() {
        prefs.importCloudCode(code())
        listOf("tablet-code", "tbxd1_invalid!", code(vehicle = "bad"), code(token = "short")).forEach {
            assertThrows(Exception::class.java) { prefs.importCloudCode(it) }
            assertEquals(TEST_CREDENTIALS, prefs.cloudCredentials())
        }
    }
    @Test fun storageFailureKeepsPreviousProfile() {
        prefs.importCloudCode(code()); tokens.failWrites = true
        assertThrows(Exception::class.java) { prefs.importCloudCode(code(uid = "ANOTHER-DRIVER")) }
        assertEquals(TEST_CREDENTIALS, prefs.cloudCredentials())
    }
    @Test fun anotherVehicleCannotMixWithExistingLocalProfile() {
        prefs.importCloudCode(code())
        assertThrows(Exception::class.java) { prefs.importCloudCode(code(vehicle = "00000000-0000-4000-8000-000000000002")) }
        assertEquals(TEST_CREDENTIALS, prefs.cloudCredentials())
    }
    @Test fun legacyTokensMoveToProtectedStoreWithoutLosingOtherSettings() {
        context.getSharedPreferences("truckbox", 0).edit().putString("gps_device_token", "legacy-value")
            .putString("core_host", "192.168.4.1").commit()
        assertEquals("legacy-value", prefs.gpsDeviceToken)
        assertEquals("legacy-value", tokens.entries["gps_device_token"])
        assertFalse(context.getSharedPreferences("truckbox", 0).contains("gps_device_token"))
        assertEquals("192.168.4.1", context.getSharedPreferences("truckbox", 0).getString("core_host", null))
    }
    @Test fun installationIdentityAndGpsPausePersist() {
        val id = prefs.installId; prefs.gpsTrackingEnabled = false
        val next = TruckBoxPreferences(context, tokens)
        assertEquals(id, next.installId); assertFalse(next.gpsTrackingEnabled)
    }
}
