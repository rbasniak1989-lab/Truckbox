# Canal Cloud da multimídia — código para revisão, sem deploy

O título refere-se ao canal completo de contas/12 tabelas. O proprietário autorizou posteriormente um **canal temporário de campo**, já implantado com duas tabelas e uma Edge nova, sem contas; consultar FIELD_TEST.md. A chave publicável do projeto é configuração pública; códigos temporários, chaves privadas e credenciais servidor nunca entram no Git/APK.

Implementação HTTP em `app.ts`, verificação de usuário pelo Supabase Auth e prova de posse RSA/SHA256 por aparelho em `security.ts`, e adaptador de consultas parametrizadas em `repository.ts`/`postgres-db.ts`. A função Edge `multimedia-next-v1` está preparada na pasta Supabase local. A migração foi gerada pelo CLI oficial 2.81.3 e não foi aplicada ao projeto hospedado.

## Fluxo

1. O Android usa uma **publishable key**, autentica o usuário pelo Supabase Auth e cria uma chave RSA2048 não exportável no Android Keystore.
2. POST `/v1/enroll`, com token de usuário e prova assinada, cadastra a chave pública e a solicitação do usuário. Isso **não** cria vínculo ou permissão. A aprovação permanece na Cloud.
3. O administrador relaciona tenant, veículo, usuário e aparelho no schema privado. Cada alteração de política exige versão nova e grava auditoria. São permitidos Financeiro/Jornada juntos, isolados ou ambos negados.
4. GET `/v1/state` verifica token, existência/validade da sessão, assinatura, nonce de uso único e relógio; resolve o vínculo no banco. Lê somente projeções autorizadas e confere vínculo/política novamente antes de serializar. Erros não contêm payload ou detalhes SQL.
5. Trip metadata é independente dos módulos protegidos. A API não seleciona notas, frete, preço, valores ou horas de direção na consulta de viagem. O deslocamento vazio não herda o frete anterior nem o `empty_before_*` automático.
6. `gps` em `/v1/state` seleciona apenas a última posição do veículo autorizado em `public.gps_latest`; dispositivo de origem, quando presente, deve continuar ativo e vinculado ao mesmo veículo. Coordenadas/tempo/faixas são validados. Origem/captura permanecem reais, sem projetar movimento; a etiqueta `RECEIVED_NOT_PHYSICALLY_VERIFIED` impede afirmar validação física. Sem linha válida, `gps=null`.

A resposta de estado ecoa o nonce da requisição e inclui `receivedAt` e `leaseExpiresAt` a partir do mesmo relógio, com 20 s entre eles. Essa lease é separada da validade administrativa da política. O Android desconta todo o tempo de rede/servidor, aguarda ACK real antes de liberar dados e remove módulos ao expirar. Polling de 10 s não cria autorização offline. Resposta gravada de outra requisição não tem nonce compatível.

Todas as assinaturas incluem `truckbox-next-v1`, UUID do aparelho, método, caminho+query exatos, instante Unix em ms, nonce UUID, SHA256 do corpo e SHA256 do token, separados por LF. Cabeçalhos: `X-TruckBox-Device`, `X-TruckBox-Time`, `X-TruckBox-Nonce`, `X-TruckBox-Signature` (base64 padrão). RSA PKCS1v1.5/SHA256; tolerância de relógio ±60s. O corpo assinado é exatamente o transmitido. Um token sem a chave do aparelho não autoriza a API.

## Dados ainda sem origem liberada

Os read models `verified_consumption`, `financial_snapshots` e `journey_windows` começam vazios. Não há backfill assumindo qualidade de contador legado, responsabilidade de despesa ou descanso. Sem fonte verificada, as APIs retornam indisponibilidade e a viagem pode aparecer com consumo ausente. Isso evita criar zeros ou médias fictícias. Alimentação desses modelos, ledger de tanque, DTC, IA e jornada legal continuam etapas distintas.

O schema privado tem RLS e nenhum acesso `anon`/`authenticated`. A função usa conexão de banco somente do ambiente servidor, com autorização explícita por usuário/sessão/aparelho/tenant/veículo em todas as rotas. Não existem novos RPCs públicos `SECURITY DEFINER`. Publicar esse canal não corrige automaticamente os fallbacks públicos legados descritos na auditoria.

## Validar antes de publicar

`npm ci && npm test && npm run typecheck`. Os testes usam Postgres WASM isolado (PGlite), chaves RSA reais geradas durante os testes e dados sintéticos. Somente a verificação externa do usuário Supabase é substituída por fixture; isso não valida Auth/Edge em produção nem hardware.

O schema base `auth.users/auth.sessions/public.trucks/public.devices/public.trips/public.operation_state/public.gps_latest` é pré-requisito do canal; não executar `db reset` contra o ambiente existente. A pasta Supabase nunca foi vinculada ao projeto de produção. Não há credencial de banco, service_role ou token de ingestão no pacote.

Antes da implantação: revisar migração, privilégios, Auth, sessão, índice de nonces, fonte de dados e remediação dos acessos legados; escolher homologação isolada ou autorizar explicitamente a adição no projeto existente. Executar advisors e a matriz de negação no ambiente real. Preparar conta de homologação pelo fluxo seguro do Supabase; não enviar senha em chat ou commit.

Rollback operacional em `rollback.sql`: revogar vínculos novos e desativar somente a nova função, conservando cadastros e auditoria. Nenhum DROP ou alteração de dados antigos é necessário.
