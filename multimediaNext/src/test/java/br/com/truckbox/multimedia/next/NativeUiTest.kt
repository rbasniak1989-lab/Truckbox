package br.com.truckbox.multimedia.next

import androidx.compose.material3.Text
import androidx.compose.runtime.*
import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.View
import android.view.inspector.WindowInspector
import androidx.activity.ComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import br.com.truckbox.multimedia.next.domain.*
import br.com.truckbox.multimedia.next.ui.TruckBoxApp
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.time.Instant

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[35],qualifiers="w1280dp-h800dp-land-mdpi")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@OptIn(ExperimentalTestApi::class)
@androidx.test.filters.SdkSuppress(minSdkVersion=29)
class NativeUiTest {
    @get:Rule val compose=createAndroidComposeRule<ComponentActivity>()
    private val now=Instant.parse("2026-11-13T12:00:00Z")

    @Test fun collapsedDrawerShowsThirteenCardsAndClosesAfterSelection() {
        compose.setContent{TruckBoxApp(AppState(),nowOverride=now)}
        Instrument.defaults.forEach { compose.onNodeWithTag("instrument-${it.name}").assertExists() }
        compose.onNodeWithTag("drawer-handle").performClick()
        compose.onNodeWithTag("nav-CONSUMPTION").performClick()
        compose.onNodeWithText("Média da viagem / etapa").assertExists()
        compose.onNodeWithTag("nav-HOME").assertIsNotDisplayed()
    }
    @Test fun allFourPermissionMatricesAffectNativeCompositionIndependently() {
        var state by mutableStateOf(AppState())
        compose.setContent{TruckBoxApp(state,startingPage=Page.TRIP,nowOverride=now)}
        listOf(false,true).forEach { f->listOf(false,true).forEach { j->
            compose.runOnIdle{state=AppState(financialAllowed=f,journeyAllowed=j)}
            if(f) compose.onNodeWithTag("financial-card").assertExists() else compose.onNodeWithTag("financial-card").assertDoesNotExist()
            if(j) compose.onNodeWithTag("journey-button").assertExists() else compose.onNodeWithTag("journey-button").assertDoesNotExist()
        } }
    }
    @Test fun revocationClosesJourneyModalImmediatelyAndPreservesTripPage() {
        var state by mutableStateOf(AppState(journeyAllowed=true))
        compose.setContent{TruckBoxApp(state,startingPage=Page.TRIP,nowOverride=now)}
        compose.onNodeWithTag("journey-button").performClick()
        compose.onNodeWithTag("journey-dialog").assertExists()
        compose.runOnIdle{state=state.copy(journeyAllowed=false)}
        compose.onNodeWithTag("journey-dialog").assertDoesNotExist()
        compose.onNodeWithTag("journey-button").assertDoesNotExist()
        compose.onNodeWithText("Mapa da viagem").assertExists()
    }
    @Test fun faultCentralIsOnlyInGeneralAndEngineExpands() {
        compose.setContent{TruckBoxApp(AppState(),startingPage=Page.TRUCK,nowOverride=now)}
        compose.onNodeWithTag("fault-central").assertExists()
        compose.onAllNodesWithText("Motor",useUnmergedTree=true).onFirst().performClick()
        compose.onNodeWithTag("fault-central").assertDoesNotExist()
        compose.onNodeWithTag("intelligence-first").assertExists()
        compose.onNodeWithTag("expand-sensors").performScrollTo().performClick()
        compose.onNodeWithText("Sinais adicionais").assertExists()
    }
    @Test fun captureOptionalModulesWithExplicitTestDemonstrationBanner() {
        val sample=AppState(demo=true,journeyAllowed=true,financialAllowed=true,
            policyLabel="DEMONSTRAÇÃO · política de teste",
            journey=JourneyView(now,journey24h(emptyList(),now)),financial=FinancialView(100000,20000,40000))
        compose.setContent{TruckBoxApp(sample,startingPage=Page.TRIP,nowOverride=now)}
        compose.onNodeWithText("DEMONSTRAÇÃO · valores fictícios · sem conexão com Core ou Cloud").assertExists()
        val output=File("build/screenshots/homolog").apply{mkdirs()}
        compose.onNodeWithTag("financial-card").performScrollTo()
        saveNativeWindow(File(output,"optional-financial-TEST-DEMO.png"))
        compose.onNodeWithTag("journey-button").performScrollTo().performClick()
        compose.onNodeWithTag("journey-dialog").assertExists()
        saveNativeWindow(File(output,"optional-journey-TEST-DEMO.png"),WindowInspector.getGlobalWindowViews().last())
    }
    @Test @Config(qualifiers="w1024dp-h460dp-land-mdpi") fun screenshots1024x460() = screenshots(1024,460)
    @Test @Config(qualifiers="w1024dp-h600dp-land-mdpi") fun screenshots1024x600() = screenshots(1024,600)
    @Test @Config(qualifiers="w1280dp-h800dp-land-mdpi") fun screenshots1280x800() = screenshots(1280,800)
    @Test @Config(qualifiers="w1672dp-h941dp-land-mdpi") fun screenshots1672x941() = screenshots(1672,941)
    @Test @Config(qualifiers="w1920dp-h1080dp-land-mdpi") fun screenshots1920x1080() = screenshots(1920,1080)

    private fun screenshots(w: Int,h: Int) {
        var page by mutableStateOf(Page.HOME)
        var renderKey by mutableIntStateOf(0)
        compose.setContent{key(renderKey){TruckBoxApp(AppState(),startingPage=page,nowOverride=now)}}
        val output=File("build/screenshots/homolog").apply{mkdirs()}
            Page.entries.forEach { p ->
                compose.runOnIdle{page=p;renderKey++}
                compose.waitForIdle()
                val root=saveNativeWindow(File(output,"${p.name.lowercase()}-${w}x${h}.png"))
                assertEquals(w,root.first)
                // Activity nativa pode reservar área de sistema; registrar a dimensão capturada.
                assertTrue("Altura do viewport: ${root.second}",root.second in (h-80)..h)
                Instrument.defaults.takeIf {p==Page.HOME}?.forEach {compose.onNodeWithTag("instrument-${it.name}").assertExists()}
            }
    }
    /** O PixelCopy de captureToImage depende de frame da janela física.
     * Renderizar a View Android em Canvas nativo funciona sem emulador, sem recriar a UI em HTML. */
    private fun saveNativeWindow(file: File,view: View=compose.activity.window.decorView): Pair<Int,Int> {
        compose.waitForIdle()
        lateinit var bitmap: Bitmap
        compose.runOnIdle {
            bitmap=Bitmap.createBitmap(view.width,view.height,Bitmap.Config.ARGB_8888)
            view.draw(Canvas(bitmap))
        }
        file.outputStream().use{bitmap.compress(Bitmap.CompressFormat.PNG,100,it)}
        val result=bitmap.width to bitmap.height
        bitmap.recycle()
        return result
    }
}
