package br.com.truckbox.app

import android.Manifest
import android.app.Application
import androidx.test.core.app.ApplicationProvider
import br.com.truckbox.app.gateway.GatewayStatus
import br.com.truckbox.app.gateway.TruckBoxGatewayService
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class DriverPermissionTest {
    @Test fun missingLocationPermissionDoesNotStartGpsOrCrash() {
        val app = ApplicationProvider.getApplicationContext<Application>()
        shadowOf(app).denyPermissions(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
        val controller = Robolectric.buildService(TruckBoxGatewayService::class.java).create()
        assertFalse(GatewayStatus.state.value.running)
        assertTrue(GatewayStatus.state.value.lastError!!.contains("localização"))
        assertEquals(android.app.Service.START_NOT_STICKY, controller.get().onStartCommand(null, 0, 1))
        controller.destroy()
    }
}
