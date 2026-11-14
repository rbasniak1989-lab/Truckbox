# Auditoria — TruckBox Multimídia Black & Orange

Data: 13/11/2026 UTC. Base auditada: `main` em `06a9309d104367a49c88167d8f2b70bd7c1ffd1e`.
Escopo: leitura de fontes, catálogo Postgres, Edge Functions implantadas e advisors. Nenhuma escrita na Cloud, firmware ou Motorista. Resultados físicos **NÃO VERIFICADOS**.

## Contrato lido primeiro

Pacote recebido: `TruckBox_Entrega_Completa_Codex_v1.zip`, diretório interno `TruckBox_Entrega_Codex_v1`. Lidos `ESPECIFICACAO_MESTRE.md`, `MATRIZ_DE_ACEITE.md`, `ESTADO_ATUAL_E_BLOQUEIOS.md`, as sete especificações complementares e inspecionados os seis HTML e oito PNG em REFERENCIAS. A mestre prevalece sobre navegação histórica, valores fictícios, switches demonstrativos e aprovação pendente citada em documentos antigos. Os PNG são referência visual, não evidência de integração.

SHA-256 do ZIP: `f50649d1bb49caf0780c4467f193add40963f810f76c25762393daff44b73b0d`. Os 26 hashes do manifesto conferiram. Inventário de catálogo/colunas/versões e fingerprints de fonte Cloud em `docs/multimediaNext/evidencias/cloud-catalog-2026-11-13.json`, sem registros de clientes ou credenciais.

## Repositório e execução

| Evidência na base | Conclusão |
|---|---|
| `settings.gradle.kts` | Só `:app`, `:multimedia`, `:recovery`; novo módulo ausente. |
| `app/build.gradle.kts` | Motorista `br.com.truckbox.driver`, versão 0.6.4; preservar. |
| `multimedia/build.gradle.kts` | Legado `br.com.truckbox.multimedia`, SDK 36, versão declarada 0.1.0; fonte/CI divergem. |
| `multimedia/.../MainActivity.kt:88`, `:191`, `:263` | Seis páginas, navegação inferior, Home diferente. Não reutilizar sua UI. |
| `.github/workflows/android-build.yml` | CI altera fonte legado com Python/sed antes do build; APK anunciado 0.3.0 difere do Gradle. Não executar esses patches neste trabalho. |
| `gradle/wrapper/gradle-wrapper.properties` | Gradle 9.4.1; sem script/jar wrapper na base. AGP 9.2.1 e Compose plugin 2.3.21. |
| `git ls-files '*ino' '*cpp' '*h' '*Test*' '*test*'` | Não há firmware nem suíte de testes nesta árvore. Auditoria de firmware depende de outro artefato/repositório. Nenhum AGENTS.md encontrado. |
| Ambiente inicial | Java Runtime 21 presente, sem javac/SDK/Gradle/adb/emulador e sem `/dev/kvm`. JDK completo e ferramentas oficiais foram instalados fora do repo; build e testes locais concluídos conforme TESTES_REALIZADOS.md. |

## Core, Motorista e Gateway

`CoreGatewayClient.kt`: GET `/api/state`, GET `/api/gateway/batch?limit=8`, POST `/api/gateway/ack`, POST `/api/gps`. `GatewayCloudClient.kt`: ingestão autenticada por device UID/token, GPS separado; `TruckBoxGatewayService` permanece responsável pelo fluxo. ACK Core ocorre após ACK Cloud. Multimídia nova não busca lotes, não envia ACK de telemetria, não reinicia contadores e não envia GPS.

O parser legado (`MainActivity.kt:670` e `findValue`) busca aliases recursivamente, inclusive objetos históricos; sucesso HTTP marca dados como atuais. Isso não prova timestamp/qualidade de cada sensor. O novo adaptador não deve adotar essa heurística. Não há captura autorizada de `/api/state` do hardware nesta sessão. Sem timestamp/qualidade de sinal verificáveis, mostrar **Sem leitura**; comunicação HTTP não comprova CAN vivo.

`FuelTankManager.kt` já ignora boia, mas `fullTank: Boolean` não comprova ambos tanques cheios, aceita referência manual e reajusta após contador cair. `FuelTankMath.kt` limita resultados, podendo esconder reset/lacuna. Não reutilizar como cálculo comercial autoritativo. `TruckBoxCloudClient.kt` envia `set_tank_state` e `full_tank`; snapshot retorna objetos completos com dados financeiros. Novos campos de calibração e ledger transacional são necessários. **720 L é exemplo por veículo, nunca valor inicial global**.

