package br.com.truckbox.app.operations

import android.content.Context
import br.com.truckbox.app.data.OperationMode
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID


data class TripRecord(
    val clientUid: String = UUID.randomUUID().toString(),
    val origin: String,
    val destination: String,
    val startedAtMs: Long,
    val endedAtMs: Long? = null,
    val startOdometerKm: Double? = null,
    val endOdometerKm: Double? = null,
    val startFuelCounterL: Double = 0.0,
    val endFuelCounterL: Double? = null,
    val weightT: Double? = null,
    val ratePerT: Double? = null,
    val freightTotal: Double? = null,
    val distanceKm: Double? = null,
    val fuelLiters: Double? = null,
    val averageKml: Double? = null,
    val status: String = "ACTIVE",
)

data class FuelingRecord(
    val clientUid: String = UUID.randomUUID().toString(),
    val tsMs: Long,
    val liters: Double,
    val pricePerL: Double,
    val total: Double,
    val station: String,
    val location: String,
    val fullTank: Boolean,
    val odometerKm: Double?,
)

data class ExpenseRecord(
    val clientUid: String = UUID.randomUUID().toString(),
    val tsMs: Long,
    val category: String,
    val description: String,
    val amount: Double,
    val tripClientUid: String? = null,
)

data class MaintenanceRecord(
    val clientUid: String = UUID.randomUUID().toString(),
    val serviceType: String,
    val performedAtMs: Long,
    val odometerKm: Double?,
    val nextDueKm: Double?,
    val cost: Double?,
    val notes: String,
)

data class OperationsSnapshot(
    val mode: OperationMode = OperationMode.NO_TRIP,
    val activeTrip: TripRecord? = null,
    val trips: List<TripRecord> = emptyList(),
    val fuelings: List<FuelingRecord> = emptyList(),
    val expenses: List<ExpenseRecord> = emptyList(),
    val maintenance: List<MaintenanceRecord> = emptyList(),
    val emptyStartedAtMs: Long? = null,
    val emptyStartOdometerKm: Double? = null,
    val emptyStartFuelCounterL: Double? = null,
)

/**
 * Persistência operacional local-first para a v0.4.1.
 * O formato JSON em SharedPreferences é deliberadamente simples para o Field Test;
 * os IDs clientUid são estáveis e já servem para upsert na nuvem.
 */
class OperationsStore(context: Context) {
    private val p = context.getSharedPreferences("truckbox_operations_v041", Context.MODE_PRIVATE)
    private val _state = MutableStateFlow(load())
    val state: StateFlow<OperationsSnapshot> = _state.asStateFlow()

    fun startTrip(
        origin: String,
        destination: String,
        weightT: Double?,
        ratePerT: Double?,
        currentOdometerKm: Double?,
        currentFuelCounterL: Double,
    ) {
        if (origin.isBlank() || destination.isBlank()) return
        val freightTotal = if (weightT != null && ratePerT != null) weightT * ratePerT else null
        val trip = TripRecord(
            origin = origin.trim(),
            destination = destination.trim(),
            startedAtMs = System.currentTimeMillis(),
            startOdometerKm = currentOdometerKm,
            startFuelCounterL = currentFuelCounterL,
            weightT = weightT,
            ratePerT = ratePerT,
            freightTotal = freightTotal,
        )
        update(_state.value.copy(
            mode = OperationMode.LOADED_TRIP,
            activeTrip = trip,
            trips = listOf(trip) + _state.value.trips.filterNot { it.clientUid == trip.clientUid },
            emptyStartedAtMs = null,
            emptyStartOdometerKm = null,
            emptyStartFuelCounterL = null,
        ))
    }

    fun endActiveTrip(currentOdometerKm: Double?, currentFuelCounterL: Double) {
        val current = _state.value.activeTrip ?: return
        val distance = if (current.startOdometerKm != null && currentOdometerKm != null) {
            (currentOdometerKm - current.startOdometerKm).coerceAtLeast(0.0)
        } else null
        val fuel = (currentFuelCounterL - current.startFuelCounterL).coerceAtLeast(0.0)
        val avg = if (distance != null && fuel > 0.02) distance / fuel else null
        val ended = current.copy(
            endedAtMs = System.currentTimeMillis(),
            endOdometerKm = currentOdometerKm,
            endFuelCounterL = currentFuelCounterL,
            distanceKm = distance,
            fuelLiters = fuel,
            averageKml = avg,
            status = "COMPLETED",
        )
        update(_state.value.copy(
            mode = OperationMode.EMPTY_MODE,
            activeTrip = null,
            trips = listOf(ended) + _state.value.trips.filterNot { it.clientUid == ended.clientUid },
            emptyStartedAtMs = ended.endedAtMs,
            emptyStartOdometerKm = currentOdometerKm,
            emptyStartFuelCounterL = currentFuelCounterL,
        ))
    }

