package br.com.truckbox.app

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import androidx.activity.ComponentActivity
import androidx.compose.material3.Surface
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.test.core.app.ApplicationProvider
import br.com.truckbox.app.cloud.TripRoutesClient
import br.com.truckbox.app.gateway.GatewaySnapshot
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.operations.TripRecord
import br.com.truckbox.app.preferences.*
import br.com.truckbox.app.ui.theme.TruckBoxTheme
import br.com.truckbox.app.ui.TripRoutePanel
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.util.concurrent.atomic.AtomicInteger

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
    @Test fun existingTripRequiresSelectionAndConfirmationBeforeBinding() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        context.getSharedPreferences("truckbox", 0).edit().clear().commit()
        val trip = TripRecord(clientUid = "existing-trip", origin = "Cidade de origem", destination = "Cidade de destino",
            startedAtMs = TEST_TIME, cloudId = "00000000-0000-4000-8000-000000000111")
        val option = JSONObject().put("id", "00000000-0000-4000-8000-000000000112")
            .put("provider", "HERE").put("origin", trip.origin).put("destination", trip.destination)
            .put("distanceM", 800_000).put("durationSec", 45_000)
        val attachments = AtomicInteger()
        val reads = AtomicInteger()
        val fake = FakeCloud { _, body ->
            val payload = body.getJSONObject("payload")
            assertEquals(trip.clientUid, payload.getString("tripClientUid"))
            when (body.getString("action")) {
                "plans" -> {
                    val linked = JSONObject(option.toString()).put("tripId", trip.cloudId).put("tollCoverage", "UNAVAILABLE")
                        .put("polyline", "_p~iF~ps|U_ulLnnqC").put("createdAt", "2026-11-20T10:00:00Z").put("tolls", JSONArray())
                    reads.incrementAndGet()
                    200 to JSONObject().put("plans", JSONArray().put(option))
                        .put("route", if (attachments.get() == 0) JSONObject.NULL else linked).toString()
                }
                "attach" -> {
                    assertEquals(option.getString("id"), payload.getString("id"))
                    attachments.incrementAndGet()
                    200 to "{\"ok\":true}"
                }
                else -> error("Unexpected operation on the active trip")
            }
        }
        val client = TripRoutesClient(prefs(context), fake.transport())
        compose.setContent {
            TruckBoxTheme(AccentOption.BLUE) {
                Surface {
                    Column(Modifier.verticalScroll(rememberScrollState())) { TripRoutePanel(trip, client, ending = false) }
                }
            }
        }
        compose.waitUntilAtLeastOneExists(hasText("Sem rota vinculada"))
        compose.onNodeWithText("VINCULAR À VIAGEM ATUAL").assertIsNotEnabled()
        compose.onNodeWithText("SELECIONAR ROTA").performClick()
        compose.onNodeWithText("Rota completa").performClick()
        compose.onNodeWithText("VINCULAR À VIAGEM ATUAL").performClick()
        assertEquals(0, attachments.get())
        compose.onNodeWithText("VINCULAR").performClick()
        compose.waitUntilAtLeastOneExists(hasText("VISUALIZAR ROTA"))
        assertEquals(1, attachments.get())
        assertTrue(reads.get() >= 2)
        compose.onNodeWithText("VISUALIZAR ROTA").performClick()
        compose.onNodeWithContentDescription("Traçado da rota selecionada").assertExists()
        capture("rota-vinculada-412x915.png")
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
