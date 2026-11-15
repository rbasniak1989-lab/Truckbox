package br.com.truckbox.app.gateway

import android.Manifest
import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.*
import android.net.ConnectivityManager
import android.net.Network
import android.os.*
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import br.com.truckbox.app.MainActivity
import br.com.truckbox.app.cloud.DriverOperationsSync
import br.com.truckbox.app.cloud.DriverCloudException
import br.com.truckbox.app.gps.*
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.preferences.TruckBoxPreferences
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel

/** Legacy component name retained for update compatibility. Cloud and phone GPS only. */
class TruckBoxGatewayService: Service(),LocationListener {
    companion object {const val CHANNEL_ID="truckbox_driver_cloud";const val NOTIFICATION_ID=601}
    private val scope=CoroutineScope(SupervisorJob()+Dispatchers.IO)
    private val wake=Channel<Unit>(Channel.CONFLATED)
    private lateinit var prefs: TruckBoxPreferences
    private lateinit var outbox: DriverGpsOutbox
    private lateinit var locations: LocationManager
    private val uploader=GatewayCloudClient()
    private var callback: ConnectivityManager.NetworkCallback?=null
    private var foregroundStarted=false
    @Volatile private var lastFixNanos=0L
    private var selectedNanos=0L

    override fun onCreate() {
        super.onCreate()
        prefs=TruckBoxPreferences(this);outbox=DriverGpsOutbox(this)
        locations=getSystemService(Context.LOCATION_SERVICE) as LocationManager
        getSystemService(NotificationManager::class.java).createNotificationChannel(NotificationChannel(CHANNEL_ID,"TruckBox GPS e Cloud",NotificationManager.IMPORTANCE_LOW))
        if(!locationAllowed()) {GatewayStatus.update {it.copy(lastError="Permita localização para iniciar o GPS",running=false)};stopSelf();return}
        try {startForeground(NOTIFICATION_ID,notification("Aguardando configuração e posição do celular"))}
        catch(_: SecurityException) {GatewayStatus.update {it.copy(running=false,lastError="Abra o Motorista para revisar a permissão de localização")};stopSelf();return}
        catch(_: IllegalStateException) {GatewayStatus.update {it.copy(running=false,lastError="Abra o Motorista e toque em Iniciar GPS")};stopSelf();return}
        foregroundStarted=true
        GatewayStatus.update {it.copy(running=true,lastError=null)}
        requestLocation()
        val cm=getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        callback=object: ConnectivityManager.NetworkCallback(){
            override fun onAvailable(network: Network){wake.trySend(Unit)}
            override fun onLost(network: Network){GatewayStatus.update {it.copy(gpsCloudOnline=false,operationsCloudOnline=false)}}
        }
        runCatching {cm.registerDefaultNetworkCallback(callback!!)}
        scope.launch {gpsLoop()}
        scope.launch {while(isActive){DriverOperationsSync.sync(prefs,OperationsStore.get(this@TruckBoxGatewayService));delay(5000)}}
    }
    override fun onStartCommand(intent: Intent?,flags: Int,startId: Int): Int {
        if(!foregroundStarted)return START_NOT_STICKY
        requestLocation();wake.trySend(Unit);return START_STICKY
    }
    override fun onBind(intent: Intent?): IBinder?=null
    override fun onDestroy() {
        foregroundStarted=false
        if(::locations.isInitialized)runCatching {locations.removeUpdates(this)}
        callback?.let {cb->runCatching {(getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).unregisterNetworkCallback(cb)}}
        scope.cancel();if(::outbox.isInitialized)outbox.close()
        GatewayStatus.update {it.copy(running=false,gpsFix=false)};super.onDestroy()
    }
    private fun locationAllowed()=ActivityCompat.checkSelfPermission(this,Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED||
        ActivityCompat.checkSelfPermission(this,Manifest.permission.ACCESS_COARSE_LOCATION)==PackageManager.PERMISSION_GRANTED

    private fun requestLocation() {
        runCatching {locations.removeUpdates(this)}
        GatewayStatus.update {it.copy(gpsEnabled=prefs.gpsTrackingEnabled)}
        if(!locationAllowed()||!prefs.gpsTrackingEnabled)return
        val configured=runCatching {prefs.gpsCredentials().configured}.getOrDefault(false)
        if(!configured) {GatewayStatus.update {it.copy(lastError="Configure o acesso à Cloud para enviar o GPS")};return}
        try {
            val fine=ActivityCompat.checkSelfPermission(this,Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED
            if(fine&&LocationManager.GPS_PROVIDER in locations.allProviders)locations.requestLocationUpdates(LocationManager.GPS_PROVIDER,2000,0f,this,Looper.getMainLooper())
            if(LocationManager.NETWORK_PROVIDER in locations.allProviders)locations.requestLocationUpdates(LocationManager.NETWORK_PROVIDER,5000,0f,this,Looper.getMainLooper())
        } catch(_: SecurityException) {GatewayStatus.update {it.copy(lastError="Permissão de localização indisponível")}}
    }
    override fun onLocationChanged(location: Location) {
        if(!prefs.gpsTrackingEnabled)return
        val elapsed=SystemClock.elapsedRealtimeNanos()
        val sample=PhoneGpsSample.fromLocation(location,prefs.installId,System.currentTimeMillis(),elapsed) ?: return
        lastFixNanos=location.elapsedRealtimeNanos
        GatewayStatus.update {it.copy(gpsFix=true,lastGpsCaptureMs=sample.capturedAtMs,gpsAccuracyM=sample.accuracyM)}
        if(selectedNanos>0&&location.elapsedRealtimeNanos-selectedNanos<10_000_000_000L)return
        val credentials=runCatching {prefs.gpsCredentials()}.getOrNull() ?: return
        if(!credentials.configured)return
        selectedNanos=location.elapsedRealtimeNanos
        scope.launch {
            try {
                val result=outbox.enqueue(credentials.scope,sample)
                GatewayStatus.update {it.copy(pendingGps=outbox.countAll(),lastError=if(result==GpsEnqueue.FULL)"Fila GPS cheia; pontos anteriores preservados" else null)}
                if(result==GpsEnqueue.INSERTED)wake.trySend(Unit)
            } catch(_: Exception) {GatewayStatus.update {it.copy(lastError="Não foi possível guardar a posição no celular")}}
        }
    }
    private suspend fun gpsLoop() {
        var failures=0
        while(currentCoroutineContext().isActive) {
            var sent=0
            try {
                val credentials=prefs.gpsCredentials()
                val enabled=prefs.gpsTrackingEnabled
                val now=SystemClock.elapsedRealtimeNanos()
                GatewayStatus.update {it.copy(gpsEnabled=enabled,gpsFix=enabled&&lastFixNanos>0&&now-lastFixNanos in 0..30_000_000_000L,pendingGps=outbox.countAll())}
                if(enabled&&credentials.configured) {
                    sent=DriverGpsSync(outbox,uploader).drain(credentials,
                        keepSending={prefs.gpsTrackingEnabled&&credentials==prefs.gpsCredentials()}) {queued->
                        GatewayStatus.update {it.copy(gpsCloudOnline=true,lastGpsUploadMs=System.currentTimeMillis(),
                            lastGpsSentCaptureMs=maxOf(it.lastGpsSentCaptureMs ?: 0,queued.sample.capturedAtMs),pendingGps=outbox.countAll(),lastError=null)}
                    }
                }
                if(sent>0)failures=0
            } catch(e: CancellationException) {throw e} catch(e: Exception) {
                failures=(failures+1).coerceAtMost(5)
                GatewayStatus.update {it.copy(gpsCloudOnline=false,lastError=if(e is DriverCloudException&&e.status in listOf(401,403))"Acesso GPS recusado pela Cloud; confira a configuração" else "Envio GPS pendente; posições preservadas no celular")}
            }
            updateNotification()
            val waitMs=if(failures>0)(2000L shl failures).coerceAtMost(60000) else if(sent>0)1000L else 5000L
            withTimeoutOrNull(waitMs){wake.receive()}
        }
    }
    private fun notification(text: String): Notification {
        val open=PendingIntent.getActivity(this,0,Intent(this,MainActivity::class.java),PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        return NotificationCompat.Builder(this,CHANNEL_ID).setSmallIcon(android.R.drawable.ic_menu_mylocation).setContentTitle("TruckBox Motorista")
            .setContentText(text).setContentIntent(open).setOngoing(true).setCategory(NotificationCompat.CATEGORY_SERVICE).build()
    }
    private fun updateNotification() {
        val s=GatewayStatus.state.value
        val text=when {
            !s.gpsEnabled -> "GPS pausado"
            s.lastError!=null -> s.lastError
            s.pendingGps>0 -> "${s.pendingGps} posições aguardando envio"
            !s.gpsFix -> "Aguardando posição do celular"
            s.lastGpsUploadMs!=null -> "GPS enviado à Cloud"
            else -> "Posição recebida; aguardando envio"
        }
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID,notification(text))
    }
}