    fun addFueling(
        liters: Double,
        pricePerL: Double,
        station: String,
        location: String,
        fullTank: Boolean,
        odometerKm: Double?,
    ): FuelingRecord? {
        if (liters <= 0.0 || pricePerL < 0.0) return null
        val item = FuelingRecord(
            tsMs = System.currentTimeMillis(),
            liters = liters,
            pricePerL = pricePerL,
            total = liters * pricePerL,
            station = station.trim(),
            location = location.trim(),
            fullTank = fullTank,
            odometerKm = odometerKm,
        )
        update(_state.value.copy(fuelings = listOf(item) + _state.value.fuelings))
        return item
    }

    fun addExpense(category: String, description: String, amount: Double) {
        if (amount < 0.0) return
        val item = ExpenseRecord(
            tsMs = System.currentTimeMillis(),
            category = category.ifBlank { "Outros" }.trim(),
            description = description.trim(),
            amount = amount,
            tripClientUid = _state.value.activeTrip?.clientUid,
        )
        update(_state.value.copy(expenses = listOf(item) + _state.value.expenses))
    }

    fun addMaintenance(
        serviceType: String,
        odometerKm: Double?,
        nextDueKm: Double?,
        cost: Double?,
        notes: String,
    ) {
        if (serviceType.isBlank()) return
        val item = MaintenanceRecord(
            serviceType = serviceType.trim(),
            performedAtMs = System.currentTimeMillis(),
            odometerKm = odometerKm,
            nextDueKm = nextDueKm,
            cost = cost,
            notes = notes.trim(),
        )
        update(_state.value.copy(maintenance = listOf(item) + _state.value.maintenance))
    }

    /**
     * Aplica o snapshot vindo do TruckBox Cloud. O Cloud é autoridade para o
     * estado operacional quando ele possui uma viagem ativa/remotamente encerrada,
     * mas registros locais ainda não enviados são preservados pelo clientUid.
     */
    fun applyCloudSnapshot(root: JSONObject) {
        if (!root.optBoolean("ok", false)) return

        val cloudTrips = root.optJSONArray("trips").toList(::tripFromCloudJson)
        val cloudFuelings = root.optJSONArray("fuelings").toList(::fuelingFromCloudJson)
        val cloudExpenses = root.optJSONArray("expenses").toList(::expenseFromCloudJson)
        val cloudMaintenance = root.optJSONArray("maintenance").toList(::maintenanceFromCloudJson)
        val op = root.optJSONObject("operation")

        val mergedTrips = mergeByUid(cloudTrips, _state.value.trips) { it.clientUid }
        val mergedFuelings = mergeByUid(cloudFuelings, _state.value.fuelings) { it.clientUid }
        val mergedExpenses = mergeByUid(cloudExpenses, _state.value.expenses) { it.clientUid }
        val mergedMaintenance = mergeByUid(cloudMaintenance, _state.value.maintenance) { it.clientUid }

        val cloudActive = cloudTrips.firstOrNull { it.status == "ACTIVE" }
        val cloudMode = op?.optString("mode")?.let { runCatching { OperationMode.valueOf(it) }.getOrNull() }

        // Se o Cloud conhece uma viagem ativa, ela vence imediatamente.
        // Se o Cloud explicitamente encerrou a mesma viagem local, limpamos o ativo.
        // Uma viagem local offline ainda inexistente no Cloud não é apagada por engano.
        val localActive = _state.value.activeTrip
        val cloudHasLocalActive = localActive?.let { a -> cloudTrips.any { it.clientUid == a.clientUid } } == true
        val active = when {
            cloudActive != null -> cloudActive
            localActive != null && !cloudHasLocalActive -> localActive
            else -> null
        }
        val mode = when {
            active != null -> OperationMode.LOADED_TRIP
            cloudMode != null -> cloudMode
            else -> _state.value.mode
        }

        update(
            _state.value.copy(
                mode = mode,
                activeTrip = active,
                trips = mergedTrips,
                fuelings = mergedFuelings,
                expenses = mergedExpenses,
                maintenance = mergedMaintenance,
                emptyStartedAtMs = op?.optNullableIsoMs("empty_started_at") ?: _state.value.emptyStartedAtMs,
                emptyStartOdometerKm = op?.optNullableDouble("empty_start_odometer_km") ?: _state.value.emptyStartOdometerKm,
                emptyStartFuelCounterL = op?.optNullableDouble("empty_start_fuel_counter_l") ?: _state.value.emptyStartFuelCounterL,
            )
        )
    }

    private fun update(value: OperationsSnapshot) {
        _state.value = value
        save(value)
    }

