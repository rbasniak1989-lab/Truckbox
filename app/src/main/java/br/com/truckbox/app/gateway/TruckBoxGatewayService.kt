package br.com.truckbox.app.gateway

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import br.com.truckbox.app.cloud.TruckBoxCloudClient
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.preferences.TruckBoxPreferences
import kotlinx.coroutines.*
import java.util.concurrent.atomic.AtomicReference

class TruckBoxGatewayService : Service(), LocationListener {
    companion object {
        const val CHANNEL_ID = "truckbox_driver_gateway"
        const val NOTIFICATION_ID = 601
        private const val FIX_MAX_AGE_MS = 30_000L
        private const val GPS_CLOUD_INTERVAL_MS = 10_000L
        private const val LIVE_POLL_MS = 2_000L
        private const val OPS_SYNC_MS = 30_000L
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private lateinit var prefs: TruckBoxPreferences
    private lateinit var core: CoreGatewayClient
    private lateinit var cloud: GatewayCloudClient
    private lateinit var locationManager: LocationManager
    private val lastFix = AtomicReference<Location?>(null)
    @Volatile private var coreOnline = false
    @Volatile private var lastGpsCloudTime = 0L

    override fun onCreate() {
        super.onCreate()
        prefs = TruckBoxPreferences(this)
        core = CoreGatewayClient(this, prefs)
        cloud = GatewayCloudClient(prefs)
        locationManager = getSystemService(Context.LOCATION_SERVICE) as LocationManager
        createChannel()
        startForeground(NOTIFICATION_ID, notification("Iniciando gateway…"))
        GatewayStatus.update { it.copy(running = true, lastError = null) }
        requestLocation()
        scope.launch { gatewayLoop() }
        scope.launch { liveLoop() }
        scope.launch { gpsCloudLoop() }
        scope.launch { operationalSyncLoop() }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_STICKY
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        runCatching { locationManager.removeUpdates(this) }
        scope.cancel()
        GatewayStatus.update { it.copy(running = false, coreOnline = false) }
        super.onDestroy()
    }

    override fun onLocationChanged(location: Location) {
        val old = lastFix.get()
        if (old == null || location.time >= old.time || (location.hasAccuracy() && old.hasAccuracy() && location.accuracy < old.accuracy)) {
            lastFix.set(location)
        }
        val age = (System.currentTimeMillis() - location.time).coerceAtLeast(0L)
        GatewayStatus.update { it.copy(gpsFix = age <= FIX_MAX_AGE_MS) }
        scope.launch {
            // GPS local: deixa /api/state do Core e futura multimídia com posição atual.
            if (core.pushGps(location)) coreOnline = true
        }
    }

    private fun requestLocation() {
        val fine = ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        val coarse = ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (!fine && !coarse) {
            GatewayStatus.update { it.copy(lastError = "Permissão de localização não concedida") }
            return
        }
        runCatching {
            if (fine && locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 2_000L, 0f, this)
            }
            if (locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 5_000L, 0f, this)
            }
        }.onFailure { e -> GatewayStatus.update { it.copy(lastError = "GPS: ${e.message}") } }
    }

    /**
     * Caminho crítico novo:
     * ESP/LittleFS -> GET batch -> Android HTTPS -> Cloud ACK -> POST ack -> ESP.
     * Se qualquer passo falhar, o ESP NÃO libera o lote.
     */
    private suspend fun gatewayLoop() {
        while (currentCoroutineContext().isActive) {
            if (!prefs.gatewayEnabled) {
                delay(2_000L)
                continue
            }

            val batch = core.fetchBatch(8)
            if (batch == null) {
                coreOnline = false
                GatewayStatus.update { it.copy(coreOnline = false, cloudOnline = false, lastError = "Core não encontrado na Starlink") }
                updateNotification()
                delay(2_000L)
                continue
            }

            coreOnline = true
            GatewayStatus.update {
                it.copy(
                    coreOnline = true,
                    pendingBytes = batch.pendingBytes,
                    lastAckSeq = batch.lastAckSeq,
                    lastBatchCount = batch.count,
                    lastError = null,
                )
            }

            if (batch.count <= 0) {
                updateNotification()
                delay(1_000L)
                continue
            }

            if (prefs.cloudDeviceToken.isBlank()) {
                GatewayStatus.update { it.copy(cloudOnline = false, lastError = "Token Cloud do Core não configurado") }
                updateNotification()
                delay(5_000L)
                continue
            }

            val result = cloud.upload(batch.samples, batch.deviceUid)
            if (result == null || result.ackSeq <= 0L) {
                GatewayStatus.update { it.copy(cloudOnline = false, lastError = "Falha ao enviar lote para a Cloud") }
                updateNotification()
                delay(2_000L)
                continue
            }

            // Segurança: só libera o lote se a Cloud confirmou pelo menos o maior seq oferecido.
            if (result.ackSeq < batch.maxSeq) {
                GatewayStatus.update { it.copy(cloudOnline = true, lastError = "ACK Cloud incompleto: ${result.ackSeq}/${batch.maxSeq}") }
                delay(2_000L)
                continue
            }

            if (!core.ack(batch.maxSeq)) {
                // Sem problema de integridade: no próximo ciclo o mesmo lote volta e o upsert da Cloud é idempotente.
                GatewayStatus.update { it.copy(cloudOnline = true, lastError = "Cloud recebeu, mas ACK local falhou; lote será repetido") }
                delay(1_000L)
                continue
            }

            GatewayStatus.update {
                it.copy(
                    coreOnline = true,
                    cloudOnline = true,
                    lastAckSeq = batch.maxSeq,
                    lastUploadMs = System.currentTimeMillis(),
                    lastError = null,
                )
            }
            updateNotification()

            // Backlog é drenado rápido, mas com pequena folga para não monopolizar CPU/rede.
            delay(if (batch.pendingBytes > 12_000L) 250L else 800L)
        }
    }

    private suspend fun liveLoop() {
        while (currentCoroutineContext().isActive) {
            val s = core.fetchLive()
            if (s != null) {
                coreOnline = true
                GatewayStatus.update {
                    it.copy(
                        coreOnline = true,
                        odometerKm = s.odometerKm,
                        coreFuelLiters = s.coreFuelLiters,
                        speedKmh = s.speedKmh,
                        rpm = s.rpm,
                        combinationWeightKg = s.combinationWeightKg,
                    )
                }
            }
            delay(LIVE_POLL_MS)
        }
    }

    private suspend fun gpsCloudLoop() {
        while (currentCoroutineContext().isActive) {
            val fix = lastFix.get()
            val age = fix?.let { System.currentTimeMillis() - it.time } ?: Long.MAX_VALUE
            val valid = fix != null && age in 0..FIX_MAX_AGE_MS
            GatewayStatus.update { it.copy(gpsFix = valid) }
            if (coreOnline && valid && fix != null && fix.time != lastGpsCloudTime && prefs.gpsDeviceToken.isNotBlank()) {
                if (cloud.uploadGps(fix, prefs.installId)) {
                    lastGpsCloudTime = fix.time
                    GatewayStatus.update { it.copy(lastGpsUploadMs = System.currentTimeMillis()) }
                }
            }
            delay(GPS_CLOUD_INTERVAL_MS)
        }
    }

    /**
     * Reenvio idempotente dos cadastros locais.
     *
     * Regra importante: viagem COMPLETED antiga nunca pode disparar end_trip de novo quando
     * já existe uma viagem ACTIVE. O end_trip altera o estado operacional para EMPTY_MODE;
     * portanto, reexecutá-lo para histórico encerrado pisaria numa viagem nova.
     *
     * Para viagens concluídas já conhecidas pelo Cloud usamos upsert_trip (sem efeito colateral
     * de modo). end_trip fica reservado somente para recuperar um encerramento que ainda não
     * chegou ao Cloud e apenas quando não há viagem local ativa.
     */
    private suspend fun operationalSyncLoop() {
        while (currentCoroutineContext().isActive) {
            if (prefs.cloudDeviceToken.isNotBlank()) {
                val op = OperationsStore(this).state.value
                val client = TruckBoxCloudClient(prefs)
                val localTrips = op.trips.take(100)
                val localActive = op.activeTrip ?: localTrips.firstOrNull { it.status == "ACTIVE" }
                val snapshot = client.snapshot()
                val cloudTrips = snapshot?.optJSONArray("trips")
                val cloudStatusByUid = buildMap<String, String> {
                    if (cloudTrips != null) {
                        for (i in 0 until cloudTrips.length()) {
                            val item = cloudTrips.optJSONObject(i) ?: continue
                            val uid = item.optString("client_uid")
                            if (uid.isNotBlank()) put(uid, item.optString("status"))
                        }
                    }
                }

                localTrips.forEach { trip ->
                    if (trip.status == "ACTIVE") {
                        client.syncTrip(trip)
                    } else {
                        // Sincroniza dados históricos sem mexer no modo operacional.
                        client.syncTrip(trip)
                    }
                }

                if (localActive == null) {
                    val pendingEnd = localTrips
                        .asSequence()
                        .filter { it.status == "COMPLETED" }
                        .filter { cloudStatusByUid[it.clientUid] != "COMPLETED" }
                        .maxByOrNull { it.endedAtMs ?: it.startedAtMs }
                    pendingEnd?.let { trip ->
                        // Se ainda não existia no Cloud, o upsert acima cria/atualiza antes do end_trip.
                        client.endTrip(trip)
                    }
                }

                op.fuelings.take(100).forEach { client.syncFueling(it) }
                op.expenses.take(100).forEach { client.syncExpense(it) }
                op.maintenance.take(100).forEach { client.syncMaintenance(it) }
            }
            delay(OPS_SYNC_MS)
        }
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            getSystemService(NotificationManager::class.java).createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "TruckBox Gateway", NotificationManager.IMPORTANCE_LOW)
            )
        }
    }

    private fun notification(text: String): Notification = NotificationCompat.Builder(this, CHANNEL_ID)
        .setSmallIcon(android.R.drawable.stat_sys_upload)
        .setContentTitle("TruckBox Motorista")
        .setContentText(text)
        .setOngoing(true)
        .setCategory(NotificationCompat.CATEGORY_SERVICE)
        .build()

    private fun updateNotification() {
        val s = GatewayStatus.state.value
        val text = when {
            !s.coreOnline -> "Core offline • aguardando Starlink"
            prefs.cloudDeviceToken.isBlank() -> "Core online • configure a Cloud"
            !s.cloudOnline && s.pendingBytes > 0 -> "Core online • ${s.pendingBytes / 1024} KB aguardando Cloud"
            s.pendingBytes > 0 -> "Sincronizando • ${s.pendingBytes / 1024} KB pendentes"
            else -> "Core + Cloud online • sincronizado"
        }
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(text))
    }
}
