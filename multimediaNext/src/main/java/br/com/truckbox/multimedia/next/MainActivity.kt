package br.com.truckbox.multimedia.next

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import br.com.truckbox.multimedia.next.ui.TruckBoxApp
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow

class MainActivity : ComponentActivity() {
    private val state = MutableStateFlow(initialState())
    private lateinit var preferencesStore: PreferencesStore
    private lateinit var local: MutableState<LocalPreferences>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window,false)
        WindowInsetsControllerCompat(window,window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if(BuildConfig.FLAVOR!="demo") window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        preferencesStore = PreferencesStore(this)
        local = mutableStateOf(preferencesStore.read())
        setContent {
            val current by state.collectAsState()
            TruckBoxApp(current,local.value,onPreferences = {
                local.value=it;preferencesStore.save(it)
            }, onNight = { night ->
                window.attributes = window.attributes.apply { screenBrightness = if(night) .12f else WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE }
            })
        }
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                if (BuildConfig.FLAVOR == "demo") return@repeatOnLifecycle
                val client = CoreClient()
                var failures = 0
                while (isActive) {
                    val host = local.value.coreHost
                    val result = withContext(Dispatchers.IO) {
                        try { client.fetch(host) } catch (_: java.io.IOException) { null } catch (_: com.google.gson.JsonParseException) { null }
                    }
                    if(host != local.value.coreHost) continue
                    if(result == null) {
                        failures++
                        state.value = state.value.copy(core=ConnectionState.OFFLINE,financial=null,journey=null,financialAllowed=false,journeyAllowed=false)
                    } else {
                        failures=0
                        state.value = state.value.copy(core=ConnectionState.HTTP_RESPONDED,readings=result.values,speed=result.speed,fuelRate=result.flow,lastCoreResponse=result.receivedAt)
                    }
                    delay((4000L shl failures.coerceAtMost(3)).coerceAtMost(30000L))
                }
            }
        }
    }
}