    private fun save(s: OperationsSnapshot) {
        val root = JSONObject()
        root.put("mode", s.mode.name)
        root.put("activeTripUid", s.activeTrip?.clientUid)
        root.put("emptyStartedAtMs", s.emptyStartedAtMs)
        root.put("emptyStartOdometerKm", s.emptyStartOdometerKm)
        root.put("emptyStartFuelCounterL", s.emptyStartFuelCounterL)
        root.put("trips", JSONArray().apply { s.trips.forEach { put(it.toJson()) } })
        root.put("fuelings", JSONArray().apply { s.fuelings.forEach { put(it.toJson()) } })
        root.put("expenses", JSONArray().apply { s.expenses.forEach { put(it.toJson()) } })
        root.put("maintenance", JSONArray().apply { s.maintenance.forEach { put(it.toJson()) } })
        p.edit().putString("snapshot", root.toString()).apply()
    }

    private fun load(): OperationsSnapshot {
        val raw = p.getString("snapshot", null) ?: return OperationsSnapshot()
        return runCatching {
            val root = JSONObject(raw)
            val trips = root.optJSONArray("trips").toList(::tripFromJson)
            val activeUid = root.optNullableString("activeTripUid")
            OperationsSnapshot(
                mode = runCatching { OperationMode.valueOf(root.optString("mode", OperationMode.NO_TRIP.name)) }.getOrDefault(OperationMode.NO_TRIP),
                activeTrip = trips.firstOrNull { it.clientUid == activeUid && it.status == "ACTIVE" },
                trips = trips,
                fuelings = root.optJSONArray("fuelings").toList(::fuelingFromJson),
                expenses = root.optJSONArray("expenses").toList(::expenseFromJson),
                maintenance = root.optJSONArray("maintenance").toList(::maintenanceFromJson),
                emptyStartedAtMs = root.optNullableLong("emptyStartedAtMs"),
                emptyStartOdometerKm = root.optNullableDouble("emptyStartOdometerKm"),
                emptyStartFuelCounterL = root.optNullableDouble("emptyStartFuelCounterL"),
            )
        }.getOrElse { OperationsSnapshot() }
    }
}

private fun TripRecord.toJson() = JSONObject().apply {
    put("clientUid", clientUid); put("origin", origin); put("destination", destination)
    put("startedAtMs", startedAtMs); put("endedAtMs", endedAtMs)
    put("startOdometerKm", startOdometerKm); put("endOdometerKm", endOdometerKm)
    put("startFuelCounterL", startFuelCounterL); put("endFuelCounterL", endFuelCounterL)
    put("weightT", weightT); put("ratePerT", ratePerT); put("freightTotal", freightTotal)
    put("distanceKm", distanceKm); put("fuelLiters", fuelLiters); put("averageKml", averageKml); put("status", status)
}
private fun FuelingRecord.toJson() = JSONObject().apply {
    put("clientUid", clientUid); put("tsMs", tsMs); put("liters", liters); put("pricePerL", pricePerL); put("total", total)
    put("station", station); put("location", location); put("fullTank", fullTank); put("odometerKm", odometerKm)
}
private fun ExpenseRecord.toJson() = JSONObject().apply {
    put("clientUid", clientUid); put("tsMs", tsMs); put("category", category); put("description", description); put("amount", amount); put("tripClientUid", tripClientUid)
}
private fun MaintenanceRecord.toJson() = JSONObject().apply {
    put("clientUid", clientUid); put("serviceType", serviceType); put("performedAtMs", performedAtMs); put("odometerKm", odometerKm)
    put("nextDueKm", nextDueKm); put("cost", cost); put("notes", notes)
}

private fun tripFromJson(o: JSONObject): TripRecord = TripRecord(
    clientUid = o.optString("clientUid"), origin = o.optString("origin"), destination = o.optString("destination"),
    startedAtMs = o.optLong("startedAtMs"), endedAtMs = o.optNullableLong("endedAtMs"), startOdometerKm = o.optNullableDouble("startOdometerKm"),
    endOdometerKm = o.optNullableDouble("endOdometerKm"), startFuelCounterL = o.optDouble("startFuelCounterL", 0.0),
    endFuelCounterL = o.optNullableDouble("endFuelCounterL"), weightT = o.optNullableDouble("weightT"), ratePerT = o.optNullableDouble("ratePerT"),
    freightTotal = o.optNullableDouble("freightTotal"), distanceKm = o.optNullableDouble("distanceKm"), fuelLiters = o.optNullableDouble("fuelLiters"),
    averageKml = o.optNullableDouble("averageKml"), status = o.optString("status", "ACTIVE")
)
private fun fuelingFromJson(o: JSONObject): FuelingRecord = FuelingRecord(
    clientUid=o.optString("clientUid"), tsMs=o.optLong("tsMs"), liters=o.optDouble("liters"), pricePerL=o.optDouble("pricePerL"), total=o.optDouble("total"),
    station=o.optString("station"), location=o.optString("location"), fullTank=o.optBoolean("fullTank"), odometerKm=o.optNullableDouble("odometerKm")
)
private fun expenseFromJson(o: JSONObject): ExpenseRecord = ExpenseRecord(
    clientUid=o.optString("clientUid"), tsMs=o.optLong("tsMs"), category=o.optString("category"), description=o.optString("description"), amount=o.optDouble("amount"), tripClientUid=o.optNullableString("tripClientUid")
)
private fun maintenanceFromJson(o: JSONObject): MaintenanceRecord = MaintenanceRecord(
    clientUid=o.optString("clientUid"), serviceType=o.optString("serviceType"), performedAtMs=o.optLong("performedAtMs"), odometerKm=o.optNullableDouble("odometerKm"),
    nextDueKm=o.optNullableDouble("nextDueKm"), cost=o.optNullableDouble("cost"), notes=o.optString("notes")
)


