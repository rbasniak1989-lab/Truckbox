# Revisão concreta — Next 0.2.0 / Core 0.2.24

A multimídia fica somente no AP do Core: sensores por HTTP local; viagem/GPS por HTTPS autenticado da Cloud, usando Starlink compartilhada pelo Core. Último valor real permanece com idade; cálculos exigem amostras atuais. Financeiro/Jornada são opcionais e independentes.

Android/backend: [PR 21](https://github.com/rbasniak1989-lab/Truckbox/pull/21). Firmware separado: [PR 27](https://github.com/rbasniak1989-lab/Truckbox/pull/27), baseado na fonte 0.2.23 localizada. Nenhum merge, criação de conta ou flash foi executado. Por solicitação posterior e aprovação explícita do proprietário, foi aplicada **somente a migração temporária de duas tabelas** e publicada a nova Edge de teste, com um único vínculo de 72 h e módulos autorizados. Ver GUIA_TESTE_VIAGEM_020.md. O plano completo de contas/12 tabelas abaixo continua proposto, sem implantação.

## Arquivos para revisão

| Parte | Arquivos / efeito |
|---|---|
| Android | `multimediaNext/src/main/.../data/{CoreSensorClient,CoreReadingsRetention,CloudHttpClient,CloudCredentials,CloudCoordinator,CloudModels}.kt`, domínio e Home/Viagem/Configurações. AppId Next e apps antigos preservados. |
| SQL aditivo | `backend-proposal/v1/supabase/migrations/20261114175416_multimedia_next_v1_access.sql`: somente schema privado `multimedia_v1`, 12 tabelas, índices, constraints e trigger de versão/auditoria. Sem alterações em objetos/dados antigos. |
| API v1 | `backend-proposal/v1/cloud/{app,security,repository,postgres-db}.ts`: Auth, prova RSA, escopo no servidor, consultas parametrizadas, projeções, nonce, lease e ACK. |
| Edge | `backend-proposal/v1/supabase/functions/multimedia-next-v1/`: entrada pronta para revisão, função ainda não publicada. |
| Reversão Cloud | `backend-proposal/v1/cloud/rollback.sql`: revoga vínculos novos conservando dados/auditoria. Desativação da rota integra o procedimento revisado. |
| Firmware | PR 27: rede NAPT/DNS apenas Core, versão/defaults/build/testes. Coleta, polling, pinos, NVS, partições, fila/ACK e Motorista preservados. |

## Implantação Cloud proposta — ainda não executada

1. **Ambiente e exposição legada.** Só há um projeto acessível, sem branch de desenvolvimento. Preferir homologação isolada; não criar recursos pagos sem decisão do responsável. Fallbacks públicos/RPCs antigos da auditoria continuam bloqueio de aceite comercial. O app novo não os usa; publicar rota nova não corrige esses acessos.
2. **Pré-requisitos e retorno.** Conferir `auth.users/sessions`, `public.trucks/devices/trips/operation_state/gps_latest`, privilégios/índices e backup. A migração local é transacional, sem reset, DROP, backfill ou calibração. Aplicar apenas no ambiente expressamente aprovado. Conferir 12 tabelas com RLS e negação a `anon`/`authenticated`; não expor o schema via PostgREST.
3. **Publicar somente `multimedia-next-v1`.** Conexão PostgreSQL/credenciais exclusivamente no servidor (`MULTIMEDIA_DATABASE_URL` ou configuração reservada disponível), TLS verificado e pool limitado. Configurar Auth URL/chave pública no servidor. Testar JWT do projeto no gateway e validação por Supabase Auth; nunca desativar TLS/autorização para fazer funcionar.
4. **Conta e vínculo reais.** O e-mail informado não correspondeu a conta do aplicativo em Auth na consulta somente leitura de 14/11. Login no painel Supabase/GitHub não cria essa conta. Preparar a conta pelo fluxo seguro, sem senha no chat/Git. O app registra solicitação da chave pública e mostra UUID; isso não concede acesso. Aprovar tenant/membership/veículo/Core ativo/usuário/aparelho após conferir IDs reais. Financeiro=false e Jornada=false inicialmente, sem concessão automática ou toggle local.
5. **Provar o serviço hospedado.** Quatro matrizes, outro tenant/veículo, Core inativo, sessão encerrada, revogação durante consulta, nonce/relógio/replay, ACK repetido, offline/lease/reboot. Viagem sem frete/notas/jornada; GPS com captura/origem reais. Executar advisors, revisar logs e registrar evidência sanitizada antes de habilitar fontes reais.

Os read models `verified_consumption`, `financial_snapshots` e `journey_windows` começam vazios. Contador legado, responsabilidade de custo e descanso não são presumidos. Sem fonte verificada, resposta indisponível, sem zero fictício. Ledger persistido de ambos tanques, DTC/episódios, pedágios/pagamentos, confirmação de vazio no Motorista e IA continuam em CONTRATOS_V1.md.

## Firmware e hardware

Candidata 0.2.24 exige revisão de binário/hash/perfil/placa e preservação da imagem conhecida/backup antes de instalar pelo mecanismo existente. O APK não executa OTA. Roteiro no PR 27: Core STA na Starlink 2,4 GHz, multimídia somente no AP Core, renovação DHCP, DNS/gateway/HTTPS, login e sensores simultâneos; queda/retorno Starlink; memória/latência/watchdog/fila/ACK e continuidade de captura. `napt_active` não comprova Internet.

Não há Core/Link/ECU/central Android conectado ao ambiente. Capturas nativas, host, RSA e Postgres isolados não substituem ensaio no equipamento. O GPS encontrado na Cloud era de 30/10; não é posição atual. Mapas/rota/altimetria/tarifas exigem fornecedor/licença; jornada legal/LGPD exigem revisão do responsável.

## Reversão

- Android: remover/reinstalar somente Next conforme guia; apps antigos intocados.
- Cloud: revogar vínculos novos e desativar rota; conservar schema, solicitações e auditoria. Nenhum DROP/reset antigo.
- Core: voltar à imagem 0.2.23 conhecida pelo mecanismo existente, sem apagar NVS/partições/armazenamento; conferir coleta e contadores após retorno.

Resultados/hashes efetivamente obtidos em TESTES_REALIZADOS.md e `docs/multimediaNext/evidencias/`. A lista de implantação acima é proposta para revisão, não execução.
