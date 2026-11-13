import type {Scope, Policy, Ports, Session} from './contracts.ts';

const sameScope = (a: Scope,b: Scope) => ['tenantId','vehicleId','multimediaDeviceId','userId','sessionId'].every(k=>a[k as keyof Scope]===b[k as keyof Scope]);
const reply = (status: number, body: unknown) => new Response(JSON.stringify(body),{status,headers:{'content-type':'application/json','cache-control':'no-store','vary':'Authorization'}});
const valid = (p: Policy | null,s: Session,now: Date): p is Policy => !!p && sameScope(p.scope,s.scope) && typeof p.financialRead==='boolean' && typeof p.journeyRead==='boolean' && Number.isSafeInteger(p.version) && p.version>0 && Date.parse(p.issuedAt)<=now.getTime() && now.getTime()<Date.parse(p.expiresAt);
const amount = (n: number) => Number.isSafeInteger(n) && n>=0;

/** Biblioteca de handler revisável. Não há Deno.serve, deploy ou endpoint em produção.
 * Reautoriza depois do fetch para descartar respostas de uma versão revogada no meio da leitura. */
export function createHandler(ports: Ports): (req: Request)=>Promise<Response> {
  return async req => {
    try {
      if(req.method!=='GET') return reply(405,{error:'method_not_allowed'});
      const url=new URL(req.url);
      if(!['/v1/policy','/v1/financial','/v1/journey','/v1/consumption'].includes(url.pathname)) return reply(404,{error:'unknown_contract'});
      const session=await ports.authenticate(req);
      if(!session) return reply(401,{error:'authentication_required'});
      // IDs livres nunca ampliam escopo autenticado.
      const requested=url.searchParams.get('vehicle_id');
      if(requested && requested!==session.scope.vehicleId) return reply(403,{error:'scope_denied'});
      if(!session.monitoringRead) return reply(403,{error:'monitoring_denied'});
      if(url.pathname==='/v1/consumption') {
        const source=await ports.consumption(session.scope);
        if(!source) return reply(503,{error:'data_unavailable'});
        if(!sameScope(source.scope,session.scope)) return reply(403,{error:'scope_denied'});
        // O backend calcula por etapa; não recebe litros/km arbitrários do Android.
        if(source.legs.some(x=>!Number.isFinite(x.distanceKm)||!Number.isFinite(x.consumedLiters)||x.distanceKm<0||x.consumedLiters<0||!['LOADED','EMPTY'].includes(x.kind))) return reply(503,{error:'invalid_consumption'});
        if(new Set(source.legs.map(x=>x.legId)).size!==source.legs.length) return reply(503,{error:'duplicate_leg'});
        // Verificar sessão novamente cobre revogação de monitoramento durante a consulta.
        const latestSession=await ports.authenticate(req);
        if(!latestSession || !latestSession.monitoringRead || !sameScope(latestSession.scope,session.scope)) return reply(403,{error:'session_revoked'});
        return reply(200,{contractVersion:1,legs:source.legs.map(x=>({legId:x.legId,kind:x.kind,distanceKm:x.distanceKm,consumedLiters:x.consumedLiters,averageKml:x.consumedLiters>0?x.distanceKm/x.consumedLiters:null}))});
      }
      const policy=await ports.policy(session.scope);
      if(!valid(policy,session,ports.now())) return reply(403,{error:'policy_unavailable_or_expired'});
      if(url.pathname==='/v1/policy') return reply(200,{contractVersion:1,scope:policy.scope,version:policy.version,issuedAt:policy.issuedAt,expiresAt:policy.expiresAt,financialRead:policy.financialRead,journeyRead:policy.journeyRead});
      const financial=url.pathname==='/v1/financial';
      if(!(financial?policy.financialRead:policy.journeyRead)) return reply(403,{error:'module_denied'});
      if(req.headers.get('x-policy-version')!==String(policy.version)) return reply(409,{error:'policy_version_changed'});
      // Crucial: nenhuma leitura protegida acontece antes da autorização.
      const source=financial?await ports.financial(session.scope):await ports.journey(session.scope);
      if(!source) return reply(503,{error:'data_unavailable'});
      if(!sameScope(source.scope,session.scope)) return reply(403,{error:'scope_denied'});
      const sessionAgain=await ports.authenticate(req);
      const after=await ports.policy(session.scope);
      if(!sessionAgain || !sameScope(sessionAgain.scope,session.scope) || !sessionAgain.monitoringRead || !valid(after,session,ports.now()) || after.version!==policy.version || !(financial?after.financialRead:after.journeyRead)) return reply(403,{error:'policy_revoked'});
      if(financial && 'contractedCents' in source) {
        if(!(source.contractedCents===null||amount(source.contractedCents))||!amount(source.incurredCents)||!amount(source.projectedCents)) return reply(503,{error:'invalid_financial'});
        if(!['LOADED','EMPTY'].includes(source.legKind)) return reply(503,{error:'invalid_operation'});
        const financialView=source.legKind==='EMPTY'?{operationKind:'EMPTY',incurredCents:source.incurredCents,projectedCents:source.projectedCents}:{operationKind:'LOADED',contractedCents:source.contractedCents,incurredCents:source.incurredCents,projectedCents:source.projectedCents};
        return reply(200,{contractVersion:1,policyVersion:after.version,financial:financialView});
      }
      if('drivingSeconds' in source) {
        const values=[source.drivingSeconds,source.restConfirmedSeconds,source.waitingSeconds,source.otherStopSeconds,source.unknownSeconds];
        if(values.some(n=>!amount(n))||values.reduce((a,b)=>a+b,0)!==86400||!Number.isFinite(Date.parse(source.reference))) return reply(503,{error:'invalid_journey_window'});
        return reply(200,{contractVersion:1,policyVersion:after.version,journey:{reference:source.reference,drivingSeconds:source.drivingSeconds,restConfirmedSeconds:source.restConfirmedSeconds,waitingSeconds:source.waitingSeconds,otherStopSeconds:source.otherStopSeconds,unknownSeconds:source.unknownSeconds,stoppedSubtotalSeconds:source.restConfirmedSeconds+source.waitingSeconds+source.otherStopSeconds}});
      }
      return reply(503,{error:'invalid_source'});
    } catch {
      return reply(503,{error:'service_unavailable'}); // Sem payload de erro/segredo em logs/resposta.
    }
  };
}