private fun tripFromCloudJson(o: JSONObject): TripRecord = TripRecord(
    clientUid = o.optString("client_uid"),
    origin = o.optString("origin"),
    destination = o.optString("destination"),
    startedAtMs = o.optIsoMs("started_at"),
    endedAtMs = o.optNullableIsoMs("ended_at"),
    startOdometerKm = o.optNullableDouble("start_odometer_km"),
    endOdometerKm = o.optNullableDouble("end_odometer_km"),
    startFuelCounterL = o.optNullableDouble("start_core_fuel_liters") ?: 0.0,
    endFuelCounterL = o.optNullableDouble("end_core_fuel_liters"),
    weightT = o.optNullableDouble("weight_t"),
    ratePerT = o.optNullableDouble("rate_per_t"),
    freightTotal = o.optNullableDouble("freight_total"),
    distanceKm = o.optNullableDouble("distance_km"),
    fuelLiters = o.optNullableDouble("fuel_liters"),
    averageKml = o.optNullableDouble("average_kml"),
    status = o.optString("status", "ACTIVE"),
)

private fun fuelingFromCloudJson(o: JSONObject): FuelingRecord = FuelingRecord(
    clientUid = o.optString("client_uid"),
    tsMs = o.optIsoMs("ts"),
    liters = o.optDouble("liters"),
    pricePerL = o.optDouble("price_per_l"),
    total = o.optDouble("total"),
    station = o.optString("station"),
    location = o.optString("location"),
    fullTank = o.optBoolean("full_tank"),
    odometerKm = o.optNullableDouble("odometer_km"),
)

private fun expenseFromCloudJson(o: JSONObject): ExpenseRecord = ExpenseRecord(
    clientUid = o.optString("client_uid"),
    tsMs = o.optIsoMs("ts"),
    category = o.optString("category", "Outros"),
    description = o.optString("description"),
    amount = o.optDouble("amount"),
    tripClientUid = o.optNullableString("trip_client_uid"),
)

private fun maintenanceFromCloudJson(o: JSONObject): MaintenanceRecord = MaintenanceRecord(
    clientUid = o.optString("client_uid"),
    serviceType = o.optString("service_type"),
    performedAtMs = o.optIsoMs("performed_at"),
    odometerKm = o.optNullableDouble("odometer_km"),
    nextDueKm = o.optNullableDouble("next_due_km"),
    cost = o.optNullableDouble("cost"),
    notes = o.optString("notes"),
)

private fun JSONObject.optIsoMs(key: String): Long =
    optNullableIsoMs(key) ?: System.currentTimeMillis()

private fun JSONObject.optNullableIsoMs(key: String): Long? {
    val value = optNullableString(key) ?: return null
    return runCatching { java.time.Instant.parse(value).toEpochMilli() }.getOrNull()
}

private fun <T> mergeByUid(cloud: List<T>, local: List<T>, uid: (T) -> String): List<T> {
    val cloudIds = cloud.map(uid).toHashSet()
    return cloud + local.filter { uid(it) !in cloudIds }
}

private fun <T> JSONArray?.toList(mapper: (JSONObject) -> T): List<T> {
    if (this == null) return emptyList()
    return buildList { for (i in 0 until length()) optJSONObject(i)?.let { add(mapper(it)) } }
}
private fun JSONObject.optNullableString(key: String): String? = if (isNull(key) || !has(key)) null else optString(key).takeIf { it.isNotBlank() }
private fun JSONObject.optNullableLong(key: String): Long? = if (isNull(key) || !has(key)) null else optLong(key)
private fun JSONObject.optNullableDouble(key: String): Double? = if (isNull(key) || !has(key)) null else optDouble(key).takeIf { it.isFinite() }