## Cloud realmente inspecionada

Projeto indicado pelo contrato. Leitura via MCP: `list_tables`, catálogo de schemas/colunas/funções, `list_migrations`, `list_edge_functions`, `get_edge_function`, `get_advisors(type=security)`. Foram consultados metadados e cobertura agregada; não exportados cadastros, tokens, trajetórias ou valores financeiros.

15 tabelas públicas: trucks, devices, telemetry_samples/latest, events, dashboard_access, trips, operation_state, fuelings, expenses, maintenance, health_minute_samples, gps_samples/latest, route_simulations. Todas têm RLS habilitada, **sem policies de acesso**. Isso bloqueia clientes comuns por padrão; não protege automaticamente Edge usando service role. Migração mais recente observada: `20261112200646 truckbox_empty_efficiency_gap_aware_v1`.

Há também schema `truckbox_ai`: modelos de óleo/desempenho/rota, model_registry, feature_5m/15m, route_memory, empty_leg_auto_v1 e views live_intelligence/anomaly/oil/performance/route. Portanto **IA e modelos não estão totalmente ausentes**. Não foi comprovado resumo LLM habilitado, qualidade/treinamento adequado nem autorização por módulo. Não importar seus scores 0–100 para a nova UI.

Não foram encontradas entidades de política por multimídia/usuário/veículo, episódios DTC, passagens/tarifas de pedágio ou classificação confirmada de jornada nos schemas de negócio consultados. `dashboard_access` tem token_hash, active, truck_id e can_write, sem financial.read/journey.read.

Cobertura agregada das últimas 1.000 linhas de telemetry_samples (01:58:51–10:39:39 UTC em 13/11): óleo/água/vazão 181, turbo 180, admissão 1, câmbio 2, pedal 1; pressão combustível, marcha, torque e latitude **zero valores não nulos**. Mistura de sample_kind significa que isso não mede frequência ECU; comprova apenas cobertura parcial neste recorte. Colunas explícitas de tensão, ambiente e circuitos de ar não constam nessa tabela; podem existir em raw/decodificação, ainda não validados. Última leitura do recorte já era antiga durante a auditoria. Nenhum desses números autoriza afirmar os 13 sensores ao vivo.

## Riscos de segurança comprovados no código implantado

| Prioridade | Evidência | Efeito / ação proposta |
|---|---|---|
| P0 | `truckbox-dashboard-data` v18 linhas 90,151–157,307–325: fallback PUBLIC_TRUCK_ID sem chave; respostas com trips/expenses/maintenance | Sem segregação Financeiro/Jornada. Não consumir no novo APK. Preparar correção separada e revisada. |
| P0 | `truckbox-owner-v2` v6 linhas 60–74: fallback público, trips inclui freight_total, rate_per_t, driving/stopped_seconds | Nome owner não comprova autenticação de proprietário. Mesmo bloqueando nova API, endpoints antigos precisam revisão para evitar acesso alternativo. |
| P0 | `truckbox-map-data` v3 linhas 46–49 e 81: fallback público; trip/trips completos | Localização e dados indiretos podem vazar. Necessário escopo autenticado e projeção explícita. |
| P0 | `truckbox-live` v7 linhas 44–59: autentica dashboard_access.id mas seleciona último veículo global | Risco de acesso entre veículos. Bloquear uso no novo módulo. |
| P0 | Advisors + pg_proc: seis RPCs públicos SECURITY DEFINER executáveis por anon/authenticated | Revisar corpos/ownership e revogar grants em mudança própria. RLS das tabelas não basta. |
| P1 | app v20, ingest v26 e gps v4 possuem atalho de dispositivo conhecido com active=true/hash fixo | Revogação em devices pode não atingir o atalho. Não copiar hashes/identificadores para o novo app. |
| P1 | app snapshot/set_tank_state, permissões baseadas só em token de coleta | Credencial do Gateway não deve ser credencial de leitura da multimídia nem de dono. |
| P1 | `truckbox_ai.empty_leg_auto_v1` e reconcile_empty_leg_v1 | Existe reconciliação automática de vazio; investigar semântica antes de vincular custos ao frete. Novo contrato exigirá confirmação Motorista. |

