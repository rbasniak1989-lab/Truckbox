# TruckBox Core Rev.B.1 — relatório da candidata 0.2.26

Data: 15/11/2026. Base incremental: `233e7bceda92116748a053637b1b311ce45a0a80`, release final 0.2.25. Branch: `fix/revb1-v0226-wifi-build-contract`. Revisão: PR30. Hardware ESP32-C6, flash 8 MB, ESP-IDF 5.5.2, GCC RISC-V 14.2.0. As fontes e artefatos anteriores continuam preservados. Esta entrega não instala firmware, não ativa TX remoto e não modifica Cloud, configurações ou viagens do dispositivo.

## 1. Causa comprovada da perda de internet

A configuração extraída do ZIP 0.2.25 publicado contém `CONFIG_LWIP_IP_FORWARD` e `CONFIG_LWIP_IPV4_NAPT` desabilitados. O SHA de aplicação ELF `85e905c41485514e94bfc0cc099682d2c200f3e89bd8c5015baf171091342f0b` corresponde ao firmware observado no boot610. O ELF publicado não contém a implementação `ip_napt_enable_netif`. Nessa configuração a API IDF não disponibiliza NAPT; ter código de roteamento e defaults corretos não faz a BIN compartilhar internet.

O build local reutilizou `build-core/sdkconfig` antigo. Os defaults não substituíam seus valores negativos persistentes. A CI fazia um build limpo com NAPT habilitado, portanto seu sucesso não verificou a BIN local efetivamente publicada. Esse foi um erro na validação da entrega anterior.

Correção: `main/Kconfig.projbuild` seleciona encaminhamento/NAPT para o Core; `CMakeLists.txt` recusa configuração efetiva incompatível; `tb_network_contract.h` faz a mesma verificação durante a compilação. O código de rede inclui a marca `TBX_CORE_IPV4_FORWARD_NAPT64_V1` no binário. O empacotador verifica SDK efetivo e BIN. `tools/verify_shipped_core.py` e a CI auditam o ZIP e a OTA publicados separadamente do build limpo da CI, incluindo igualdade das duas OTAs, hashes internos, fonte limpa, rollback e tabela de partições.

O primeiro rebuild local desta candidata reutilizou a configuração problemática e produziu IP_FORWARD/NAPT habilitados. O novo ELF contém a implementação NAPT. Limite da tabela: 64 conexões; port mapping de entrada continua desabilitado. DNS, DHCP, AP/STA, detecção de sub-redes sobrepostas e reação aos eventos de Wi-Fi foram preservados e testados com mocks. `napt_active=true` significa que o roteamento foi habilitado, não que um site externo foi testado: `internet_verified=false` permanece explícito.

## 2. RAM e tarefas

Comparação dos ELFs reais, com a mesma IDF:

| Item | BIN publicada 0.2.25 | Candidata 0.2.26 | Diferença |
|---|---:|---:|---:|
| `.dram0.data` | 14.796 B | 14.796 B | 0 |
| `.dram0.bss` | 69.112 B | 67.080 B | -2.032 B |
| DRAM estática dessas seções | 83.908 B | 81.876 B | -2.032 B |
| `.iram0.text` | 127.966 B | 127.966 B | 0 |
| Reserva da fila TCP bruto | 8.448 B | 0 | -8.448 B |
| Pilha solicitada de `tb_tcp` | 4.096 B | 0 | -4.096 B |

A fila e a tarefa deixam de reservar **12.544 B (12,25 KiB)**, além dos custos de controle/socket não medidos. A economia estática de 2.032 B é medida pelo ELF; a reserva dinâmica removida é calculada das alocações RV32. Não se declara aumento equivalente do heap em campo: NAPT foi restaurado e também aloca memória. O teste de startup compila a função real nas opções TCP ligado/desligado e verifica que armazenamento, captura e bridge continuam iniciando. O ELF da candidata não contém `live_task`.

No perfil J1708_ONLY, cada entrada do cache retém até 17 bytes de parâmetro, máximo de um quadro J1708 de 21 bytes após MID/PID/comprimento/checksum. O teste usa PID194 com 17 bytes e confirma preservação integral. A quantidade de leituras continua 128; não foram reduzidas as filas de aquisição, armazenamento ou eventos. A montagem multisseção e o histórico de DTCs continuam no módulo existente, que recebe os dados antes da cópia ao cache. Outros perfis mantêm o campo de 32 bytes e o código J1939.

O endpoint `/api/live` reutiliza buffer de 1.536 B e transmite partes sem construir uma árvore cJSON. Os provedores de números usam formatação inteira, evitando dtoa/printf de ponto flutuante nesse caminho. Pilha estática indicada pelo GCC para o provedor principal: 1.456 B; isso não representa toda a cadeia de chamadas nem substitui a medição de high-water mark no hardware. O diagnóstico completo continua opcional, paginado quando necessário.

