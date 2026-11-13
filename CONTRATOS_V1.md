# Contratos v1 propostos — não implantados

Estado: desenho revisável e biblioteca de autorização executável em `backend-proposal/v1`. **Não há endpoint v1 implantado, adaptador de banco, login ou provisionamento no APK.** Nenhuma migração ou função existente foi alterada. Os dados de teste estão somente nos testes e no flavor demo.

## Autenticação e isolamento

Criar identidade de multimídia separada das credenciais de ingestão Core/GPS e de dashboard. Pareamento de proprietário autenticado vincula tenant, veículo, dispositivo e usuário/sessão. Proposta: chave privada Android Keystore e desafio assinado, registro da chave pública no backend, token de curta duração atrelado à sessão e prova de posse; validar compatibilidade do hardware. O proprietário autentica pelo Supabase Auth e membership de tenant comprovada no servidor. Não usar user_metadata nem IDs livres do Android como autorização.

Toda leitura resolve a identidade no servidor, verifica vínculo ativo e versão atual. Revogação no banco deve valer a cada requisição, incluindo sessão encerrada e caminhos alternativos antigos. Claims em JWT sozinhas podem ficar antigas. Proteção só da nova API não corrige o fallback público dos endpoints/RPCs antigos. A remediação desses caminhos é pré-condição de aceite integrado.

Contrato de política: `scope {tenantId,vehicleId,multimediaDeviceId,userId,sessionId}`, `version`, `issuedAt`, `expiresAt`, `financialRead`, `journeyRead`. UTC ISO8601; versões estritamente monotônicas por escopo; mesmo número não pode alterar conteúdo ou prorrogar validade. Sem política verificável, negar módulos. Política/dados de escopo anterior nunca migram para outro motorista/veículo/aparelho.

## Recursos v1

| Recurso proposto | Conteúdo / proteção | Implementação neste incremento |
|---|---|---|
| GET `/v1/policy` | Política efetiva derivada da identidade, sem valores protegidos | Handler de referência testado localmente. |
| GET `/v1/consumption` | Etapas LOADED/EMPTY distintas; média calculada server-side, sem moeda/jornada | Handler de referência testado; repositório real pendente. |
| GET `/v1/financial` | Receita contratada em centavos, custo incorrido e total projetado; exige financialRead e versão | Handler explícito, projeção de campos e revalidação após consulta; sem adaptador real. |
| GET `/v1/journey` | Janela móvel de 24h, categorias exclusivas; parado é subtotal; exige journeyRead | Handler valida total; classificação real e segmentos pendentes. |
| GET `/v1/monitoring` | Sinais técnicos de veículo autorizado, por amostra, sem objetos trip SQL inteiros | Contrato pendente de captura Core/Cloud e catálogo validado. |
| POST `/v1/policy/ack` | Versão aplicada, escopo autenticado, horário servidor; idempotente | Proposto; **não implementado**. Não apresentar aplicado sem ACK. |
| GET `/v1/faults` | Episódios ECU e anomalias separadas, origem/horários confirmados | Captura e API pendentes; UI indisponível. |
| GET `/v1/tolls` | Passagens técnicas sem valores; detalhes financeiros em recurso protegido | Fornecedor, geometria, tarifas e API pendentes. |
| GET `/v1/insights` | Evidências, cobertura, período, modelo/versão e campos classificados por permissão | Modelos existentes não bastam para autorizar conteúdo; integração pendente. |
| PUT `/v1/dashboard-layout` | 13 posições/tamanhos fixos; vínculo usuário/veículo e autorização | Pendente de sessão e persistência Cloud; UI bloqueia edição. |

Erros tipados, sem payloads de exceção: 401 identidade ausente, 403 escopo/módulo revogado ou expirado, 409 versão divergente, 503 fonte indisponível. Respostas `Cache-Control: no-store`; zero cache protegido persistente no Android. Cliente descarta respostas tardias de outro escopo/versão. Falha de rede remove dados protegidos; telemetria técnica segue conforme sua autorização. Contratos internos TypeScript e Kotlin estão tipados; a ligação HTTP Cloud Android permanece ausente enquanto não houver serviço seguro real.

## Telemetria e consumo

Amostra: sensor_id, valor/unidade, source (barramento/ECU/decodificação), quality, sampled_at, ingested_at, vehicle_id, trip_id/leg_id quando aplicável, epoch/seq para identidade. Sinais categóricos sem média; duas séries para ar, um cartão Home. GET /api/state legado pode responder sem esses metadados: o adaptador marca UNVERIFIED e não o promove a live pelo horário HTTP. Não haverá fallback nos objetos history/raw. Mapear catálogo J1939/J1708 por captura autorizada antes de apresentar leituras atuais.

Ledger tanque autoritativo: evento BOTH_TANKS_FULL explicitamente confirmado no Motorista, capacidade por veículo, epoch/seq e contador na referência. PARTIAL adiciona litros sem reset. COUNTER debita delta uma vez incluindo lenta; não integrar vazão e contador sobre o mesmo intervalo. GAP/reset/rollover não reconciliado torna estimativa desatualizada. A boia não é campo do cálculo. Saldo e autonomia não devem aparecer como exatos se qualidade não for válida. IDs duplicados com conteúdo diferente viram conflito. Reenvios/lotes atrasados reconstroem cronologicamente sob lock transacional por veículo; preservar trilha, versões e correções.

