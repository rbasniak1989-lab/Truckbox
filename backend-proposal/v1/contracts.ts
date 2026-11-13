/** PROPOSTA v1. Sem conexão com produção e sem adaptador de banco instalado. */
export type Scope = Readonly<{tenantId: string; vehicleId: string; multimediaDeviceId: string; userId: string; sessionId: string}>;
export type Session = Readonly<{scope: Scope; monitoringRead: boolean}>;
export type Policy = Readonly<{scope: Scope; version: number; issuedAt: string; expiresAt: string; financialRead: boolean; journeyRead: boolean}>;
export type FinancialSource = Readonly<{scope: Scope; legKind: "LOADED" | "EMPTY"; contractedCents: number | null; incurredCents: number; projectedCents: number}>;
export type JourneySource = Readonly<{scope: Scope; reference: string; drivingSeconds: number; restConfirmedSeconds: number; waitingSeconds: number; otherStopSeconds: number; unknownSeconds: number}>;
export type ConsumptionSource = Readonly<{scope: Scope; legs: ReadonlyArray<{legId: string; kind: "LOADED" | "EMPTY"; distanceKm: number; consumedLiters: number}>}>;
export interface Ports {
  // Implementação deve validar token/credencial de dispositivo e sessão no servidor.
  // Nunca construir identidade usando IDs livres de headers/query/body.
  authenticate(request: Request): Promise<Session | null>;
  policy(scope: Scope): Promise<Policy | null>;
  financial(scope: Scope): Promise<FinancialSource | null>;
  journey(scope: Scope): Promise<JourneySource | null>;
  consumption(scope: Scope): Promise<ConsumptionSource | null>;
  now(): Date;
}
