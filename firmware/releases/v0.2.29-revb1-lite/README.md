# TruckBox Rev.B.1 Lite 0.2.29

O firmware do **Link** grava a telemetria em flash antes de enviá-la e retoma o envio automaticamente depois de falhas de internet. A fila compacta suportou seis horas de aquisição simulada com todos os campos de sensores preenchidos. O Core 0.2.28 instalado já é compatível. A imagem opcional do Core 0.2.29 acrescenta o motivo do reset ao diagnóstico do Link.

| Arquivo | Uso |
| --- | --- |
| [truckbox_link_0.2.29-lite_ota.bin](truckbox_link_0.2.29-lite_ota.bin) | Atualização de aplicativo para o Link ESP32-C6 |
| [truckbox_core_0.2.29-lite_ota.bin](truckbox_core_0.2.29-lite_ota.bin) | Atualização opcional para o Core ESP32-C6 com política J1708 já gravada |
| [SHA256SUMS.txt](SHA256SUMS.txt) | Hashes dos dois BINs. Conferir com `sha256sum -c SHA256SUMS.txt` |
| [TESTES.txt](TESTES.txt) | Logs da suíte, inspeção das imagens e mapas de memória |

## Lacuna de 22/11/2026

| Captura no equipamento, BRT | Persistência no Supabase, BRT | Sequência |
| --- | --- | ---: |
| 03:34:09.367 | 03:34:11.452 | 1000422 |
| 03:39:29.791 | 03:46:30.210 | 1003456 |
| 03:46:38.483 | 03:46:40.293 | 1007938 |

