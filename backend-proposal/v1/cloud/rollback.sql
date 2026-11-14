-- Rollback operacional. Revisar e executar apenas quando autorizado.
-- Revoga o novo canal; não apaga cadastro, operações ou auditoria.
begin;
update multimedia_v1.bindings
set active=false,monitoring_read=false,financial_read=false,journey_read=false,
    policy_version=policy_version+1
where active or monitoring_read or financial_read or journey_read;
commit;
-- Depois, desativar SOMENTE a função Edge multimedia-next-v1.
-- Não executar DROP de schema/dados e não alterar funções/filas legadas.
