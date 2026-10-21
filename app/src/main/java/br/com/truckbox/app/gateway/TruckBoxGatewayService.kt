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
    private lateinit var spool: TelemetrySpool
    private lateinit var locationManager: LocationManager
    private val lastFix = AtomicReference<Location?>(null)
    @Volatile private var coreOnline = false
    @Volatile private var lastGpsCloudTime = 0L

    override fun onCreate() {
        super.onCreate()
        prefs = TruckBoxPreferences(this)
        core = CoreGatewayClient(this, prefs)
        cloud = GatewayCloudClient(this, prefs)
        spool = TelemetrySpool(this)
        locationManager = getSystemService(Context.LOCATION_SERVICE) as LocationManager
        createChannel()
        startForeground(NOTIFICATION_ID, notification("Iniciando gateway…"))
        GatewayStatus.update { it.copy(running = true, lastError = null) }
        requestLocation()
        scope.launch { gatewayLoop() }
        scope.launch { cloudDrainLoop() }
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
     * ESP32 -> SQLite Android (COMMIT duravel) -> ACK ESP32.
     * Sem espaco/erro SQLite, o lote fica no Core.
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
                GatewayStatus.update {
                    it.copy(coreOnline = false, lastError = "Core nao encontrado na Starlink")
                }
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
                )
            }
            if (batch.count <= 0) {
                delay(1_000L)
                continue
            }
            try {
                val saved = spool.persist(batch, prefs.cloudDeviceUid)
                GatewayStatus.update {
                    it.copy(
                        androidPendingBytes = saved.bytes,
                        androidPendingCount = saved.count,
                        phoneNearLimit = saved.nearLimit,
                        lastError = when {
                            saved.nearLimit -> "Fila Android proxima de 512 MB"
                            saved.quarantinedBatches > 0L ->
                                "${saved.quarantinedBatches} lote(s) do ESP32 protegidos no Android; ${saved.quarantinedSamples} amostras exigem recuperacao"
                            else -> null
                        },
                    )
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                GatewayStatus.update {
                    it.copy(lastError = "Dados no ESP32: ${e.message?.take(150)}")
                }
                updateNotification()
                delay(5_000L)
                continue
            }
            // NUNCA confirmar ao Core antes da transacao no SQLite.
            if (!core.ack(batch.maxSeq)) {
                GatewayStatus.update {
                    it.copy(lastError = "Salvo no Android; ACK ESP32 falhou, tentando novamente")
                }
                updateNotification()
                delay(1_000L)
                continue
            }
            GatewayStatus.update { it.copy(lastAckSeq = batch.maxSeq) }
            updateNotification()
            delay(if (batch.pendingBytes > 12_000L) 250L else 800L)
        }
    }

    /**
     * Fila independente: nao depende do ESP32 estar online.
     * Retomada automatica apos fechar/reiniciar o app.
     */
    private suspend fun cloudDrainLoop() {
        var retryMs = 5_000L
        while (currentCoroutineContext().isActive) {
            if (!prefs.gatewayEnabled) {
                delay(2_000L)
                continue
            }
            try {
                val saved = spool.stats()
                GatewayStatus.update {
                    it.copy(
                        androidPendingBytes = saved.bytes,
                        androidPendingCount = saved.count,
                        phoneNearLimit = saved.nearLimit,
                    )
                }
                val upload = spool.nextBatch()
                if (upload == null) {
                    delay(2_000L)
                    continue
                }
                if (prefs.cloudDeviceToken.isBlank() || prefs.cloudDeviceUid.isBlank()) {
                    GatewayStatus.update {
                        it.copy(cloudOnline = false, lastError = "Fila segura no Android; configure UID/Token Cloud")
                    }
                    updateNotification()
                    delay(5_000L)
                    continue
                }

                val result = cloud.upload(upload.samples, upload.deviceUid)
                if (result == null || result.ackSeq < upload.maxSeq) {
                    val detail = cloud.lastError?.take(160)
                        ?: "ACK parcial da Cloud"
                    GatewayStatus.update {
                        it.copy(cloudOnline = false, lastError = "Fila Android pendente: $detail")
                    }
                    updateNotification()
                    delay(retryMs)
                    retryMs = (retryMs * 2).coerceAtMost(60_000L)
                    continue
                }
                // Apenas os IDs enviados e confirmados, nunca os futuros.
                val remaining = spool.confirmUploaded(upload, result.ackSeq)
                GatewayStatus.update {
                    it.copy(
                        cloudOnline = true,
                        androidPendingBytes = remaining.bytes,
                        androidPendingCount = remaining.count,
                        phoneNearLimit = remaining.nearLimit,
                        lastUploadMs = System.currentTimeMillis(),
                        lastError = if (remaining.quarantinedBatches > 0L)
                            "${remaining.quarantinedBatches} lote(s) brutos preservados no Android para recuperacao"
                        else null,
                    )
                }
                updateNotification()
                retryMs = 5_000L
                delay(200L)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                GatewayStatus.update {
                    it.copy(cloudOnline = false, lastError = "Fila Android: ${e.message?.take(160)}")
                }
                updateNotification()
                delay(retryMs)
                retryMs = (retryMs * 2).coerceAtMost(60_000L)
            }
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
            s.phoneNearLimit -> "ATENCAO: fila Android proxima de 512 MB"
            s.androidPendingCount > 0 && !s.cloudOnline ->
                "Salvos no Android: ${s.androidPendingBytes / 1024} KB, aguardando Cloud"
            s.androidPendingCount > 0 -> "Enviando ${s.androidPendingCount} amostras do Android"
            !s.coreOnline -> "Core offline • aguardando Starlink"
            prefs.cloudDeviceToken.isBlank() -> "Core online • configure a Cloud"
            s.pendingBytes > 0 -> "Salvando dados do ESP32 no Android"
            else -> "ESP32 + Cloud • fila local vazia"
        }
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(text))
    }
}