Os registros de invocação de `truckbox-ingest` mostram 12min18.944s entre as chamadas de 03:34:11.643 e 03:46:30.587 BRT, ambas HTTP 200. Durante a lacuna, a função ficou sem chamadas regulares e sem respostas 4xx/5xx. A amostra de 03:39:29.791 chegou 420.419 s depois da captura, evidência de que pelo menos uma amostra foi mantida até a retomada. O salto de sequência comprova descontinuidade na numeração. A reserva de sequências em blocos de 128 e a capacidade de saltar blocos de reserva impedem usar essa diferença como contagem de amostras perdidas. O histórico disponível ainda tem capturas faltantes. [Logs do Supabase](https://supabase.com/dashboard/project/wbzrrjufhqfgoctxtlyi/logs/edge-logs)

Entre os envelopes de saúde 1000420 e 1007942, o Core manteve o boot `4093039421`, o heap mínimo de 138.108 B e avançou de 871 para 1.821 requisições, com respostas de 869 para 1.819 e dois timeouts acumulados. O Link apresentou `pending=1` nas duas leituras e `dropped` de 0 para 2. O mínimo de RAM do Link reportado subiu de 128.356 B para 134.180 B. A versão anterior da saúde na Cloud omitia boot, uptime e motivo de reset do Link. Esses pontos confirmam a continuidade do Core. O proprietário reportou uptime contínuo do Link, sem comprovação independente na Cloud para esse intervalo. O relato registra 20 eventos `CORE_LOST` ou `CORE_RESTORED` em uma hora. O código antigo podia gerar o par a partir de uma única consulta perdida.

As evidências localizam a interrupção **antes da chegada ao endpoint**, entre coleta local, armazenamento e transporte HTTPS. DNS, Wi-Fi, TLS, pressão do armazenamento e tarefa de envio são hipóteses para o gatilho. O código antigo podia reescrever quase todo o journal no ACK ou na admissão de uma amostra junto à cota, sob o mutex compartilhado. Isso explica um risco concreto de bloquear a fila. A cota sozinha explica mal o intervalo, pois 12min19s a 10 s por amostra e 60 s por saúde gerariam cerca de 60 kB na cadência medida. A situação instantânea do arquivo antigo e a etapa de rede que falhou naquela hora continuam indeterminadas. [Coletor 0.2.27](https://github.com/rbasniak1989-lab/Truckbox/blob/196040ebaef44ca7e57fc3251ac0e56f5c420eb6/firmware/espidf/components/tb_link/tb_link_collector.c)

Reprodução em leitura no SQL Editor. `created_at` é a persistência no banco e `ts` é o horário que a ingestão atribuiu à captura. Compare com `edge_logs` no mesmo intervalo UTC.

```sql
select s.seq, s.ts as captura_utc, s.created_at as persistencia_utc,
       extract(epoch from s.created_at - s.ts) as atraso_s
from public.telemetry_samples s join public.devices d on d.id = s.device_id
where d.device_uid = 'tb-c6-d40592b9190c'
  and s.created_at >= timestamptz '2026-11-22 06:33:00+00'
  and s.created_at <  timestamptz '2026-11-22 06:48:00+00'
  and s.raw->>'source' = 'REVB1_LINK_LITE'
order by s.created_at;
```

## Espaço útil e retenção

Na 0.2.27, `JOURNAL_BUDGET (256u * 1024u)` limitava `TBLT1` em `/gps/telemetry.log` a **262.144 B de arquivo FAT em flash**. NVS guarda identidade, sequência reservada e referência de viagem. Em amostras reais de 03h22–03h34, a estimativa do JSON original mais a linha de CRC foi de **756 B operacionais** em média (723–773, 67 amostras) e **372 B de saúde** (279–403, 18 amostras). Isso representa cerca de 53 min com a fila vazia, seis operacionais e um registro de saúde por minuto. Arquivo já ocupado, linhas de ACK, eventos extras e compactações reduzem o valor útil. A fila cheia rejeitava a próxima amostra. Dessa forma preservava as anteriores, porém sem prioridade por classe e com contagem de descarte em RAM. A inicialização recuperava uma linha final incompleta e reconhecia o ACK gravado no arquivo. [Implementação do journal anterior](../../espidf/components/tb_link/tb_gps_outbox.c)

| Região física | Endereço | Espaço |
| --- | --- | ---: |
| Flash total do perfil ESP32-C6 | `0x000000–0x7FFFFF` | 8 MiB |
| Aplicativo OTA 0 | `0x020000` | 3 MiB |
| Aplicativo OTA 1 | `0x320000` | 3 MiB |
| `storage`, FAT com wear leveling, montada em `/gps` | `0x620000` | 1.966.080 B |
| Sistema de arquivos no Link instalado | Medição do Link 0.2.27 com pouco GPS pendente | 1.912.832 B totais, 1.900.544 B livres |

A nova fila tem teto de **1 MiB**, limitado pelo espaço livre somado aos bytes do seu próprio journal e descontada uma reserva compartilhada de 384 KiB para o restante do volume. Uma fila grande de GPS, cujo limite próprio é 768 KiB, o arquivo legado e outros dados do volume podem reduzir a cota real. Veja `offline_queue_capacity_bytes` após iniciar a coleta. O SHA-256 da tabela de partições permaneceu `e55838fc61f9490cce18b019b42ff48ca9d7c18550507ac994ea3ec163b3e044`. Os dois slots OTA e o rollback continuam disponíveis. A solução usa flash interna com setores de 4 KiB e independe do microSD. [Mapa de partições](../../espidf/partitions.csv)

## Como a fila e as conexões se recuperam

- O Link consulta `/api/telemetry/snapshot` no Core via HTTP autenticado na LAN a cada 10 s. A porta TCP 35000 é do perfil RAW CAN e fica fora desse fluxo. No perfil Core Lite, o servidor RAW fica desligado. A camada J1708, o scheduler de requisições e a multimídia conservam o comportamento atual.
- A fila grava tipo, horário original e sequência em segmentos de 16 KiB. A identidade do arquivo é vinculada aos UIDs no `publisher1` e cada quadro tem CRC. O texto dos números é compactado sem perda de precisão. `fsync` confirma o append antes do registro entrar no contador durável. O ACK alterna entre dois checkpoints com CRC. A liberação dos segmentos começa após a gravação do ACK. O segmento em uso pode continuar ocupando até 16 KiB depois de confirmar todos os seus registros. Uma gravação incompleta no fim é recuperável. Corrupção de um quadro inteiro mantém o arquivo para inspeção e aparece no status como falha da fila.
- O arquivo antigo drena com seus próprios ACKs. Registros novos entram no journal compacto em `/gps/tblite`, com as mesmas reservas de sequência. Em caso de rollback e retorno, o envio compara as cabeças dos formatos para manter a ordem. Quando a cota termina, preserva todos os registros já admitidos. Os últimos 64 KiB, ou 1/8 da capacidade em volumes menores, ficam disponíveis para operacionais depois que a saúde atinge o limite. Descartes novos são informados por classe e persistidos quando o meio aceita a gravação.
- O envio usa lotes de até oito registros e 8 KiB por requisição. Após sucesso há 1 s de pausa. O firmware aceita somente HTTP 2xx com números de registros e maior `seq` correspondentes ao lote. Falha de ACK, conexão ou gravação local deixa o lote para nova tentativa. Um lote recusado por HTTP 400/413 é sondado com envio unitário. O coletor continua gravando novas amostras enquanto ocorre o replay. A Cloud recebe o histórico em ordem de fila e o painel pode permanecer atrasado até alcançar as amostras recentes.
- Cada tentativa recria a conexão HTTPS e registra a etapa de falha. O timeout de socket é de 5 s e o orçamento externo do POST é de 15 s no código. A resolução DNS síncrona do ESP-IDF pode atrasar a devolução de controle ao cliente. O retorno de rota tenta antecipar o próximo envio. Em falhas repetidas, a espera aumenta de 2 a 30 s ou vai a 60 s para autenticação ou HTTP 429. Perda de IP ou falha na associação pode acionar recuperação de DHCP/Wi-Fi no Link, limitada a uma tentativa a cada 30 s. O coletor e o uploader rodam em tarefas distintas. Uma falha de Internet deixa a LAN disponível sem reiniciar o ESP32.
- Cada ciclo local permite até três tentativas curtas para falhas transitórias. `CORE_LOST` requer três ciclos perdidos. Mesmo boot depois da queda gera `CORE_RESTORED`. Boot novo gera `CORE_BOOT`. O primeiro contato gera `CORE_SEEN`. Se a LAN falhar ou as leituras expirarem no Core, o Link só consegue reter as amostras já recebidas. Se os dois ESPs reiniciarem offline sem hora confiável, os dados novos conservam boot e uptime como diagnóstico `DECODED` e ficam fora do cálculo financeiro.

O painel local do Link traz `telemetry_collector` em `/api/state?compact=1`. Esse bloco e o envelope de saúde expõem heap livre, mínimo e maior bloco do Link, heap mínimo do Core, identificação dos dois boots e motivos de reset disponíveis, RSSI e estado Wi-Fi, alcance da internet, status HTTP/Cloud, último ACK, desligamentos do vínculo e idade, ocupação, descartes e replay da fila. `internet_reachable=null` significa que uma rota está disponível, mas falta uma resposta HTTP recente que prove alcance. `cloud_connected=true` requer um ACK válido recente. As métricas antigas do coletor continuam disponíveis. [Journal](../../espidf/components/tb_link/tb_link_journal.c) · [Transporte e coleta](../../espidf/components/tb_link/tb_link_collector.c)

## Supabase

O formato HTTP continua compatível com `truckbox-ingest` v26. Na operação normal, a Cloud confirma persistência em `telemetry_samples` ou `health_minute_samples` com deduplicação por `(device_id,seq)`. A proposta de ingestão v26 e a proteção SQL usam tempo de captura para proteger `telemetry_latest`, `operation_state` e o vínculo com a viagem ativa. Uma amostra antiga diferente entra no histórico e o painel conserva os dados mais recentes. O teste inclui uma amostra antiga com sequência maior, uma duplicata e um registro sem hora válida. A reconstrução financeira de viagens já encerradas segue fora do replay. A proteção nova do Supabase só vale após revisão e implantação autorizada dos dois arquivos. A consulta de produção foi somente leitura. [Patch de ingestão](../../espidf/cloud/truckbox-ingest-lite-replay-v26.patch) · [Proteção SQL](../../espidf/cloud/telemetry_replay_review.sql) · [Reversão SQL](../../espidf/cloud/telemetry_replay_rollback.sql)

## Testes e memória

| Ensaio automatizado | Resultado |
| --- | --- |
| A — 15 min sem Internet | 105 registros, 34.906 B, 14 lotes e zero perdas ou duplicatas |
| B — 60 min com reinício/corte da cauda | 420 registros, 154.999 B, 60 lotes e zero perdas ou duplicatas entre as entradas gravadas |
| Extensão de B — 6 h com sensores preenchidos, reinício/corte da cauda | 2.520 registros, 931.916 B (910 KiB), 360 lotes e zero perdas ou duplicatas entre as entradas gravadas |
| C — 40 oscilações | 681 registros distintos na Cloud simulada, oito reofertas com os mesmos IDs, fila drenada |
| D / E — desligamento offline / após o ACK remoto | Recuperação de cauda e do checkpoint anterior. Mesmos IDs no retry quando a persistência remota antecedeu o corte |
| F — cota cheia com sensores preenchidos | Com 512 KiB, primeiro descarte de saúde aos 178 min e primeiro operacional aos 206 min. Com 1 MiB, saúde aos 380 min e operacional aos 405 min. Descartes persistidos e registros admitidos preservados |
| G — regressão e memória | Suíte de host passou com ASan/UBSan, com detector de leaks desabilitado no ambiente. J1708, multimídia local, TLS simulado e limitação do índice e buffers passaram. Falta medição de heap no Link ESP32-C6 sob carga real |

No ensaio de cota cheia, o maior quadro operacional ocupou 392 B em flash para 1.031 B de JSON. O maior registro de saúde ocupou 322 B para 1.188 B de JSON. Os ensaios comprovam retenção e reenvio em arquivos reais no host, com relógio confiável, LAN operante e lotes de até 8 KiB. Os 360 lotes de seis horas somam cerca de 6 min só nas pausas, além do tempo de HTTPS e do servidor. A duração física, os efeitos de desgaste e o tempo efetivo de recuperação no Starlink serão medidos no equipamento.

| Build ESP-IDF 5.5.2 | BSS DIRAM | DIRAM usada | Pilha de coleta | Pilha de envio |
| --- | ---: | ---: | ---: | ---: |
| Link 0.2.27 | 32.360 B | 165.144 B | 6.144 B | 8.192 B |
| Link 0.2.29 | 38.408 B | 171.192 B | 7.168 B | 8.192 B |
| Core Lite 0.2.27, mapa de referência | 60.712 B | 194.006 B | — | — |
| Core Lite 0.2.29 opcional | 60.712 B | 194.006 B | — | — |

O Link ocupa mais 6.048 B de DIRAM estática e mais 1.024 B de pilha de coleta. O índice de segmentos consome 2.720 B na simulação e a fila offline reside em flash. Há quatro posições transitórias na RAM para gravação e um POST de até 8 KiB. O uploader espera pelo menos 52.000 B livres e um bloco contíguo de 16.000 B para iniciar HTTPS. O heap mínimo de campo da versão nova e a latência online de 30 s após esvaziar o backlog seguem pendentes. [Resultados e tamanhos dos builds](TESTES.txt)

## Instalação e verificação

1. Em bancada com LAN e Internet controláveis, confirme a função, a versão, o UID do Core associado e o estado da fila em `/api/state?compact=1`. Guarde o estado inicial e o BIN anterior. Aproveite o mesmo UID, referência de viagem, registro de GPS, SSID e credenciais. Em homologação use um dispositivo e um destino separados da viagem oficial.
2. Use alimentação estável, com o caminhão estacionado caso a placa esteja nele. Entre no painel como administrador e escolha **Link, neste dispositivo** em **Atualização por Wi‑Fi**. Confira o SHA-256, selecione a imagem de aplicativo do Link e execute a validação e a atualização pelo painel. Aguarde o painel concluir antes de reiniciar. [Painel do Link](http://192.168.1.92/#ota-title)
3. Após o boot, confira `version=0.2.29-lite`, função `link`, `phase=ready`, `storage_ready=true`, `core_online=true`, evolução de `durable` e `last_durable_ack`, retorno de `cloud_connected=true` e registros recentes na Cloud de homologação. Anote heap mínimo e maior bloco, boot IDs, resets e perdas. O Core 0.2.28 pode permanecer instalado. Se o motivo de reset do Core também for necessário na saúde, o BIN opcional mantém e lê a política J1708 presente na NVS. A imagem genérica parte da política gravada. A validação inicial de uma placa de reposição continua sujeita à política J1708 dessa unidade.
4. Corte apenas o acesso à Internet por 15 min, mantendo a LAN e os sensores ativos. Confirme que `offline_queue_pending_records`, bytes usados e idade do mais antigo sobem sem aumentar descartes. Ao restabelecer o acesso, espere `pending=0`, ACK avançar e a idade voltar a zero. Compare no Supabase de homologação os IDs e `ts` das amostras antes, durante e depois. Conte registros por `(device_id,seq)` e compare a sequência de captura, a continuidade de consumo/distância e a posição de `telemetry_latest`.
5. Repita por uma hora e em oscilações. Em bancada, faça um ciclo de alimentação do Link offline e outro durante o replay. Registre filas antes e depois, tempo até o ACK, RAM e boot. Para testar fila cheia, use a simulação ou uma unidade de bancada com destino isolado. Um teste de 2 h com tráfego J1708, multimídia ligada e reconexões confirma que captura, requisições, heap e latência se mantêm. Registre `reset_reason` do Link e do Core em vez de deduzir reboot pelos eventos de desconexão.

## Rollback

Se possível, espere a fila drenar e copie o estado com `pending=0` e `last_durable_ack` antes de voltar ao Link 0.2.27 por OTA manual. Se precisar retornar com amostras pendentes do novo formato, mantenha NVS e FAT intactas. O Link anterior só lê `TBLT1`, e as amostras do journal compacto voltam a ser visíveis quando se reinstala a 0.2.29. Os dois formatos reservam sequências no mesmo `publisher1`. Substituição de partições, `erase_flash`, formatação, exclusão de pendências e retomada automática de OTA ficam fora deste procedimento. Se o Core opcional for instalado, o BIN anterior é o Core 0.2.28. [Link 0.2.27](../v0.2.27-revb1-lite/truckbox_link_0.2.27-lite_ota.bin) · [Core 0.2.28](../v0.2.28-revb1-lite/truckbox_core_0.2.28-lite_j1708_ota.bin)

Para homologar o lado Cloud, salve a função v26 original do ambiente autorizado e aplique a proposta sobre uma cópia em ambiente isolado. Confira que o SHA-256 da fonte normalizada é `329573d097b495ee19f44065ed7324928b301e42b7a01ba978c271370e7cabd3`, com quebra de linha única ao final. Teste o SQL junto às tabelas e triggers existentes. O rollback dessa parte restaura a v26 original e remove somente os dois guards adicionados pela revisão. A execução deste pacote ficou na branch, na compilação e na validação local. [Teste da Edge Function com mock](../../espidf/cloud/test_ingest_mock.mjs) · [Teste SQL isolado](../../espidf/cloud/test_lite_replay_sql.mjs)

## Reprodução

Fonte das imagens no commit `eb0ac8f87caefec1f9c3d4939606d407f86e5c3f`, com ESP-IDF 5.5.2 e alvo `esp32c6`. Instale a ferramenta para esse alvo. Execute em `firmware/espidf`.

```sh
idf.py -B build-link-lite-229 -DTRUCKBOX_ROLE=link -DTRUCKBOX_REVB1_LITE=ON \
  -DPROJECT_VER=0.2.29-lite build
idf.py -B build-core-lite-229 -DTRUCKBOX_ROLE=core -DTRUCKBOX_REVB1_LITE=ON \
  -DTRUCKBOX_REVB1_CONFIRMED_CORE_UID= -DPROJECT_VER=0.2.29-lite build
sh tests/run_all.sh
```
