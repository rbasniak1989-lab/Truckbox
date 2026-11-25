package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.time.Instant

/** Core e Cloud têm ciclos independentes. Nenhum dado protegido é persistido por este coordenador. */
class CloudCoordinator(
    private val configured: Boolean,
    private val api: CloudApi,
    private val signerFactory: ()->DeviceSigner,
    private val tokens: RefreshTokenStore,
    private val app: MutableStateFlow<AppState>,
    private val now: ()->Instant=Instant::now,
) {
    private val lock=Mutex()
    private val identityGuard=Any()
    private var session: CloudAuthSession?=null
    private var sessionEpoch=-1L
    private var signer: DeviceSigner?=null
    private var registeredSession: String?=null
    private var scope: IdentityScope?=null
    private var policy: PolicySession?=null
    private var acknowledged: Pair<IdentityScope,Long>?=null
    private val mutableUi=MutableStateFlow(CloudUiState(configured=configured,message=if(configured) "Entre para solicitar o vínculo na Cloud" else "Servidor Cloud ainda não configurado nesta compilação"))
    val ui: StateFlow<CloudUiState> = mutableUi

    suspend fun activateTest(code: String) {
        if(!configured)return
        val epoch=synchronized(identityGuard){tokens.invalidate().also {clearViews(ConnectionState.AUTH_REQUIRED);mutableUi.update {it.copy(authenticated=false,busy=true,message="Ativando vínculo temporário na Cloud")}}}
        withContext(Dispatchers.IO) {lock.withLock {
            try {
                val device=signer ?: signerFactory().also {signer=it}
                val auth=api.activateTest(code.trim(),device,requireNotNull(app.value.coreDeviceUid){"Core identity required"})
                currentCoroutineContext().ensureActive()
                if(!tokens.writeIfCurrent(epoch,auth.refreshToken))return@withLock
                session=auth;sessionEpoch=epoch;registeredSession=null
                pull(epoch,auth)
            } catch(e: CancellationException){throw e}catch(e: Exception){failed(epoch,e)}
        }}
    }

    suspend fun signIn(email: String,password: String) {
        if(!configured)return
        val epoch=synchronized(identityGuard) {
            tokens.invalidate().also {
                clearViews(ConnectionState.AUTH_REQUIRED)
                mutableUi.update {it.copy(authenticated=false,busy=true,message="Autenticando na Cloud")}
            }
        }
        withContext(Dispatchers.IO) {lock.withLock {
            try {
                val auth=api.signIn(email.trim(),password);currentCoroutineContext().ensureActive()
                if(!tokens.writeIfCurrent(epoch,auth.refreshToken))return@withLock
                if(tokens.generation()!=epoch)return@withLock
                session=auth;sessionEpoch=epoch;registeredSession=null
                pull(epoch,auth)
            } catch(e: CancellationException) {throw e} catch(e: Exception) {failed(epoch,e)}
        }}
    }
    suspend fun refresh() {
        if(!configured)return
        withContext(Dispatchers.IO) {lock.withLock {
            if(mutableUi.value.busy)return@withLock
            val epoch=tokens.generation()
            try {
                val device=signer ?: signerFactory().also {signer=it}
                synchronized(identityGuard) {if(tokens.generation()==epoch)mutableUi.update {it.copy(deviceId=device.deviceId)}}
                var auth=session.takeIf {sessionEpoch==epoch}
                if(auth==null || !auth.expiresAt.isAfter(now().plusSeconds(30))) {
                    val refresh=auth?.refreshToken ?: tokens.read()?.takeIf {it.generation==epoch}?.token
                    if(refresh==null) {
                        synchronized(identityGuard) {if(tokens.generation()==epoch){clearViews(ConnectionState.AUTH_REQUIRED);mutableUi.update {it.copy(authenticated=false,busy=false,message="Entre com seu acesso do TruckBox")}}}
                        return@withLock
                    }
                    auth=if(isFieldTestToken(refresh))api.activateTest(refresh,device,requireNotNull(app.value.coreDeviceUid)) else api.refresh(refresh)
                    currentCoroutineContext().ensureActive()
                    if(!tokens.writeIfCurrent(epoch,auth.refreshToken))return@withLock
                    session=auth;sessionEpoch=epoch;registeredSession=null
                }
                pull(epoch,auth)
            } catch(e: CancellationException) {throw e} catch(e: Exception) {failed(epoch,e)}
        }}
    }
    suspend fun signOut() {
        val epoch=synchronized(identityGuard) {
            tokens.invalidate().also {
                clearViews(ConnectionState.AUTH_REQUIRED)
                mutableUi.update {it.copy(authenticated=false,busy=false,message="Sessão local encerrada")}
            }
        }
        withContext(Dispatchers.IO) {lock.withLock {
            val old=session;session=null;sessionEpoch=-1;registeredSession=null;scope=null;policy=null;acknowledged=null
            try {if(old!=null)api.signOut(old)} catch(e: CancellationException){throw e} catch(_: Exception) {
                synchronized(identityGuard){if(tokens.generation()==epoch)mutableUi.update {it.copy(message="Sessão local encerrada; revogação remota não confirmada")}}
            }
        }}
    }
    fun disconnectViews()=synchronized(identityGuard) {clearViews(ConnectionState.UNVERIFIED)}
    fun reportUnavailable()=synchronized(identityGuard) {
        clearViews(ConnectionState.UNVERIFIED)
        mutableUi.update {it.copy(authenticated=false,busy=false,message="Acesso seguro à Cloud indisponível neste aparelho")}
    }

    private suspend fun pull(epoch: Long,auth: CloudAuthSession) {
        currentCoroutineContext().ensureActive()
        val device=signer ?: signerFactory().also {signer=it}
        if(registeredSession!=auth.sessionId) {
            api.enroll(auth,device);currentCoroutineContext().ensureActive()
            registeredSession=auth.sessionId
        }
        val dto=api.snapshot(auth,device);currentCoroutineContext().ensureActive()
        require(dto.contractVersion==1)
        val identity=requireNotNull(dto.scope).identity()
        require(identity.user==auth.userId && identity.session==auth.sessionId && identity.device==device.deviceId)
        require(dto.vehicleLabel==null || dto.vehicleLabel.length<=120)
        require(dto.coreDeviceUid!=null && Regex("[A-Za-z0-9_-]{1,128}").matches(dto.coreDeviceUid))
        val received=Instant.parse(requireNotNull(dto.receivedAt))
        require(kotlin.math.abs(java.time.Duration.between(received,now()).seconds)<=60)
        val serverLease=java.time.Duration.between(received,Instant.parse(requireNotNull(dto.leaseExpiresAt))).toMillis()
        require(serverLease in 1..20000 && dto.roundTripMillis in 0 until serverLease)
        val leaseDeadline=now().plusMillis(serverLease-dto.roundTripMillis)
        if(scope!=identity) {scope=identity;policy=PolicySession(identity);acknowledged=null}
        val remote=dto.policy?.domain()
        val applied=remote!=null && policy!!.applyFromCloud(remote,now())
        if(!applied)policy!!.disconnect()
        synchronized(identityGuard) {
            if(tokens.generation()!=epoch)return
            require(leaseDeadline.isAfter(now()))
            // Remover imediatamente o conteúdo de escopo/versão anterior, antes de aguardar ACK de rede.
            app.update {previous ->
                val flags=policy!!.allowed(now())
                val same=previous.cloudScope==identity && previous.policyVersion==remote?.version
                previous.copy(financial=previous.financial.takeIf {same && flags.first},journey=previous.journey.takeIf {same && flags.second},
                    financialAllowed=previous.financialAllowed && same && flags.first,journeyAllowed=previous.journeyAllowed && same && flags.second)
            }
        }
        val trip=dto.trip?.domain()
        val gps=dto.gps?.domain(now())
        val fuelRange = runCatching { dto.fuelRange?.domain(received) }.getOrNull()
        val fuelHistory = trip?.let { runCatching { dto.fuelHistory?.domain(it.id, received) }.getOrNull() }
        val route = trip?.takeIf { it.kind == LegKind.LOADED }
            ?.let { runCatching { dto.route?.domain(it.id, received) }.getOrNull() }
        val routeForecast = route?.let { runCatching { dto.routeForecast?.domain(it, received) }.getOrNull() }
        val operationalDeadline = if (applied) minOf(auth.expiresAt, requireNotNull(remote).expiresAt) else leaseDeadline
        val legs=(dto.legs ?: emptyList()).map {it.domain()}
        require(legs.map {it.legId}.distinct().size==legs.size)
        require(legs.isEmpty() || (trip!=null && legs.all {it.legId==trip.legId && it.kind==trip.kind}))
        val allowed=policy!!.allowed(now())
        val financial=if(allowed.first)dto.financial?.domain() else null
        val journey=if(allowed.second)dto.journey?.domain() else null
        if(financial!=null)require(trip!=null && dto.financial?.operationKind==trip.kind.name)
        if(applied) {
            policy!!.acceptResponse(identity,remote!!.version,ProtectedViews(financial,journey),now())
            if(acknowledged!=(identity to remote.version)) {
                // A política já foi aplicada/limpa no guard local; confirmação remota só após resposta real.
                api.acknowledge(auth,device,remote.version);currentCoroutineContext().ensureActive()
                acknowledged=identity to remote.version
            }
        }
        synchronized(identityGuard) {
            if(tokens.generation()!=epoch)return
            val flags=policy!!.allowed(now());val views=policy!!.snapshot(now())
            require(leaseDeadline.isAfter(now()))
            app.update {previous ->
                val changed=previous.expectedCoreUid!=dto.coreDeviceUid
                previous.copy(cloud=ConnectionState.CLOUD_AUTHENTICATED,trip=trip,legs=legs,gps=gps,
                    fuelRange=fuelRange, fuelHistory=fuelHistory, route=route, routeForecast=routeForecast?.withFinancialAccess(flags.first),
                    operationalExpiresAt=operationalDeadline,
                    financial=views.financial,journey=views.journey,financialAllowed=flags.first,journeyAllowed=flags.second,
                    policyVersion=remote?.version.takeIf {applied},policyExpiresAt=remote?.expiresAt?.let {minOf(it,leaseDeadline)}.takeIf {applied},
                    policyLabel=when {
                        !applied -> "Política indisponível ou expirada"
                        flags.first && flags.second -> "Financeiro e Jornada liberados pela Cloud"
                        flags.first -> "Financeiro liberado pela Cloud"
                        flags.second -> "Jornada liberada pela Cloud"
                        else -> "Módulos opcionais não liberados pela Cloud"
                    },cloudScope=identity,
                    vehicleLabel=dto.vehicleLabel,expectedCoreUid=dto.coreDeviceUid,lastCloudResponse=received,
                    readings=if(changed) emptyMap() else previous.readings,localInstruments=if(changed) emptyMap() else previous.localInstruments,
                    coreDeviceUid=if(changed) null else previous.coreDeviceUid,coreInternetSharing=if(changed) null else previous.coreInternetSharing,
                    speed=if(changed) null else previous.speed,fuelRate=if(changed) null else previous.fuelRate)
            }
            mutableUi.update {it.copy(configured=true,authenticated=true,busy=false,deviceId=device.deviceId,message="Vínculo consultado na Cloud")}
        }
    }
    private fun failed(epoch: Long,error: Exception)=synchronized(identityGuard) {
        if(tokens.generation()!=epoch)return@synchronized
        val status=when {error is CloudHttpError && error.status==403->ConnectionState.AUTH_PENDING
            error is CloudHttpError && error.status in listOf(400,401)->ConnectionState.AUTH_REQUIRED
            error is CloudHttpError && error.status==404->ConnectionState.BLOCKED
            error is CloudHttpError->ConnectionState.UNVERIFIED
            error is java.io.IOException->ConnectionState.OFFLINE
            else->ConnectionState.UNVERIFIED}
        clearViews(status, keepOperational = status == ConnectionState.OFFLINE)
        if(error is CloudHttpError && error.status in listOf(400,401)) {
            val credential = session?.takeIf { sessionEpoch == epoch }?.refreshToken
                ?: tokens.read()?.takeIf { it.generation == epoch }?.token
            // Erros de prova ou relógio encerram as visualizações, preservando o código de campo cifrado.
            if (credential?.let(::isFieldTestToken) != true) tokens.invalidate()
            session=null;sessionEpoch=-1;registeredSession=null
        }
        mutableUi.update {it.copy(authenticated=session!=null,busy=false,deviceId=signer?.deviceId,message=when {
            error is CloudHttpError && error.status==404->"Canal Cloud aguardando publicação neste ambiente"
            error is CloudHttpError && error.status==403->"Aguardando aprovação do usuário, veículo e multimídia na Cloud"
            error is CloudHttpError && error.status in listOf(400,401)->"Autenticação recusada. Confira o acesso e a data/hora do aparelho"
            error is java.io.IOException->"Cloud sem resposta · dados operacionais com horário da última consulta"
            else->"Não foi possível validar a resposta ou a identidade da Cloud"
        })}
    }
    private fun clearViews(status: ConnectionState, keepOperational: Boolean = false) {
        policy?.disconnect()
        app.update {it.copy(cloud=status,trip=null,gps=null,legs=emptyList(),financial=null,journey=null,financialAllowed=false,journeyAllowed=false,
            fuelRange=it.fuelRange.takeIf { keepOperational }, fuelHistory=it.fuelHistory.takeIf { keepOperational },
            route=it.route.takeIf { keepOperational }, routeForecast=it.routeForecast.takeIf { keepOperational }?.withFinancialAccess(false),
            operationalExpiresAt=it.operationalExpiresAt.takeIf { keepOperational },
            policyVersion=null,policyExpiresAt=null,policyLabel="Aguardando sincronização segura",cloudScope=null,vehicleLabel=null,expectedCoreUid=null)}
    }
}
