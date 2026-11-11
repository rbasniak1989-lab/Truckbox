# Verificação — Core 0.2.16 produção

Executada em 11/11/2026, Linux, ESP-IDF **5.5.2**, ESP32-C6/flash 8 MB. Não foram executadas OTA, transmissão real, alterações na placa/Cloud ou nova campanha NEXIQ.

## Resultados

- `bash firmware/espidf/tools/build_v020.sh core`: **PASS**, projeto real, aplicação 0.2.16, ESP-IDF v5.5.2, perfil J1708_ONLY. Binário de aplicação **1.517.168 bytes**, dentro da OTA de 3.145.728 bytes. Identidade e hashes em `test_evidence/v0216_image.json`.
- `sh firmware/espidf/tests/run_all.sh`: **PASS**, testes C com ASan/UBSan e 24 testes Python. Inclui armazenamento/recuperação, CRC/seq/ACK, fairness/watchdog, J1939, J1587, UART real com mocks, viagem, GPS, transporte Link, configuração, memória/JSON e painel.
- Novo `run_j1708_production.sh`: **PASS**, controlador/scheduler/guard/monitor reais com relógio/UART simulados.
- Navegador, HTML real servido por HTTP local: **22 cenários PASS**, incluindo automático sem comando de arm, separação dos ensaios, parada/reabilitação, painel móvel, auth, OTA/erros e diagnóstico parcial por falta de RAM. As rotas são mocks; não há acesso à placa.
- Ingestão v26 executada localmente com Supabase/Deno/banco mocks: **PASS**, status automático e snapshots preservados no payload, nulos/idades/qualidades mantidos, ACK correto e nenhum efeito em viagem/consumo/latest. Fonte privada do backend não foi publicada; executor de regressão está em `cloud/test_ingest_mock.mjs`. Nenhum deploy/migração SQL realizado.

## Casos exercitados

| Caso | Resultado |
|---|---|
| Configuração inicial e migração | Chave NVS `v216` criada uma vez; perfil conhecido MID172; credenciais/base/v212 preservados |
| Reinício e desativação | Carrega sem nova escrita; opt-out permanece; corrupção não restaura habilitação silenciosa |
| NVS sem espaço/falha de gravação | Nova migração bloqueada; configuração base válida/passiva preservada; política não é tratada como persistida |
| Boot sem aplicativo/internet | Início após três janelas saudáveis; nenhum parâmetro Wi-Fi/Android participa do controlador |
| Permanência | Relógio avançado 13 horas, sem renovação de autorização; produção segue ativa, sem expiração de lease |
| Cadências/prioridade | Todos os 17 respondem, prioridade 31/33 em arrancada, normal/rápido conforme tabela; sem PID duplicado |
| Limite e arbitragem | Pelo menos 500 ms entre inícios; um pendente; backend ocupado adia sem erro de TX ou perda da leitura passiva |
| Respostas | MID errado rejeitado; MID/PID/prazo corretos contam candidata; latência registrada; não define validação externa |
| Falhas | Três timeouts/erros de eco suspendem; sem novas escritas TX no backoff; volta somente após espera e janelas saudáveis novas |
| Intertravamentos | NVS não confirmada, perfil ausente, UART/edge indisponível ou MID conflitante não transmitem |
| OTA | Pausa; configuração permanece; só retoma após novas condições saudáveis |
| Arrancada | Intenção/saída de parada, limite de 30 s mesmo com pedal mantido, retorno em cruzeiro, ausência de velocidade não inventa evento |
| Embreagem | Padrões sintéticos geram candidatos combinados; bytes/idades/qualidades/nulos são preservados; sem confirmação de patinação |
| Memória | Falha em cada alocação da tupla não muda posições nem deixa nós órfãos; streaming/paginação continuam funcionando |

Persistência é teste do **código real de configuração**, com a API física NVS simulada. O teste do controlador usa política carregada como fronteira; o build liga a NVS real do ESP-IDF. Não se declara que a nova chave já foi gravada no dispositivo.