`domain/TankLedger.kt` é referência pura testada, **não ledger persistido na Cloud**. Reiniciar a função com os mesmos eventos prova determinismo, não persistência de Core/SD/Gateway. O APK homolog não grava nem assume saldo legado; mostra “Calibre em tanque cheio”. Aceite T18 requer armazenamento/replay real.

## DTC, pedágios, vazio e jornada

Episódio DTC: fault_id, episode_id, ECU/protocolo, SPN/FMI ou MID/PID/SID reais, activated_at, last_seen_active_at, deactivated_at só por confirmação, source_timestamp/ingestion_timestamp, qualidade e recorrências. Sem heartbeat/snapshot válido: estado atual UNKNOWN, último confirmado preservado. Recorrência cria episódio novo. Nunca converter ausência de frames em inativo.

Passagem de pedágio: passage_key estável por veículo/praça/sentido/etapa/janela, revisões idempotentes; baixa precisão não comprova travessia. FORECAST→IDENTIFIED substitui custo da mesma passagem; CONFIRMED exige evidência de cobrança/valor. OWNER reduz sobra uma vez; CARRIER não reduz; REIMBURSABLE separa desembolso/crédito. Preço desconhecido não vira zero confiável. O repositório/API precisará tratar revisões contraditórias e auditoria de correções.

Vazio é etapa independente. O SQL existente `truckbox_ai.reconcile_empty_leg_v1` escreve empty_before_* automaticamente; isso **não equivale a confirmação do vínculo financeiro**. Não usar esses campos para alocar custos na nova multimídia. Criar confirmação explícita no Motorista antes de relacionar etapa e frete.

Jornada: intervalos exclusivos direção, descanso confirmado, espera/trabalho, outras paradas e desconhecido somam 24h. Parado é subtotal; lacunas e sobreposição contraditória ficam desconhecidas. GPS parado nunca é descanso confirmado. Sem revisão legal não exibir conformidade, próximo marco obrigatório ou alertas jurídicos. Modelo real de edição/confirmação será do Motorista.

## Migrações aditivas propostas e reversão

Não foram criados arquivos em uma pasta de migração automática. Depois da revisão, gerar cada migration pelo CLI Supabase e testar em staging isolado. Validar schema/grants reais antes de escrever DDL definitivo.

| Ordem | Adição proposta | Reversão segura |
|---|---|---|
| M1 | Schema privado `multimedia_v1`; memberships/vehicle_bindings/device_public_keys e sessions; FKs para trucks sem alterar IDs antigos | Desativar novos vínculos/sessões e rotas v1; preservar auditoria. |
| M2 | module_policies, policy_audit, device_policy_acks; UNIQUE escopo+version; expiração e revogação explícitas | Negar módulos e desativar novas leituras; manter registros/ACK. |
| M3 | fuel_events/fuel_balances; UNIQUE vehicle+event_id e contador epoch/seq; both_tanks_confirmed nullable nos novos registros; quality/gaps | Suspender reconciliador; conservar ledger/exportação. Não mapear full_tank legado para ambos cheios. |
| M4 | fault_episodes/fault_observations, chaves source/event, estado UNKNOWN e transições confirmadas | Desativar ingestão nova e endpoints; não apagar episódios. |
| M5 | toll_plazas/providers/tariff_versions/passages e atribuições por responsável; confirmed_empty_links | Desativar detecção; não apagar despesas/auditoria, não tocar operações antigas. |
| M6 | journey_segments/confirmations/corrections; intervalos e fonte/classificação | Desabilitar módulo; manter registros auditáveis conforme retenção legal. |
| M7 | insight_runs/evidence, dashboard_layouts de 13 slots por usuário/veículo | Desabilitar geração/layout customizado; restaurar layout padrão da UI. |

RLS em todas as tabelas novas, privilégios mínimos, sem SELECT/EXECUTE público implícito. Backend não deve usar service_role como substituto de autorização. Preferir invoker/consulta com contexto autenticado; funções privilegiadas, se necessárias, ficam privadas, com checks de escopo e grants explicitamente revogados para PUBLIC/anon. Revisar views e search_path. Migrações não fazem backfill assumindo permissões ou confirmação de tanques/vazio. Criar backup, registrar checksums e executar testes de negação entre tenants antes de promoção. Rollback operacional não exige DROP de dados. Corrigir exposição legada em PR próprio com plano de compatibilidade; não abrir RLS para fazer o APK funcionar.

## Dependências ainda não liberadas

Google Routes/Elevation já aparecem no código implantado, mas cobrança, chave habilitada, contrato/licença, cobertura e adequação a rodotrem não foram comprovados. Escolher SDK de mapa nativo, atribuição/cache/offline e fonte comercial de praças/tarifas/altimetria; nenhuma compra feita. Definir revisão de legislação de jornada, LGPD, retenção/localização e classificação por motorista. Validar hardware Rev.B.1 e Android 13” antes de qualquer declaração comercial.
