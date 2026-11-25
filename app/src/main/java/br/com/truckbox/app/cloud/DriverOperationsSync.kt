package br.com.truckbox.app.cloud

import br.com.truckbox.app.gateway.GatewayStatus
import br.com.truckbox.app.operations.DriverAction
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.preferences.TruckBoxPreferences
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** One ordered sender shared by visible UI and location service; no Core/telemetry ACK. */
object DriverOperationsSync {
    private val mutex=Mutex()
    suspend fun sync(prefs: TruckBoxPreferences,store: OperationsStore,transport: DriverCloudTransport=DriverCloudTransport())=mutex.withLock {
        try {
            val credentials=prefs.cloudCredentials()
            if(!credentials.configured) {
                GatewayStatus.update {it.copy(operationsCloudOnline=false,operationsError="Configure o acesso à Cloud",pendingOperations=store.state.value.pendingCommands.size)}
                return@withLock
            }
            val client=TruckBoxCloudClient(prefs,transport,credentials)
            fun refresh() = (client.snapshot() ?: throw DriverCloudException(502)).let { snapshot ->
                credentials.vehicleId?.let { vehicle ->
                    for (key in listOf("trips", "fuelings", "expenses", "maintenance")) {
                        val rows = snapshot.optJSONArray(key) ?: continue
                        for (i in 0 until rows.length()) require(rows.getJSONObject(i).getString("truck_id") == vehicle)
                    }
                    snapshot.optJSONObject("operation")?.let { require(it.getString("truck_id") == vehicle) }
                }
                check(credentials == prefs.cloudCredentials())
                store.applyCloudSnapshot(snapshot)
            }
            var cloud = refresh()
            var refreshEntries = false
            val backlog = store.state.value.pendingCommands.filter { it.scope == credentials.scope }
            val heldStarts = backlog.filter { it.action == DriverAction.UPSERT_TRIP && it.awaitingTripReview }
                .map { it.entityUid }.toSet()
            val pending = backlog.filter { !it.awaitingTripReview && it.expense?.tripClientUid !in heldStarts }
            for (command in pending.take(8)) {
                if (credentials != prefs.cloudCredentials()) return@withLock
                val trip = command.trip
                if (trip != null) {
                    val existing = cloud.trips.firstOrNull { it.clientUid == trip.clientUid }
                    val starting = command.action == DriverAction.UPSERT_TRIP
                    if ((starting && existing != null) || (!starting && existing?.status == "COMPLETED")) {
                        store.acknowledgeCommand(command.id)
                        continue
                    }
                    if ((starting && cloud.hasActiveTrip) || (!starting && cloud.activeTrip?.clientUid != trip.clientUid)) {
                        store.holdTripCommand(command.id)
                        continue
                    }
                }
                val expenseTrip = command.expense?.tripClientUid
                if (expenseTrip != null && cloud.trips.none { it.clientUid == expenseTrip } &&
                    store.state.value.pendingCommands.any { it.action == DriverAction.UPSERT_TRIP && it.entityUid == expenseTrip }) continue
                if (!client.execute(command)) throw DriverCloudException(502)
                store.acknowledgeCommand(command.id)
                if (trip != null) cloud = refresh() else refreshEntries = true
            }
            if (refreshEntries) refresh()
            val queued = store.state.value.pendingCommands
            GatewayStatus.update {
                it.copy(operationsCloudOnline = true, lastUploadMs = System.currentTimeMillis(), pendingOperations = queued.size,
                    operationsError = if (queued.any { command -> command.awaitingTripReview })
                        "Há registros de viagem offline preservados para revisão. Consulte a viagem ativa da Cloud." else null)
            }
        } catch(e: CancellationException) {throw e} catch(_: Exception) {
            GatewayStatus.update {it.copy(operationsCloudOnline=false,operationsError="Sincronização pendente; dados locais preservados",pendingOperations=store.state.value.pendingCommands.size)}
        }
    }
}
