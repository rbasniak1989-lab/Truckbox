# Diagnóstico Volvo J1587 — serviço preparado, não publicado

Código incremental da candidata Core0.2.17 / Motorista0.6.2. O firmware usa a ingestão e ACK atuais, sem depender deste serviço de consulta para coletar, persistir ou fazer polling. Nenhum deploy ou SQL de escrita foi executado.

## Componentes

- `volvo-fh-e5-0.1.0.json`: catálogo pequeno com três correspondências candidatas, módulo, FMI genérico, versão, família/ano, descrição, causas possíveis, verificações, origem e confiança. Não é base OEM completa. As descrições foram redigidas para esta integração; não se distribui a norma SAE nem uma base proprietária integral.
- `domain.mjs`: parser194, projeção do hexadecimal legado, fragmentos multisseção completos, histórico sem resolução por ausência e diferenças de contadores. Preserva zero/ausente/desconhecido separadamente.
- `handler.mjs`: API de consulta autenticada; valida dispositivo ativo e `token_hash`, restringe todos os acessos ao `truck_id` associado e rejeita Core de outro veículo. Corpo limitado, páginas de200amostras, no máximo128falhas por resposta e leitura limitada de fragmentos/extremidades. Não escreve em tabelas nem confirma registros do Core.
- `../../supabase/functions/truckbox-diagnostics/index.ts`: adaptador Deno/Supabase. Usa as tabelas existentes `devices`, `trips`, `telemetry_samples`; não exige migração. O serviço não altera o `truckbox-ingest` existente.
- `../../app/src/main/assets/volvo-diagnostics-cache.json`: cópia idêntica do catálogo, verificada em teste. O cache Android é limitado, isolado por veículo e não substitui um histórico completo no servidor.

## Contrato

`POST /functions/v1/truckbox-diagnostics`, JSON com `device_uid` e `token` já cadastrados. Não inserir credencial de serviço no aplicativo. Campos opcionais:

- `core_device_uid`: verifica se o Core local pertence ao mesmo veículo autenticado.
- `trip_client_uid`: consulta viagem pertencente ao veículo, ou404.
- `before_id`: cursor de amostras; número decimal em string, para preservar bigint.
- `fault_offset`: paginação de até128falhas da página atual.

Retorno inclui `faults`, `next_before_id`, `next_fault_offset`, `history_complete=false`, `operating`, `counter_reference`, versão do catálogo e `financial_records_modified=false`. Repetições não geram nova ativação; código desconhecido conserva MID, tipo, identificador, FMI e bruto.

Eventos novos continuam com `sample_kind=J1708_REQUEST_EVENT`: `DIAGNOSTIC_DTC`, `DIAGNOSTIC_RAW`, `DIAGNOSTIC_INVALID`, `COUNTER_CAPTURE`, `VOLVO_OPERATING`, `POLLING_PID_STATS`. O hexadecimal legado `j1587_raw["194"]=[hex,idade_s,MID]` é preservado. Esta consulta não migra nem apaga amostras antigas.

O filtro por viagem usa a janela temporal disponível e inclui proximidade das extremidades para as capturas assíncronas. Isso não cria uma associação financeira ou um horário de falha exato quando faltam referências temporais. As capturas START/END são buscadas separadamente, pois podem estar em páginas distantes. Quando faltam, mantém a necessidade de integração/estimativa identificada.

`financial_primary_allowed` só pode ser verdadeiro para um delta coerente com extremos próximos≤30s dos eventos; não significa validação OEM/física nem escrita financeira. `same_instant/simultaneous=false` e os tempos de cada contador permanecem explícitos. Retentativa preenche um extremo faltante, sem substituir a primeira leitura válida do mesmo extremo.

## Verificação local

Node22.13+ ou24:

```sh
node --test cloud/diagnostics/test_diagnostics.mjs
node cloud/diagnostics/check_entry.mjs
```

O segundo comando remove os tipos e verifica a sintaxe sem executar o adaptador. Não substitui um deploy/invocação Deno real. A suíte testa autenticação/escopo com mocks; nunca usa credenciais de produção.

Para reproduzir o mock da ingestão atual, fornecer privadamente o fonte v26 ao script `firmware/espidf/cloud/test_ingest_mock.mjs --source ARQUIVO_PRIVADO.ts --v26`. O arquivo privado não integra o pacote público.

## Publicação posterior, somente autorizada

Depois de revisão e autorização explícita para deploy, um operador com acesso ao projeto pode publicar exclusivamente `truckbox-diagnostics` usando a CLI Supabase a partir da raiz. O adaptador obtém `SUPABASE_URL` e `SUPABASE_SERVICE_ROLE_KEY` no ambiente do servidor; não há segredos novos no fonte.

`verify_jwt=false` é intencional para o contrato dos dispositivos existentes: a função aplica sua própria autenticação por token antes de qualquer consulta de telemetria. Não desabilitar essa verificação no handler. Em uma homologação autorizada, conferir401 com token incorreto,403 para Core de outro veículo,404 para viagem alheia, paginação e limites tardios de contadores. Nenhuma tabela de viagem deverá mudar.

Enquanto não publicado, o aplicativo usa as APIs locais e seu cache. A consulta remota e o histórico completo continuam pendentes. Rollback deste serviço é independente da OTA do Core e não exige apagar dados.