## Duas horas offline na carga nova

7.200 segundos virtuais, aproximadamente 60 quadros passivos/s, **6.780 requisições e 6.780 candidatas**, janelas aceleradas de 30 s em cada 120 s. Decoders, cache e construtores de registros reais. Autorização/controle e persistência são testados em camadas separadas; os 14 resets abaixo são da camada de arquivos, não restauração de sessão ativa.

| Relógio | Registros | Bytes físicos | Orçamento | Resets simulados | Sincronização |
|---|---:|---:|---:|---:|---|
| Sem UTC | 3256 | 1281076 | 1818624 | 14 | 3256 únicos, 3264 entregas incluindo retries; sem duplicação ou válidos pendentes |
| Com UTC | 3256 | 1379760 | 1818624 | 14 | 3256 únicos, 3264 entregas incluindo retries; sem duplicação ou válidos pendentes |

438.780 quadros processados, 65 leituras em cache, **zero gravações RAW contínuas**, zero perda por lotação e erros de escrita no cenário. Menos registros que a versão anterior é consequência da agregação de sucessos, não de reduzir a recepção/pedidos. Todos os sensores passivos e os 17 solicitados aparecem nos registros. Evidências de arquivo isoladas continuam preservadas após ACK e ocupam espaço futuro; não foram apagadas.

Este resultado comprova duas horas **na carga especificada**, não autonomia ilimitada ou em uma flash já cheia. Mais eventos/ocupação anterior podem reduzir a margem. Não há cartão SD habilitado.

## RAM, pilha e partições

Comparação dos ELF/DWARF com a 0.2.15:

| Item | 0.2.15 | 0.2.16 |
|---|---:|---:|
| RAM estática, incluindo IRAM | 202580 B | 203524 B |
| Item da fila de eventos | 392 B | 392 B |
| Controlador ativo | 1120 B | 1136 B |
| Scheduler | 592 B | 868 B |

**+944 B estáticos**, sem aumento da fila de 16 eventos. Não há alocação JSON por quadro no monitor nem NVS por ciclo. Quadro de pilha individual compilado: `request_task` 528 B, `j1708_task` 208 B, `snapshot_task` 368 B e `auto_tick` 64 B; não é medição de high-watermark nem soma de toda a cadeia de chamadas. Heap/pilha físicos ainda não foram medidos para esta imagem.

Tabela de partições inalterada: SHA256 `5e25baf13060f871fb6e9103f6cec4000a6c0e36bdf4d554fa037a26f3f91832`. Bootloader/rollback, codec, ACK, viagens e Link preservados. Nenhuma funcionalidade da v0.1.0-b1 sobrescrita.

## Arquivos e limitações

Principais arquivos: `tb_base/config.c`/`include/tb.h` (NVS); `j1708_17pid/src/tbx_j1708_requests.*` e `tbx_j1708_policy.h` (fila/cadências); `tbx_j1708_auto.*` (produção/recuperação); `tbx_transmission.*` (monitor); `capture.c`/`telemetry.c` (integração/dados); `core.c`/`index.html` (API/painel); testes e ferramentas de pacote. Diff integral acompanha a release.

Limitações explícitas: qualidade proprietária permanece EM VALIDAÇÃO; candidatos de embreagem não são diagnóstico mecânico; PID160 não recebe relação de eixos inventada; perfil J1708_ONLY da base mantido, sem resolver a ausência física de CAN. Não é necessário repetir NEXIQ para habilitar a política conhecida. A imagem foi preparada, não aplicada.

Logs: `test_evidence/v0216_host.txt`, `v0216_build_core.txt`, `v0216_browser.*`, `v0216_cloud_mock.txt`, `v0216_memory.json`, `v0216_stack_frames.txt`, `v0216_profile_readonly.json`. [Funcionamento e atualização](PRODUCAO_v0.2.16.md).
