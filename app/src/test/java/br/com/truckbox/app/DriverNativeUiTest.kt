package br.com.truckbox.app

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import androidx.activity.ComponentActivity
import androidx.compose.material3.Surface
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.test.core.app.ApplicationProvider
import br.com.truckbox.app.gateway.GatewaySnapshot
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.preferences.*
import br.com.truckbox.app.ui.theme.TruckBoxTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w412dp-h915dp-port-mdpi")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@OptIn(ExperimentalTestApi::class)
class DriverNativeUiTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()
    @Test fun statusHasNoCoreDependencyOrInventedPosition() {
        compose.setContent { TruckBoxTheme(AccentOption.BLUE) { Surface { StatusPage(GatewaySnapshot()) } } }
        compose.onNodeWithText("Cloud e GPS").assertIsDisplayed()
        compose.onNodeWithText("Aguardando posição").assertExists()
        compose.onNodeWithText("ESP32 / Starlink").assertDoesNotExist()
        compose.onNodeWithText("Sem confirmação da Cloud").assertExists()
        capture("status-celular-412x915.png")
    }
    @Test @Config(qualifiers = "w320dp-h640dp-port-mdpi") fun smallPhoneCanScrollToGpsDetails() {
        compose.setContent { TruckBoxTheme(AccentOption.BLUE) { Surface { StatusPage(GatewaySnapshot(pendingGps = 3)) } } }
        compose.onNodeWithText("3 posições aguardando envio").performScrollTo().assertIsDisplayed()
        capture("status-celular-320x640.png")
    }
    @Test fun configurationAllowsExplicitStartAndPauseWithoutExposingToken() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        context.getSharedPreferences("truckbox", 0).edit().clear().commit()
        val p = prefs(context)
        var starts = 0; var stops = 0
        compose.setContent { TruckBoxTheme(AccentOption.BLUE) {
            Surface { SettingsPage(p, OperationsStore.isolatedForTest(context, p), { starts++ }, { stops++ }) }
        } }
        compose.onNodeWithText("Código privado do Motorista").assertExists()
        compose.onAllNodesWithText(TEST_CREDENTIALS.token).assertCountEquals(0)
        capture("configuracao-celular-412x915.png")
        compose.onNodeWithText("INICIAR GPS / REVISAR PERMISSÕES").performScrollTo().performClick()
        assertEquals(1, starts)
        compose.onNode(isToggleable()).performScrollTo().performClick()
        assertEquals(1, stops); assertFalse(p.gpsTrackingEnabled)
    }
    private fun capture(name: String) {
        compose.waitForIdle()
        lateinit var bitmap: Bitmap
        compose.runOnIdle {
            val view = compose.activity.window.decorView
            bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
            view.draw(Canvas(bitmap))
        }
        val file = File("build/screenshots/motorista/$name").apply { parentFile?.mkdirs() }
        file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }; bitmap.recycle()
    }
}
