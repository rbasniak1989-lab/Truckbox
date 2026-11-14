# Canal temporário de campo — autorizado pelo proprietário

Atualização v2: timestamps PostgreSQL com offset são serializados em UTC `Z`, compatível com o parser `Instant` AOSP Android 8–13. Frações de segundo e instantes preservados; nenhuma alteração no código de acesso, vínculo ou permissões. O proprietário confirmou o vínculo no APK e a Cloud registrou ACK v1. As notas de implantação v1 abaixo são históricas; a ativação positiva já foi confirmada.

Solicitação posterior de 14/11: teste de viagem sem provisionar contas, liberar Financeiro/Jornada para o próprio vínculo e GPS exclusivamente TruckBox Motorista via Cloud. Não usar credenciais de ingestão nem fallback público.

`field-test.ts` implementa prova RSA, token opaco aleatório 256 bits, nonce, relógio e RPC somente servidor. A migração `20261114221000_multimedia_field_test_v1.sql` cria duas tabelas privadas com RLS, expiração máxima 72 h, pinning de aparelho/chave/Core e versão de permissões. O principal é um vínculo temporário real, distinto de usuário Supabase Auth. Código bruto fica fora do Git/APK e só é entregue privadamente; banco armazena SHA256. Primeiro uso exige Core existente/ativo do veículo autorizado. Próximos usos exigem mesma chave/Core/aparelho. Revogação e expiração são verificadas em cada consulta.

`public.truckbox_multimedia_field_test_v1` é SECURITY DEFINER com search_path fixo e EXECUTE **apenas service_role**, negado explicitamente a PUBLIC/anon/authenticated. A Edge usa segredo reservado do servidor para a RPC depois de verificar a assinatura. O gateway `verify_jwt=false` permite o código temporário; não elimina a autenticação implementada na função. Não habilitar nenhuma rota anônima.

Estado: migração temporária aplicada; após aprovação explícita da configuração pelo proprietário, a **nova** função `multimedia-next-v1` v1 foi publicada. Um único vínculo proprietário, Financeiro/Jornada=true, expira em 17/11/2026 22h29 UTC e aguarda o aparelho real. Uma credencial adicional de QA foi rejeitada e removida do procedimento; não foi criada. O esquema completo de 12 tabelas e as contas Auth não foram alterados. Código privado não foi ativado pela automação.

Verificação hospedada: 401 sem token; 403 com token/prova RSA válidos e Core errado; um único acesso ativo e ainda sem aparelho vinculado; EXECUTE da RPC negado a anon/authenticated, permitido somente ao servidor. Ativação positiva com o Keystore da multimídia e hardware permanecem pendentes.

Projeções: viagem sem frete/notas/jornada; GPS apenas ANDROID_PHONE, incluindo última amostra do Motorista quando gps_latest contém uma fonte Core/Link mais nova. Mantém timestamp/origem/precisão reais. Financeiro/Jornada podem ser autorizados remotamente, porém dados continuam null sem fonte verificada; consumo/ledger sem zeros fabricados. A fonte de permissões nunca é preferência Android.

Testes com Postgres WASM e RSA reais em `field-test.test.ts`: quatro matrizes, primeiro uso, outro aparelho/Core, ausência de token, assinatura falsa, tempo antigo, replay, ACK idempotente/versão, revogação/Core inativo, GPS só Motorista e negações de privilégios SQL. São fixtures isoladas, não teste de hardware ou implantação.

Rollback operacional após provisionamento: `update multimedia_field_test_v1.access set active=false,policy_version=policy_version+1 where active;` e desativar a nova função. Conservar tabelas/nonces como evidência; não tocar em apps/dados/RPCs antigos. Enquanto não houver registro ativo nem Edge publicada, não há acesso de campo habilitado.
