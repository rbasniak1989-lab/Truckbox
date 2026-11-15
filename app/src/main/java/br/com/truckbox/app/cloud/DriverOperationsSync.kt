package br.com.truckbox.app.cloud

import br.com.truckbox.app.gateway.GatewayStatus
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
            repeat(8) {
                if(credentials!=prefs.cloudCredentials())return@withLock
                val command=store.state.value.pendingCommands.firstOrNull {it.scope==credentials.scope} ?: return@repeat
                if(!client.execute(command))throw DriverCloudException(502)
                store.acknowledgeCommand(command.id)
            }
            val snapshot=client.snapshot() ?: throw DriverCloudException(502)
            credentials.vehicleId?.let {vehicle->
                for(key in listOf("trips","fuelings","expenses","maintenance")) {
                    val rows=snapshot.optJSONArray(key) ?: continue
                    for(i in 0 until rows.length())require(rows.getJSONObject(i).getString("truck_id")==vehicle)
                }
                snapshot.optJSONObject("operation")?.let {require(it.getString("truck_id")==vehicle)}
            }
            if(credentials!=prefs.cloudCredentials())return@withLock
            store.applyCloudSnapshot(snapshot)
            GatewayStatus.update {it.copy(operationsCloudOnline=true,lastUploadMs=System.currentTimeMillis(),operationsError=null,pendingOperations=store.state.value.pendingCommands.size)}
        } catch(e: CancellationException) {throw e} catch(_: Exception) {
            GatewayStatus.update {it.copy(operationsCloudOnline=false,operationsError="Sincronização pendente; dados locais preservados",pendingOperations=store.state.value.pendingCommands.size)}
        }
    }
}
