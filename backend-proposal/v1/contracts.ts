/** Contrato proposto v1. Adaptador em cloud/; migração/Edge não publicados em produção. */
export type Scope = Readonly<{tenantId: string; vehicleId: string; multimediaDeviceId: string; userId: string; sessionId: string}>;
export type Session = Readonly<{scope: Scope; monitoringRead: boolean}>;
export type Policy = Readonly<{scope: Scope; version: number; issuedAt: string; expiresAt: string; financialRead: boolean; journeyRead: boolean}>;
export type FinancialSource = Readonly<{scope: Scope; legKind: "LOADED" | "EMPTY"; contractedCents: number | null; incurredCents: number; projectedCents: number}>;
export type JourneySource = Readonly<{scope: Scope; reference: string; drivingSeconds: number; restConfirmedSeconds: number; waitingSeconds: number; otherStopSeconds: number; unknownSeconds: number}>;
export type FuelQuality = 'INTEGRATED_PID183' | 'RECONCILED_PID250' | 'ESTIMATED_GAPS' | 'INSUFFICIENT';
export type FuelMeasurement = Readonly<{quality: FuelQuality; sampledAt: string | null}>;
export type ConsumptionSource = Readonly<{scope: Scope; legs: ReadonlyArray<{legId: string; kind: "LOADED" | "EMPTY"; distanceKm: number; consumedLiters: number; fuel?: FuelMeasurement}>}>;
export type TripData = Readonly<{id: string; legId: string; kind: "LOADED" | "EMPTY"; origin: string | null; destination: string | null; weightT: number | null; legStartedAt: string | null; consumption: ConsumptionSource['legs'][number] | null}>;
export type TripSource = Readonly<{scope: Scope; trip: TripData | null}>;
export type GpsData = Readonly<{latitude: number; longitude: number; sampledAt: string; source: string; provider: string | null; accuracyM: number | null; verification: 'RECEIVED_NOT_PHYSICALLY_VERIFIED'}>;
export interface Ports {
  // Implementação deve validar token/credencial de dispositivo e sessão no servidor.
  // Nunca construir identidade usando IDs livres de headers/query/body.
  authenticate(request: Request): Promise<Session | null>;
  policy(scope: Scope): Promise<Policy | null>;
  financial(scope: Scope): Promise<FinancialSource | null>;
  journey(scope: Scope): Promise<JourneySource | null>;
  consumption(scope: Scope): Promise<ConsumptionSource | null>;
  trip?(scope: Scope): Promise<TripSource | null>;
  now(): Date;
}
