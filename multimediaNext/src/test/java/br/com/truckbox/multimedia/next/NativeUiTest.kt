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
    @Test fun coreConnectionReportExplainsUnverifiedValuesAndAllowsExplicitRetest() {
        var retries=0
        val report=CoreDiagnostics(now,listOf(CoreAttempt("truckbox.local",CoreIssue.DNS),CoreAttempt("192.168.4.1",httpStatus=200)),2)
        compose.setContent{TruckBoxApp(AppState(core=ConnectionState.HTTP_RESPONDED,coreDiagnostics=report),startingPage=Page.SETTINGS,nowOverride=now,onCoreRetry={retries++})}
        compose.onNodeWithText("Testar conexão agora").performScrollTo().performClick()
        compose.runOnIdle{assertEquals(1,retries)}
        compose.onNodeWithText("Campos numéricos reconhecidos: 2").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText(report.explanation).assertExists()
        compose.onNodeWithText("truckbox.local · Nome do Core não resolvido").assertExists()
        compose.onNodeWithText("192.168.4.1 · JSON recebido · HTTP 200").assertExists()
    }
    @Test fun receivedLegacyValueIsOnlyShownAsUnverifiedInsideSensorDetails() {
        val reading=Reading(4.4,"bar",Origin.CORE,receivedAt=now,quality=Quality.UNVERIFIED)
        compose.setContent{TruckBoxApp(AppState(readings=mapOf(Instrument.OIL_PRESSURE to InstrumentValue(reading))),nowOverride=now)}
        compose.onNodeWithText("Valor recebido, não verificado:",substring=true).assertDoesNotExist()
        compose.onNodeWithTag("instrument-OIL_PRESSURE").performClick()
        compose.onNodeWithText("Valor recebido, não verificado:",substring=true).assertExists()
        assertNull(reading.current(now))
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
    @Test @Config(qualifiers="w1920dp-h1080dp-land-mdpi") fun captureHomeForDesignReviewExplicitDemo13Inches() {
        val demoValues=listOf(4.4,1.8,4.5,87.0,92.0,38.0,76.0,12.0,27.6,24.0,62.0,10.1,74.0)
        fun reading(value: Double,unit: String)=Reading(value,unit,Origin.DEMO)
        val readings=Instrument.defaults.mapIndexed { index,sensor -> sensor to InstrumentValue(reading(demoValues[index],sensor.unit),
            if(sensor==Instrument.AIR) reading(10.4,"bar") else null) }.toMap()
        compose.setContent{TruckBoxApp(AppState(demo=true,core=ConnectionState.DEMO,cloud=ConnectionState.DEMO,readings=readings),nowOverride=now)}
        compose.onNodeWithText("DEMONSTRAÇÃO · valores fictícios · sem conexão com Core ou Cloud").assertExists()
        assertHomeGeometry()
        saveNativeWindow(File("build/screenshots/homolog/home-13pol-1920x1080-TEST-DEMO.png").apply{parentFile?.mkdirs()})
    }

    private fun assertHomeGeometry() {
        val left=compose.onNodeWithTag("home-instruments").fetchSemanticsNode().boundsInRoot
        val right=compose.onNodeWithTag("home-intelligence").fetchSemanticsNode().boundsInRoot
        val footer=compose.onNodeWithTag("home-footer").fetchSemanticsNode().boundsInRoot
        val root=compose.onNodeWithTag("app-root").fetchSemanticsNode().boundsInRoot
        assertEquals(2f,left.width/right.width,.02f)
        assertEquals(left.top,right.top,1f)
        assertTrue(right.left>left.right)
        assertTrue(footer.top>=left.bottom && footer.top>=right.bottom)
        assertTrue(footer.bottom<=root.bottom+1)
        Instrument.defaults.forEach { compose.onNodeWithTag("instrument-${it.name}").assertIsDisplayed() }
    }

    private fun screenshots(w: Int,h: Int) {
        var page by mutableStateOf(Page.HOME)
        var renderKey by mutableIntStateOf(0)
        compose.setContent{key(renderKey){TruckBoxApp(AppState(),startingPage=page,nowOverride=now)}}
        val output=File("build/screenshots/homolog").apply{mkdirs()}
            Page.entries.forEach { p ->
                compose.runOnIdle{page=p;renderKey++}
                compose.waitForIdle()
                val root=saveNativeWindow(File(output,"${p.name.lowercase()}-${w}x${h}.png"))
                if(p==Page.HOME) assertHomeGeometry()
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