Todas as funções acima têm `verify_jwt=false`; algumas validam tokens próprios. A flag isolada não é prova de endpoint aberto: os riscos acima foram obtidos da autorização interna. Não foram enviados pedidos que revelassem dados pessoais/financeiros para confirmar exploração.

Leitura adicional de `pg_get_functiondef` confirmou ausência de checks auth.uid/auth.jwt/session nos corpos de truckbox_trip_live_summary e truckbox_fuel_range_metrics. `reconcile_empty_leg_v1` atualiza empty_before_* e registra empty_leg_auto_v1 sem confirmação explícita no corpo inspecionado. Isso reforça a necessidade de contrato novo, sem alterar o comportamento existente silenciosamente.

Advisors: [RPC SECURITY DEFINER público](https://supabase.com/docs/guides/database/database-linter?lint=0028_anon_security_definer_function_executable), [RLS sem policy](https://supabase.com/docs/guides/database/database-linter?lint=0008_rls_enabled_no_policy). RLS ausente em tabelas privadas de truckbox_ai requer avaliar schemas expostos/grants; exposição HTTP desse schema não foi comprovada.

## Contratos e fornecedores

`truckbox-app` v20 realmente chama Google Routes e Elevation usando segredo server-side e RPC truckbox_predict_planned_route. Isso prova implementação, **não** credencial provisionada, licença comercial, faturamento, cobertura, restrições de altura/peso/eixos para rodotrem ou segurança da rota. Não há fonte de tarifas/praças comprovada. Não contratar serviços nem inventar alternativas. Mapa nativo, cache/offline, atribuição/licenças e termos Google precisam decisão e validação.

DTC: decodificação/captura DM1/DM2/J1708, episódios e horários não comprovados. Queda de comunicação nunca inativa falha. 4G/SD/GPS do Core Rev.B.1 não verificados fisicamente. Jornada: parada não é descanso; classificação Motorista, legislação vigente e revisão jurídica pendentes. LGPD: identidade de motorista, localização, retenção, acesso, exclusão e base legal precisam validação do responsável. Estatísticas devem ponderar tempo, excluir lacunas/sentinelas/duplicatas e explicar cobertura.

## Segredos e preservação

O proprietário autorizou explicitamente nesta conversa a publicação da auditoria detalhada e do inventário da Cloud no repositório público. A autorização não inclui tokens, chaves privadas, registros pessoais/financeiros nem alterações em produção; esses materiais não integram a entrega.

Varredura inicial por formatos de JWT/chaves privadas/sb_secret/GitHub token não encontrou esses formatos. A inspeção manual encontrou **credencial de leitura em query string** em `multimedia/.../MainActivity.kt:626`, que a primeira expressão não detectava. Não reproduzir seu valor, não copiar a URL e propor rotação/revogação em revisão própria. Não é prova de histórico Git limpo (clone raso). Tokens do Motorista são configurados em runtime; há hashes/IDs fixos em fontes Cloud, que não serão copiados. Novo módulo não incluirá credencial, token de ingestão, service_role, VIN ou IDs reais. Build/SDK/keystore/cache ficam ignorados ou fora do repo; executar scanner incluindo URLs antes da entrega e inspecionar APK. Não rotacionar credenciais nem corrigir produção silenciosamente.

## Decisão para o primeiro incremento

Criar módulo isolado com cinco telas nativas e estados honestos, catálogo exato dos 13 instrumentos e testes das regras. Integração Cloud protegida **BLOQUEADA** pelo contrato/autenticação acima; os módulos começam negados. API nova pode ser proposta e testada localmente, sem deploy ou migração de produção. Dados reais só serão apresentados quando a origem, recência e autorização forem verificáveis. Nenhuma validação de campo será declarada concluída.

## Adendo 14/11 — Core localizado, Starlink e canal Cloud proposto

O inventário inicial acima descreve a main. Ao examinar as branches, foram encontrados os fontes reais do **Core 0.2.23** em `fix/revb1-v0223-polling-fuel`, commit `bd723891cdaad2b23466c8311d13409b3d4a36c0`. `firmware/espidf/components/tb_core/telemetry.c` publica `/api/sensors` schema 2 em páginas de até oito, com boot/captura/idade/validação/origem e último valor válido separado. `tb_web.c` publica `/api/health` com UID/boot e rede. `/api/state` é diagnóstico compacto, não DTO plano dos instrumentos.

`components/tb_base/network.c` e `WIFI_LINK_CLOUD_v0.2.2.md` provam AP+STA/roteador externo na 0.2.23, porém **sem NAPT**. Isso explica sensores acessíveis pelo AP sem Internet para a multimídia. Por solicitação posterior explícita, foi preparada a candidata **0.2.24**, [PR 27](https://github.com/rbasniak1989-lab/Truckbox/pull/27), para compartilhar Starlink via Core; nenhuma gravação/OTA executada. Diff limitado a rede/configuração de build/versão/testes/documentação; coleta, pinos, NVS, fila/ACK e partições preservados. Compilação/testes são evidência local/CI, não comprovação física de NAT/DNS/Starlink.

O usuário relatou sensores recebidos no APK e desaparecendo entre consultas. O incremento 0.2.0 conserva último valor real por instrumento/Core com idade, sem promovê-lo a amostra atual ou valor de cálculo. Zero real substitui a leitura; ausência/NA não apaga histórico volátil. Trocas de host/UID/veículo e MIDs/boots do ar impedem mistura. Isso é requisito posterior do usuário e substitui a regra visual de ocultar todo dado após cinco segundos; frescor para cálculos permanece obrigatório.

Leitura somente de catálogo em 14/11 confirmou `public.gps_latest` com veículo/dispositivo, fonte, captura, lat/lon, precisão e provedor. Consulta agregada encontrou uma posição `ANDROID_PHONE/gps` com data de **30/10**. Não há evidência de GPS atual; coordenadas/identificadores não foram publicados. A conta Auth do aplicativo correspondente ao e-mail informado não foi encontrada. Nenhuma conta/convite/senha foi criada ou enviada.

O adaptador novo em `backend-proposal/v1/cloud/repository.ts` lê metadados explícitos de viagem e GPS somente no veículo de vínculo autenticado, sem copiar credenciais ou atalhos legados. `security.ts` verifica Auth, sessão no banco, posse RSA, nonce e relógio; `app.ts` revalida contexto/política e emite lease curta. Migração local privada e Edge foram preparados/testados isoladamente, **não aplicados/publicados**. Os P0/P1 legados acima continuam pendentes; o canal novo não os elimina.

Na branch firmware existem decodificadores e testes host de PID194/diagnóstico e persistência. Isso amplia a evidência de implementação de base, sem provar episódios/códigos/horários do caminhão nem contrato de leitura completo para a nova multimídia. DTC, IA, fontes verificadas de consumo/financeiro/jornada, mapa/tarifas, legal e hardware continuam bloqueios descritos em CONTRATOS_V1.md. Plano/revisão atualizados em PLANO_INTEGRACAO_CORE_CLOUD.md e REVISAO_CORE_CLOUD_020.md.

### Autorização posterior de teste sem contas

O proprietário solicitou teste de viagem sem cadastro de contas, todos os módulos autorizados para seu vínculo e GPS exclusivamente Motorista. Após rejeição inicial da publicação e aprovação explícita da configuração (`verify_jwt=false` com autenticação própria e service_role só servidor), foram aplicadas duas tabelas privadas `multimedia_field_test_v1.access/nonces`, uma RPC com EXECUTE apenas servidor e a nova Edge `multimedia-next-v1` v1. Um único vínculo de até 72 h, Financeiro/Jornada=true, aguarda ativação pelo Keystore real. A credencial adicional de QA foi rejeitada e removida; não criada. Nenhum usuário Auth ou objeto/dado antigo foi alterado. Migração completa de 12 tabelas permanece proposta.

Conferência hospedada: um acesso ativo, sem aparelho vinculado por automação; negação EXECUTE a anon/authenticated; 401 sem token; 403 para token/RSA válidos com Core errado, sem consumir o vínculo. Token bruto não integra APK/Git; código e hash só fora do repositório/banco privado respectivamente. Viagem seleciona apenas metadados; GPS ANDROID_PHONE do Motorista preserva origem/data. Permissão não valida contador, dinheiro ou jornada: esses payloads permanecem indisponíveis até haver fonte verificada. Teste positivo de ativação, integração completa e hardware continuam pendentes.