Não há outra tarefa não utilizada comparável que possa ser excluída com segurança neste Core. `tb_j1708`, `tb_j1708_tx`, `tb_decode`, `tb_store`, `tb_bridge`, `tb_snapshot` e `tb_trip` fazem aquisição, polling, decodificação, persistência, sincronização e checkpoint. Wi-Fi/lwIP/HTTP sustentam internet, comunicação e OTA. CAN já não inicia neste perfil. GPS/modem são tarefas do firmware Link, que não foi alterado; desativá-las no Core não liberaria sua RAM. Não foram diminuídas pilhas essenciais por suposição.

## 3. Leituras para a multimídia

Vetores extraídos dos registros já existentes do boot610, usados em testes de software:

| Sinal | Origem | Bytes | Resultado | Qualidade |
|---|---|---|---|---|
| Marcha atual | MID130/PID163 | `31 32` | 12 | EM_VALIDACAO |
| Seletor | MID130/PID162 | `20 41` | A, automático; marcha numérica nula | EM_VALIDACAO |
| Torque absoluto | MID128/PID93 | `20` | 867,2 Nm | EM_VALIDACAO |
| Torque percentual | PID93 + referência histórica | `20` e 2643 Nm | aproximadamente 32,8112% | DERIVADO_REFERENCIA_HISTORICA_EM_VALIDACAO |
| Pressão de alimentação | MID128/PID94 | `81` | 4,4505 bar | EM_VALIDACAO |

2643 Nm é a correção do Documento Mestre J1939 v1.1 para o perfil histórico, não uma nova leitura da ECU nem prova externa atual. A referência antiga de 2600 Nm não é usada. A derivação só é publicada para `tb-c6-d40592b9190c`; outro dispositivo recebe percentual nulo sem uma referência comprovada. PID92 continua carga do motor e não substitui torque. TTLs: marcha 5 s, pressão 15 s, torque 65 s, conforme a atualização existente. Falta, indisponibilidade e dados antigos produzem nulos; MID, bytes e idade continuam acessíveis em `live_readings`.

PID93 mantém a cadência existente de 60 s. Não se promete percentual de torque atualizado a cada segundo; a candidata apenas disponibiliza corretamente a leitura e sua qualidade. As demais leituras passivas existentes permanecem no cache/API, e o endpoint limitado oferece também temperaturas, pressões, carga, rotações e VEB disponíveis.

## 4. Polling e pressão de combustível

Somente PID94/MID128 mudou de 30 s para **10.000 ms**. Continua na mesma fila e recebe prioridade compatível com a pressão solicitada. Os demais MIDs, bytes e períodos permanecem inalterados, inclusive PID117/MID140, PID118/MID140 e PID250/MID128 em 10 s.

| PIDs | MID | Período normal | Durante arrancada |
|---|---|---|---|
| 31 | 130 | 10 s | 2 s |
| 33 | 130 | 5 s | 2 s |
| 34, 63 | 130 | 15 s | 5 s |
| 32, 160 | 130 | 30 s | 10 s |
| 37 | 130 | 30 s | 15 s |
| 117, 118 | 140 | 10 s | 10 s |
| 250 | 128 | 10 s | 10 s |
| 94 | 128 | 10 s | 10 s |
| 161 | 130 | 10 s | 10 s |
| 153 | 128 | 30 s | 30 s |
| 26, 68, 93, 108, 158 | 128 | 60 s | 60 s |
| 62 | 144 | 60 s | 60 s |
| 107, 154, 182, 244, 439 | 128 | 300 s | 300 s |
| 235, 236, 188 | 128 | 900 s | 900 s |

As metas não autorizam rajadas: continuam uma requisição pendente, pelo menos 500 ms entre transmissões, timeout 450 ms e arbitragem/barramento livre. A soma de metas durante arrancada pode exceder duas por segundo; o scheduler adia parâmetros secundários. Respostas continuam candidatas correlacionadas por MID/PID/timestamp, nunca promovidas a validação elétrica. Política NVS, MID atual, recuperação controlada e separação entre operação automática e ensaio manual foram preservados. A tarefa não altera integrações PID183/PID250, limites de validação, âncoras ou persistência de viagem.

## 5. Multimídia e viagem em andamento

O APK `0.3.1-core-live` mantém aplicação e navegação existentes. Prefere `/api/live`; aceita os endpoints anteriores quando trazem instrumentos, mas não trata um diagnóstico parcial antigo contendo só RAM como telemetria completa. Isso evitava que a aplicação buscasse contexto da Cloud ao receber HTTP200 com somente contadores.

Instrumentos do Core permanecem a fonte local. Contexto da viagem é consultado independentemente na infraestrutura read-only existente, no máximo a cada 10 s; falhas PID194 são consultadas a cada 30 s, em páginas de até oito entradas, até 32 posições. A falta de internet não impede as leituras locais. O parser passa a aceitar o nome real `trip_fuel_l`. Só utiliza dados da operação ativa/EMPTY_MODE; não transforma totais de vida do Core ou viagens históricas em uma viagem atual. Uma consulta sem viagem ACTIVE não comprova apagamento de viagem: pode ser modo vazio ou contexto ainda não sincronizado.

