package br.com.truckbox.app.operations

import java.util.UUID

enum class DriverAction { UPSERT_TRIP,END_TRIP,UPSERT_FUELING,UPSERT_EXPENSE,UPSERT_MAINTENANCE }
/** Immutable payload at the time of the edit; no credentials in the durable queue. */
data class PendingCloudCommand(
    val id: String=UUID.randomUUID().toString(),val scope: String?=null,val action: DriverAction,
    val trip: TripRecord?=null,val fueling: FuelingRecord?=null,val expense: ExpenseRecord?=null,val maintenance: MaintenanceRecord?=null,
    val awaitingTripReview: Boolean = false,
) {
    val entityUid: String get()=when(action) {
        DriverAction.UPSERT_TRIP,DriverAction.END_TRIP -> requireNotNull(trip).clientUid
        DriverAction.UPSERT_FUELING -> requireNotNull(fueling).clientUid
        DriverAction.UPSERT_EXPENSE -> requireNotNull(expense).clientUid
        DriverAction.UPSERT_MAINTENANCE -> requireNotNull(maintenance).clientUid
    }
}
