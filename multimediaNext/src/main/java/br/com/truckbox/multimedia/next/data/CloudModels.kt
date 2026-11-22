package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.annotations.SerializedName
import java.time.Instant

data class CloudConfig(val url: String,val publishableKey: String) {
    val configured: Boolean get()=url.isNotEmpty() && publishableKey.startsWith("sb_publishable_")
}
data class CloudAuthSession(val accessToken: String,val refreshToken: String,val userId: String,val sessionId: String,val expiresAt: Instant)
data class AuthUserDto(val id: String?=null)
data class AuthTokenDto(
    @SerializedName("access_token") val accessToken: String?=null,
    @SerializedName("refresh_token") val refreshToken: String?=null,
    val user: AuthUserDto?=null,
)
data class AuthClaimsDto(val sub: String?=null,@SerializedName("session_id") val sessionId: String?=null,val exp: Long?=null)
data class CloudScopeDto(val tenantId: String?=null,val vehicleId: String?=null,val multimediaDeviceId: String?=null,val userId: String?=null,val sessionId: String?=null) {
    fun identity(): IdentityScope {
        val fields=listOf(tenantId,vehicleId,multimediaDeviceId,userId,sessionId)
        require(fields.all {it!=null && UUID_PATTERN.matches(it)})
        return IdentityScope(tenantId!!,vehicleId!!,multimediaDeviceId!!,userId!!,sessionId!!)
    }
}
internal val UUID_PATTERN=Regex("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")
data class CloudPolicyDto(val scope: CloudScopeDto?=null,val version: Long?=null,val issuedAt: String?=null,val expiresAt: String?=null,val financialRead: Boolean?=null,val journeyRead: Boolean?=null) {
    fun domain()=CloudPolicy(requireNotNull(scope).identity(),requireNotNull(version),Instant.parse(requireNotNull(issuedAt)),Instant.parse(requireNotNull(expiresAt)),requireNotNull(financialRead),requireNotNull(journeyRead))
}
data class CloudFuelDto(val quality: FuelQuality?=null,val sampledAt: String?=null)
data class CloudLegDto(val legId: String?=null,val kind: String?=null,val distanceKm: Double?=null,val consumedLiters: Double?=null,val averageKml: Double?=null,
                       val fuel: CloudFuelDto?=null) {
    fun domain(): LegConsumption {
        require(!legId.isNullOrBlank() && kind in setOf("LOADED","EMPTY"))
        val km=requireNotNull(distanceKm);val liters=requireNotNull(consumedLiters)
        require(km.isFinite() && liters.isFinite() && km>=0 && liters>=0)
        if(liters>0) require(averageKml?.isFinite()==true && kotlin.math.abs(averageKml-km/liters)<.000001)
        else require(averageKml==null)
        val measurement=fuel?.let{FuelMeasurement(it.quality ?: FuelQuality.INSUFFICIENT,it.sampledAt?.let(Instant::parse))}
        return LegConsumption(legId,LegKind.valueOf(kind!!),km,liters,measurement)
    }
}
data class CloudTripDto(val id: String?=null,val legId: String?=null,val kind: String?=null,val origin: String?=null,val destination: String?=null,val weightT: Double?=null,val legStartedAt: String?=null,val consumption: CloudLegDto?=null) {
    fun domain(): TripView {
        require(!id.isNullOrBlank() && !legId.isNullOrBlank() && kind in setOf("LOADED","EMPTY"))
        require(listOf(origin,destination).all {it==null || it.length<=250})
        require(weightT==null || (weightT.isFinite() && weightT>=0))
        val c=consumption?.domain();require(c==null || (c.legId==legId && c.kind.name==kind))
        return TripView(id,legId,origin ?: "Origem não informada",destination ?: "Destino não informado",LegKind.valueOf(kind!!),c,
            weightT=weightT,legStartedAt=legStartedAt?.let(Instant::parse))
    }
}
data class CloudFinancialDto(val operationKind: String?=null,val contractedCents: Long?=null,val incurredCents: Long?=null,val projectedCents: Long?=null) {
    fun domain(): FinancialView {
        require(operationKind in setOf("LOADED","EMPTY"))
        require(listOfNotNull(contractedCents,incurredCents,projectedCents).all {it in 0..9_007_199_254_740_991L})
        require(operationKind!="EMPTY" || contractedCents==null)
        return FinancialView(contractedCents,requireNotNull(incurredCents),requireNotNull(projectedCents))
    }
}
data class CloudJourneyDto(val reference: String?=null,val drivingSeconds: Long?=null,val restConfirmedSeconds: Long?=null,val waitingSeconds: Long?=null,val otherStopSeconds: Long?=null,val unknownSeconds: Long?=null,val stoppedSubtotalSeconds: Long?=null) {
    fun domain(): JourneyView {
        val values=listOf(drivingSeconds,restConfirmedSeconds,waitingSeconds,otherStopSeconds,unknownSeconds).map {requireNotNull(it)}
        require(values.all {it in 0..86400} && values.sum()==86400L)
        require(stoppedSubtotalSeconds==values[1]+values[2]+values[3])
        return JourneyView(Instant.parse(requireNotNull(reference)),JourneySummary(values[0],values[1],values[2],values[3],values[4]))
    }
}
data class CloudGpsDto(val latitude: Double?=null,val longitude: Double?=null,val sampledAt: String?=null,val source: String?=null,val provider: String?=null,val accuracyM: Double?=null,val verification: String?=null) {
    fun domain(now: Instant): CloudPosition {
        val lat=requireNotNull(latitude);val lon=requireNotNull(longitude);val sampled=Instant.parse(requireNotNull(sampledAt))
        require(lat.isFinite() && lon.isFinite() && lat in -90.0..90.0 && lon in -180.0..180.0)
        require(!sampled.isAfter(now.plusSeconds(60)) && !source.isNullOrBlank() && source.length<=80 && (provider==null || provider.length<=80))
        require(accuracyM==null || (accuracyM.isFinite() && accuracyM>=0))
        require(verification=="RECEIVED_NOT_PHYSICALLY_VERIFIED")
        return CloudPosition(lat,lon,sampled,source,provider,accuracyM)
    }
}
data class CloudSnapshotDto(val contractVersion: Int?=null,val scope: CloudScopeDto?=null,val policy: CloudPolicyDto?=null,val requestNonce: String?=null,
    val vehicleLabel: String?=null,val coreDeviceUid: String?=null,val trip: CloudTripDto?=null,val legs: List<CloudLegDto>?=null,
    val financial: CloudFinancialDto?=null,val journey: CloudJourneyDto?=null,val gps: CloudGpsDto?=null,val receivedAt: String?=null,val leaseExpiresAt: String?=null,
    @Transient val roundTripMillis: Long=0)

interface CloudApi {
    fun activateTest(code: String,signer: DeviceSigner,coreUid: String): CloudAuthSession = throw UnsupportedOperationException("Test access unavailable")
    fun signIn(email: String,password: String): CloudAuthSession
    fun refresh(refreshToken: String): CloudAuthSession
    fun enroll(session: CloudAuthSession,signer: DeviceSigner)
    fun snapshot(session: CloudAuthSession,signer: DeviceSigner): CloudSnapshotDto
    fun acknowledge(session: CloudAuthSession,signer: DeviceSigner,version: Long)
    fun signOut(session: CloudAuthSession)
}
