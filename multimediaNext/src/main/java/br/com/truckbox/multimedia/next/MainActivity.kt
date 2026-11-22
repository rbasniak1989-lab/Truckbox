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
import br.com.truckbox.multimedia.next.gps.GpsSettings
import br.com.truckbox.multimedia.next.gps.GpsSharingService
import br.com.truckbox.multimedia.next.gps.hasPreciseLocation
import br.com.truckbox.multimedia.next.ui.TruckBoxApp
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update

class MainActivity : ComponentActivity() {
    private val state = MutableStateFlow(initialState())
    private lateinit var preferencesStore: PreferencesStore
    private lateinit var local: MutableState<LocalPreferences>
    private val coreRetry = MutableStateFlow(0L)
    private var cloud: CloudCoordinator?=null
    private val cloudFallback=MutableStateFlow(CloudUiState())
    private var cloudAction: Job?=null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterImmersiveMode()
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if(BuildConfig.FLAVOR!="demo") window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        preferencesStore = PreferencesStore(this)
        local = mutableStateOf(preferencesStore.read())
        if(BuildConfig.FLAVOR!="demo") {
            val config=CloudConfig(BuildConfig.CLOUD_URL,BuildConfig.CLOUD_PUBLISHABLE_KEY)
            if(config.configured) try {
                cloud=CloudCoordinator(true,CloudHttpClient(config),{AndroidDeviceSigner()},
                    EncryptedRefreshTokenStore(this,config.url+"|"+config.publishableKey),state)
            } catch(_: Exception) {
                cloudFallback.value=CloudUiState(message="Armazenamento seguro indisponível neste aparelho")
            }
        }
        setContent {
            val current by state.collectAsState()
            val currentCloud by (cloud?.ui ?: cloudFallback).collectAsState()
            TruckBoxApp(current,local.value,onPreferences = {
                val hostChanged = local.value.coreHost != it.coreHost
                local.value=it;preferencesStore.save(it)
                if(hostChanged) {
                    state.update {it.copy(readings=emptyMap(),speed=null,fuelRate=null,localFuel=null,coreDeviceUid=null,coreInternetSharing=null,core=ConnectionState.UNVERIFIED,
                        coreFaults=emptyList(),faultUid=null,faultSnapshotAt=null,faultReadable=false,faultIncomplete=true,faultOverflow=0)}
                    coreRetry.value++
                }
            }, onNight = { night ->
                window.attributes = window.attributes.apply { screenBrightness = if(night) .12f else WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE }
            },onCoreRetry = { coreRetry.value++ },cloudUi=currentCloud,
                onCloudSignIn={email,password->launchCloudAction {it.signIn(email,password)}},
                onCloudSignOut={launchCloudAction {it.signOut()}},onCloudRefresh={launchCloudAction {it.refresh()}},onCloudTestActivate={code->launchCloudAction {it.activateTest(code)}})
        }
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                if (BuildConfig.FLAVOR == "demo") return@repeatOnLifecycle
              launch {
                val localConnection=WifiCoreConnectionFactory(this@MainActivity)
                val client = CoreSensorClient(localConnection)
                val liveClient = CoreLiveClient(localConnection)
                coreRetry.collectLatest {
                    var failures = 0
                    while (isActive) {
                        val host = local.value.coreHost
                        val expectedCore=state.value.expectedCoreUid
                        val fetch = withContext(Dispatchers.IO) { client.fetch(host,expectedCore) }
                        val live = fetch.response?.physicalDeviceUid?.let {uid ->
                            withContext(Dispatchers.IO) {liveClient.fetch(host,uid)}
                        }.orEmpty()
                        ensureActive() // Não aplicar resposta de uma consulta cancelada por troca de host/reteste.
                        if(host != local.value.coreHost || expectedCore!=state.value.expectedCoreUid) continue
                        val result = fetch.response
                        if(result == null) {
                            failures++
                            state.update {if(it.expectedCoreUid!=expectedCore)it else it.copy(core=ConnectionState.OFFLINE,coreDiagnostics=fetch.diagnostics,readings=staleCoreReadings(it.readings),speed=null,fuelRate=null,
                                faultReadable=false)}
                        } else {
                            failures=0
                            val values=result.values.toMutableMap()
                            for((sensor,entry) in live) {
                                if(values[sensor]?.first?.observed(java.time.Instant.now())==null)values[sensor]=entry
                            }
                            state.update {if(it.expectedCoreUid!=expectedCore)it else it.copy(core=if(fetch.diagnostics.sensorApi) ConnectionState.SENSORS_RECEIVED else ConnectionState.HTTP_RESPONDED,coreDiagnostics=fetch.diagnostics,
                                readings=retainCoreReadings(it.readings.takeIf {_ -> result.physicalDeviceUid!=null && result.physicalDeviceUid==it.coreDeviceUid} ?: emptyMap(),values),
                                coreDeviceUid=result.physicalDeviceUid,coreInternetSharing=result.internetSharing,speed=result.speed,fuelRate=result.flow,lastCoreResponse=result.receivedAt,
                                coreFaults=it.coreFaults.takeIf {_ -> result.physicalDeviceUid==it.faultUid}.orEmpty(),
                                faultUid=it.faultUid.takeIf {_ -> result.physicalDeviceUid==it.faultUid},
                                faultReadable=it.faultReadable && result.physicalDeviceUid==it.faultUid)}
                        }
                        delay(((if(fetch.diagnostics.sensorApi && result!=null) 1000L else 4000L) shl failures.coerceAtMost(3)).coerceAtMost(30000L))
                    }
                }
              }
              launch {
                val faults=CoreFaultClient(WifiCoreConnectionFactory(this@MainActivity))
                coreRetry.collectLatest {
                    while(isActive) {
                        val host=local.value.coreHost
                        val uid=state.value.coreDeviceUid
                        if(uid!=null) {
                            val snapshot=withContext(Dispatchers.IO) {faults.fetch(host,uid)}
                            ensureActive()
                            if(host==local.value.coreHost && uid==state.value.coreDeviceUid) {
                                state.update {previous->
                                    if(snapshot==null)previous.copy(faultReadable=false)
                                    else {
                                        val known=previous.coreFaults.takeIf {previous.faultUid==uid}.orEmpty()
                                            .associateBy {it.key}.toMutableMap()
                                        // Reenvio atualiza a falha, ausência não a torna inativa.
                                        for(fault in snapshot.faults)known[fault.key]=fault
                                        previous.copy(coreFaults=known.values.toList().takeLast(32),
                                            faultUid=uid,faultSnapshotAt=snapshot.checkedAt,
                                            faultReadable=true,faultIncomplete=snapshot.incomplete,
                                            faultOverflow=snapshot.overflow)
                                    }
                                }
                            }
                        }
                        delay(15000)
                    }
                }
              }
              launch {
                val fuel=CoreFuelClient(WifiCoreConnectionFactory(this@MainActivity))
                coreRetry.collectLatest {
                    while(isActive) {
                        val host=local.value.coreHost
                        val uid=state.value.coreDeviceUid
                        if(uid!=null) {
                            val window=withContext(Dispatchers.IO) {fuel.fetch(host,uid)}
                            ensureActive()
                            if(host==local.value.coreHost && uid==state.value.coreDeviceUid)
                                state.update {it.copy(localFuel=window)}
                        }
                        delay(10000)
                    }
                }
              }
              launch {
                while(isActive) {cloudSafely {it.refresh()};delay(10000)}
              }
            }
        }
    }

    private fun launchCloudAction(action: suspend (CloudCoordinator)->Unit) {
        val coordinator=cloud ?: return
        cloudAction?.cancel()
        cloudAction=lifecycleScope.launch {cloudSafely(action)}
    }
    private suspend fun cloudSafely(action: suspend (CloudCoordinator)->Unit) {
        try {cloud?.let {action(it)}}catch(e: CancellationException){throw e}catch(_: Exception){cloud?.reportUnavailable()}
    }

    override fun onStop() {
        super.onStop()
        cloudAction?.cancel()
        cloud?.disconnectViews()
    }

    override fun onResume() {
        super.onResume()
        enterImmersiveMode()
        if (BuildConfig.FLAVOR == "homolog" && GpsSettings(this).enabled && hasPreciseLocation(this)) {
            GpsSharingService.start(this)
        }
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
