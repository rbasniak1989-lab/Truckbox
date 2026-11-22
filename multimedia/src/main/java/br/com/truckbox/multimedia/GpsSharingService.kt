package br.com.truckbox.multimedia

import android.Manifest
import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.database.sqlite.SQLiteException
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import androidx.core.location.LocationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicReference

internal fun hasPreciseLocation(context: Context): Boolean = ContextCompat.checkSelfPermission(
    context, Manifest.permission.ACCESS_FINE_LOCATION,
) == PackageManager.PERMISSION_GRANTED

internal data class GpsSharingState(
    val running: Boolean = false,
    val capture: String = "Compartilhamento desativado",
    val upload: String = "",
    val pending: Long = 0,
    val discarded: Long = 0,
    val lastConfirmedAt: Long = 0,
)

class GpsSharingService : Service(), LocationListener {
    companion object {
        private const val NOTIFICATION = 407
        private const val CHANNEL = "truckbox_multimedia_gps"
        private const val STOP = "br.com.truckbox.multimedia.action.STOP_GPS"
        private val mutableState = MutableStateFlow(GpsSharingState())
        internal val state = mutableState.asStateFlow()

        internal fun start(context: Context): Boolean = runCatching {
            ContextCompat.startForegroundService(context, Intent(context, GpsSharingService::class.java))
        }.isSuccess

        internal fun stop(context: Context) {
            GpsSettings(context).enabled = false
            context.stopService(Intent(context, GpsSharingService::class.java))
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val settings by lazy { GpsSettings(this) }
    private val locations by lazy { getSystemService(LocationManager::class.java) }
    private val fix = AtomicReference<Location?>()
    private var started = false
    private var listening = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == STOP) settings.enabled = false
        if (!settings.enabled || !hasPreciseLocation(this)) {
            mutableState.update { it.copy(capture = if (settings.enabled) "Permita a localização precisa" else "Compartilhamento desativado") }
            stopSelf()
            return START_NOT_STICKY
        }
        if (started) return START_NOT_STICKY

        val notificationManager = getSystemService(NotificationManager::class.java)
        notificationManager.createNotificationChannel(NotificationChannel(CHANNEL, "GPS TruckBox", NotificationManager.IMPORTANCE_LOW))
        val stop = PendingIntent.getService(this, 0, Intent(this, GpsSharingService::class.java).setAction(STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE)
        val notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setContentTitle("TruckBox Multimídia GPS")
            .setContentText("Compartilhando a localização da central")
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(0, "Parar", stop)
            .build()
        try {
            ServiceCompat.startForeground(this, NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION)
        } catch (_: RuntimeException) {
            settings.enabled = false
            mutableState.update { it.copy(capture = "O Android bloqueou a ativação do GPS") }
            stopSelf()
            return START_NOT_STICKY
        }

        started = true
        mutableState.value = GpsSharingState(running = true, capture = "Aguardando sinal de GPS")
        listenForGps()
        scope.launch {
            val credentials = settings.credentials()
            if (credentials == null) {
                settings.enabled = false
                mutableState.update { it.copy(capture = "Informe o cadastro GPS da TruckBox Cloud") }
                stopSelf()
                return@launch
            }
            GpsOutbox(this@GpsSharingService).use { outbox ->
                coroutineScope {
                    launch { captureLoop(outbox, credentials.deviceUid, settings.installId) }
                    launch { uploadLoop(outbox, credentials) }
                }
            }
        }
        return START_NOT_STICKY
    }

    @SuppressLint("MissingPermission") // onStartCommand verifies the precise-location permission.
    private fun listenForGps() {
        listening = runCatching {
            locations.requestLocationUpdates(LocationManager.GPS_PROVIDER, 2_000L, 0f, this, Looper.getMainLooper())
        }.isSuccess
    }

    override fun onLocationChanged(location: Location) {
        fix.set(location.takeIf { it.provider == LocationManager.GPS_PROVIDER && !LocationCompat.isMock(it) }?.let(::Location))
    }

    override fun onProviderEnabled(provider: String) = Unit
    override fun onProviderDisabled(provider: String) { fix.set(null) }
    @Deprecated("Recepção em versões antigas do Android")
    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) = Unit

