# Integração de combustível v2 — revisão pendente, sem deploy

O Core 0.2.23 calcula um único acumulado canônico (`trip_fuel_l` / `fuelLitersTest`). `trip_accounting.v=2` distingue este contrato. A integração PID183 ainda não conciliada é provisória; `direct_l` é diagnóstico dos deltas ECU, não outra parcela a somar ao acumulado.

A consulta somente leitura de 14/11/2026 encontrou `truckbox-ingest` v26 e quatro funções SQL que precisam mudar em conjunto. Hoje o ingest chama `truckbox_apply_trip_rate_tail`, pode substituir o valor do Core pelo agregado da viagem, e os triggers de calibração aplicam fator adicional. Uma atualização somente do Core não muda essas rotinas.

Preparação executada, sem alterar a Cloud:

```sh
python3 prepare_fuel_v2.py ingest-v26.ts ingest-fuel-v2-review.ts review.patch
node test_fuel_v2.mjs
```

O preparador exige o SHA256 da fonte consultada (normalização apenas do newline final), recusa outra versão e nunca altera a entrada. O diff público contém somente a política de combustível e os pontos de integração; autenticação, ACK, identidade, filtros e transporte permanecem iguais. Obtenha novamente as definições e compare antes de qualquer aplicação futura. `fuel_v2_review.sql` e `fuel_v2_rollback_review.sql` são propostas de DDL para revisão, **não foram executadas**. A sintaxe TypeScript do resultado foi analisada com o parser do Node; o SQL precisa de validação em uma base isolada antes de aprovação.

A política proposta:

- Preserva o acumulado original em `raw.core_fuel_l` e toda `trip_accounting`.
- Não reintegra PID183 nem calibra novamente linhas v2.
- Usa consumo por operação apenas quando o UID informado pelo Core corresponde a `trips.client_uid`. Sem referência correspondente, publica consumo da viagem como indisponível; conserva o acumulado do equipamento e a qualidade.
- Não soma eventos repetidos `FUEL_RECONCILIATION`: a chave usa dispositivo, contadores brutos e boots/uptimes das duas âncoras. Correções negativas são permitidas e não são resets.
- Preserva agregados financeiros/históricos existentes. Uma viagem com dados mistos fica excluída dos reconstrutores legados até uma migração explicitamente revisada; não há comando de recálculo nesta entrega. Essa decisão deixa o agregado antigo de `trips` sem atualização automática, embora as novas amostras normalizadas e o total do Core estejam disponíveis. A migração desse agregado e dos consumidores deve ser aprovada separadamente.

Formato compacto `trip_accounting.op`: `[UID da operação, litros atribuídos à operação, alocação estimada]`. A partição entre operações dentro de um intervalo ECU é estimada, mesmo quando o total do intervalo foi confirmado. Ausência de referência, evento de viagem entregue com atraso e fronteiras anteriores ao firmware novo não autorizam inferir uma captura exata.

Evento `FUEL_RECONCILIATION`, com unidade L:

- `anchor`: `[MID, raw PID250, L convertidos, boot, uptime_ms, unix_ms ou 0, odômetro validado ou null]`.
- `previous`: `[raw anterior, boot anterior, uptime_ms anterior, unix_ms anterior ou 0]`.
- `litres`: `[delta ECU do último intervalo, integração PID183 desse intervalo, correção aplicada, deltas ECU confirmados acumulados, provisório após âncora, canônico atual, distância validada do intervalo ou null, interpolação estimada do intervalo substituída pela ECU]`.
- `interval_id`: contador auxiliar persistido. A identidade completa usa as âncoras; não usar apenas esse contador, pois uma manutenção explícita pode reiniciá-lo.
- `operation_uid`, `allocation_estimated`, `boundary_correction_l`: contexto da alocação, não nova medição ECU por trecho.

As âncoras fornecem os limites temporais/quilométricos para consultas de 10/50/100 km. O total confirmado de um intervalo que atravessa um limite não confirma suas partes; qualquer distribuição interna continua estimada. Não há nova interface de usuário ou alteração financeira nesta entrega.

`unanchored_l`, quando presente, conserva o saldo anterior à primeira âncora disponível. Ele não se torna confirmado por uma leitura posterior que só delimita o próximo intervalo. `unknown_gap`/`historical_unknown` distinguem lacunas antigas; não somar esse saldo novamente ao canônico.
