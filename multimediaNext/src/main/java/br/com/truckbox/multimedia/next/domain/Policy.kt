package br.com.truckbox.multimedia.next.domain

import java.time.Instant

data class IdentityScope(val tenant: String, val vehicle: String, val device: String, val user: String, val session: String)
data class CloudPolicy(
    val scope: IdentityScope, val version: Long, val issuedAt: Instant, val expiresAt: Instant,
    val financialRead: Boolean, val journeyRead: Boolean,
)
data class ProtectedViews(val financial: FinancialView? = null, val journey: JourneyView? = null)

/** Recebe somente resultados de transporte autenticado. Sem fonte Cloud conectada: deny.
 * Não há setter de permissão em preferências nem transporte produtivo configurado neste incremento. */
class PolicySession(private var scope: IdentityScope) {
    private var policy: CloudPolicy? = null
    private var highestVersion = 0L
    private var connected = false
    private var views = ProtectedViews()

    @Synchronized fun applyFromCloud(incoming: CloudPolicy, now: Instant): Boolean {
        if (incoming.scope != scope || incoming.version < highestVersion || incoming.version <= 0 ||
            incoming.issuedAt.isAfter(now) || !incoming.expiresAt.isAfter(now) ||
            !incoming.expiresAt.isAfter(incoming.issuedAt)) return false
        // A mesma versão não pode mudar permissões ou estender a validade.
        if (incoming.version == highestVersion && policy != incoming) return false
        views = ProtectedViews()
        policy = incoming
        highestVersion = incoming.version
        connected = true
        return true // ACK só depois de aplicar/limpar; envio do ACK depende de API futura.
    }
    @Synchronized fun changeIdentity(next: IdentityScope) {
        scope = next; policy = null; highestVersion = 0; connected = false; views = ProtectedViews()
    }
    @Synchronized fun disconnect() { connected = false; views = ProtectedViews() }
    @Synchronized fun allowed(now: Instant): Pair<Boolean, Boolean> {
        val p = policy
        if (!connected || p == null || p.scope != scope || now.isBefore(p.issuedAt) || !now.isBefore(p.expiresAt)) {
            views = ProtectedViews(); return false to false
        }
        return p.financialRead to p.journeyRead
    }
    @Synchronized fun acceptResponse(responseScope: IdentityScope, version: Long, incoming: ProtectedViews, now: Instant) {
        val (f, j) = allowed(now)
        if (responseScope != scope || policy?.version != version) return
        views = ProtectedViews(incoming.financial.takeIf { f }, incoming.journey.takeIf { j })
    }
    @Synchronized fun snapshot(now: Instant): ProtectedViews {
        val (f,j) = allowed(now)
        return ProtectedViews(views.financial.takeIf { f }, views.journey.takeIf { j })
    }
}