    private suspend fun captureLoop(outbox: GpsOutbox, deviceUid: String, installId: String) {
        var nextCapture = 0L
        var lastSample = 0L
        while (currentCoroutineContext().isActive) {
            val elapsed = SystemClock.elapsedRealtime()
            val sample = fix.get()?.let { location ->
                gpsPoint(installId, location.time, System.currentTimeMillis(), elapsed - location.elapsedRealtimeNanos / 1_000_000,
                    location.latitude, location.longitude, location.speed.toDouble().takeIf { location.hasSpeed() },
                    location.bearing.toDouble().takeIf { location.hasBearing() }, location.altitude.takeIf { location.hasAltitude() },
                    location.accuracy.toDouble().takeIf { location.hasAccuracy() })
            }
            val capture = when {
                !listening -> "GPS indisponível nesta central"
                !locations.isProviderEnabled(LocationManager.GPS_PROVIDER) -> "Ative a localização da central"
                sample == null -> "Aguardando sinal de GPS"
                else -> "Sinal de GPS recebido"
            }
            mutableState.update { it.copy(capture = capture) }
            if (sample != null && elapsed >= nextCapture && sample.capturedAt > lastSample) {
                nextCapture = elapsed + GPS_INTERVAL_MS
                try {
                    val saved = outbox.enqueue(deviceUid, sample)
                    lastSample = sample.capturedAt
                    val pending = outbox.pending(deviceUid)
                    mutableState.update { it.copy(pending = pending, discarded = it.discarded + if (saved) 0 else 1) }
                } catch (_: SQLiteException) {
                    mutableState.update { it.copy(capture = "Falha ao gravar a posição no Android") }
                }
            }
            delay(2_000)
        }
    }

    private suspend fun uploadLoop(outbox: GpsOutbox, credentials: GpsCredentials) {
        var failures = 0
        while (currentCoroutineContext().isActive) {
            try {
                val pending = outbox.pending(credentials.deviceUid)
                mutableState.update { it.copy(pending = pending) }
                val point = outbox.oldest(credentials.deviceUid)
                if (point == null) {
                    delay(2_000)
                    continue
                }
                val result = uploadGps(credentials, point)
                currentCoroutineContext().ensureActive()
                if (result.confirmed) {
                    outbox.acknowledge(credentials.deviceUid, point)
                    val remaining = outbox.pending(credentials.deviceUid)
                    mutableState.update { it.copy(upload = "Cloud confirmou o envio", lastConfirmedAt = System.currentTimeMillis(), pending = remaining) }
                    failures = 0
                    delay(2_000)
                    continue
                }
                val status = when (val code = result.httpStatus) {
                    0 -> "Aguardando conexão com a Cloud"
                    401, 403 -> "A Cloud recusou o ID ou token GPS"
                    in 200..299 -> "Aguardando confirmação válida da Cloud"
                    else -> "Envio GPS pendente (HTTP $code)"
                }
                mutableState.update { it.copy(upload = status) }
                failures = (failures + 1).coerceAtMost(5)
                val pause = when (result.httpStatus) {
                    401, 403, 429 -> 60_000L
                    else -> (2_000L shl failures).coerceAtMost(60_000)
                }
                delay(pause)
            } catch (_: SQLiteException) {
                mutableState.update { it.copy(upload = "Fila de GPS indisponível") }
                delay(5_000)
            }
        }
    }

    override fun onDestroy() {
        if (listening) locations.removeUpdates(this)
        scope.cancel()
        mutableState.update { it.copy(running = false, capture = if (settings.enabled) it.capture else "Compartilhamento desativado") }
        super.onDestroy()
    }
}
