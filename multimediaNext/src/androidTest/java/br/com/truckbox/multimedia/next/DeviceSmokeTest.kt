package br.com.truckbox.multimedia.next

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import br.com.truckbox.multimedia.next.domain.Instrument
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
@OptIn(ExperimentalTestApi::class)
class DeviceSmokeTest {
    @get:Rule val compose=createAndroidComposeRule<MainActivity>()
    @Test fun thirteenInstrumentsAndNavigationOnAndroid() {
        Instrument.defaults.forEach {compose.onNodeWithTag("instrument-${it.name}").assertExists()}
        compose.onNodeWithTag("drawer-handle").performClick()
        compose.onNodeWithTag("nav-TRIP").performClick()
        compose.onNodeWithText("Mapa da viagem").assertExists()
        compose.onNodeWithTag("journey-button").assertDoesNotExist()
        compose.onNodeWithTag("financial-card").assertDoesNotExist()
    }
}
