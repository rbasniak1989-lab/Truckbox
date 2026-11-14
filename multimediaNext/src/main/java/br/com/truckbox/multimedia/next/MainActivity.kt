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
import kotlinx.coroutines.flow.collectLatest

class MainActivity : ComponentActivity() {
    private val state = MutableStateFlow(initialState())
    private lateinit var preferencesStore: PreferencesStore
    private lateinit var local: MutableState<LocalPreferences>
    private val coreRetry = MutableStateFlow(0L)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterImmersiveMode()
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if(BuildConfig.FLAVOR!="demo") window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        preferencesStore = PreferencesStore(this)
        local = mutableStateOf(preferencesStore.read())
        setContent {
            val current by state.collectAsState()
            TruckBoxApp(current,local.value,onPreferences = {
                val hostChanged = local.value.coreHost != it.coreHost
                local.value=it;preferencesStore.save(it)
                if(hostChanged) coreRetry.value++
            }, onNight = { night ->
                window.attributes = window.attributes.apply { screenBrightness = if(night) .12f else WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE }
            },onCoreRetry = { coreRetry.value++ })
        }
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                if (BuildConfig.FLAVOR == "demo") return@repeatOnLifecycle
                val client = CoreSensorClient(WifiCoreConnectionFactory(this@MainActivity))
                coreRetry.collectLatest {
                    var failures = 0
                    while (isActive) {
                        val host = local.value.coreHost
                        val fetch = withContext(Dispatchers.IO) { client.fetch(host) }
                        ensureActive() // Não aplicar resposta de uma consulta cancelada por troca de host/reteste.
                        if(host != local.value.coreHost) continue
                        val result = fetch.response
                        if(result == null) {
                            failures++
                            state.value = state.value.copy(core=ConnectionState.OFFLINE,coreDiagnostics=fetch.diagnostics,
                                readings=emptyMap(),speed=null,fuelRate=null,
                                financial=null,journey=null,financialAllowed=false,journeyAllowed=false)
                        } else {
                            failures=0
                            state.value = state.value.copy(core=if(fetch.diagnostics.sensorApi) ConnectionState.SENSORS_RECEIVED else ConnectionState.HTTP_RESPONDED,coreDiagnostics=fetch.diagnostics,
                                readings=result.values,speed=result.speed,fuelRate=result.flow,lastCoreResponse=result.receivedAt)
                        }
                        delay(((if(fetch.diagnostics.sensorApi && result!=null) 1000L else 4000L) shl failures.coerceAtMost(3)).coerceAtMost(30000L))
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        enterImmersiveMode()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if(hasFocus) enterImmersiveMode()
    }

    private fun enterImmersiveMode() {
        WindowCompat.setDecorFitsSystemWindows(window,false)
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        WindowInsetsControllerCompat(window,window.decorView).apply {
            systemBarsBehavior=WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }
}
