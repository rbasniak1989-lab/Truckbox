package br.com.truckbox.app

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Run on a phone/emulator; does not create trips or fabricate GPS. */
@RunWith(AndroidJUnit4::class)
@OptIn(ExperimentalTestApi::class)
class DriverCloudSmokeTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()
    @Test fun cloudStatusAndPrivateCodeConfigurationAreAccessible() {
        compose.onNodeWithText("Cloud e GPS").assertIsDisplayed()
        compose.onNodeWithText("Config").performClick()
        compose.onNodeWithText("Código privado do Motorista").assertExists()
        compose.onNodeWithText("Core na Starlink").assertDoesNotExist()
    }
}