A tela existente de diagnóstico mostra MID, PID/SID/PPID/PSID, FMI, estado explicitamente informado pela ECU, contador opcional inclusive zero, bruto e último momento conhecido. Descrições Volvo mantêm classificação candidata e desconhecidos são preservados. Sumir da próxima mensagem não resolve uma falha. O cache em memória é limitado a 32 códigos e isolado por UID; esta alteração não cria um histórico OEM completo nem envia comando de limpeza.

Não houve deploy na Cloud. A Edge Function de ingestão v26 consultada ainda utiliza a projeção legada `SPN183_RATE_CLOUD_V1`; a migração financeira/reconciliação está preparada separadamente e não foi aplicada. O Core mantém sua contabilidade canônica e qualidade. Não se declara que atualizar esta BIN migra médias históricas da Cloud.

## 6. Testes executados e limites

Evidências em `test_evidence/v0.2.26/`; logs completos da CI ficam ligados no PR.

- Suíte host completa: base/configuração, DHCP/NAT/DNS, crash/HTTP/JSON, parsers J1939 e equivalência Python/C, J1587 passiva/ativa, enquadramento UART, RX guard, colisões/timeout/eco, política nativa, PID194/multisseção, viagens/consumo, armazenamento/CRC/recuperação/ACK e fairness: passou.
- PID94 em 10 s/MID128, limite global e resposta candidata correlacionada: passou. PID117/118/250 preservados, inclusive timeout de um PID sem bloqueio global.
- Vetores reais de marcha/torque/pressão, valores ausentes, referência por UID, stale e zero heap cJSON na leitura limitada: passou. Testes também injetam falhas em cada alocação do estado de telemetria/adapter JSON.
- Diagnóstico host com orçamento de campo 41.596 B: resumo compacto com pico de 13.665 B incluindo reservas simuladas de upload/checkpoint, 200 repetições, zero vazamentos. Essa medição isola o diagnóstico de captura/bridge; não representa o heap total da placa. O provedor real de sensores também tem teste próprio de todas as alocações.
- Testes do painel em navegador: fluxo de autenticação, configuração, OTA, manutenção e controles existentes: passaram. Nenhuma chamada foi feita à placa.
- Duas horas offline em relógio virtual com tráfego equivalente a 443.209 quadros e 10.249 requisições/candidatas: passaram sem gravação bruta contínua. Com relógio UTC, 4.341 registros ocuparam 1.803.282 B de orçamento 1.818.624 B, 37 segmentos, 14 falhas/reboots simulados; 4.349 entregas geraram 4.341 registros únicos no mock Cloud, sem duplicação nem pendências válidas ao final. Sem UTC: 3.909 registros, 1.631.250 B. Evidência isolada permaneceu preservada.
- Simulação nominal 300 km: 299,98172160 km amostrados, 59,81502658 L canônicos, três reinícios POWERON/TASK_WDT/OTA, duas horas offline, MID188 preservado, 1.207 registros únicos após 1.208 entregas; sem parada global do polling. São reinícios modelados, não resets provocados no caminhão.
- Build real Core ESP32-C6/IDF5.5.2 e CI independentes: passaram. Build/testes Android Motorista e Multimídia: passaram; a CI guarda também os XMLs dos testes. Os IDs e os hashes finais estão no diretório da release.
- Empacotador recusa configuração sem NAPT e BIN sem marca mesmo se o SDK alegar habilitação; a auditoria também recusa OTA avulsa diferente da contida no ZIP.

O cenário offline de duas horas tem margem pequena: 15.342 B no caso UTC. Evidência corrompida preservada, filas prévias ou tráfego diferente reduzem a autonomia disponível; não é promessa irrestrita de duas horas em qualquer volume já ocupado. Esta candidata não altera armazenamento ou partições para esconder esse limite. microSD/FRAM/4G continuam sem habilitação nova.

Ainda pendem observações físicas após autorização: internet real da Starlink atravessando o AP, leituras na multimídia instalada, heap/high-water em viagem, condições elétricas e reset da placa. Os testes host não constituem prova elétrica TX. A correção da causa de todos os reinícios não é declarada: não há novo panic dump ou medição de EN/tensão nesta tarefa. O único defeito novo de conectividade foi comprovado nos bytes da entrega anterior e corrigido no build.

## 7. Arquivos principais e entrega

`main/Kconfig.projbuild`, `CMakeLists.txt`, `tb_base/network.c`, `tb_network_contract.h`, `tb_web/tb_web.c`, `tb_core/core.c`, `telemetry.c`, `live_fields.inc`, `live_number.h`, interfaces em `include/`, tabela/prioridade J1708, scripts de release/auditoria, testes nativos, workflows CI e adaptadores/testes da multimídia. O diff completo está na release. Wi-Fi continua com as credenciais salvas; não há nova senha, GPIO ou ligação física.

Pacote contém fontes completas, BIN Core OTA, bootloader/partições para recuperação manual, símbolos, sdkconfig efetivo, manifesto e SHA256, ferramentas Windows, evidências e rollback Core0.2.24 auditado. Não há nova BIN Link. Consulte `ATUALIZACAO_v0.2.26.md` para atualização e limitações do rollback.
