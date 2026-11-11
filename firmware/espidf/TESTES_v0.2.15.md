# Testes — Core Rev.B.1 0.2.15 candidata

Executados em 11/11/2026, Linux, ESP-IDF **5.5.2**, compilador RISC-V fornecido pelo IDF, ESP32-C6/flash 8 MB. Nenhuma conexão à placa, transmissão J1708 real, gravação OTA/USB, reset ou alteração em produção. Leituras da Cloud são históricas do boot 290, não testes da candidata instalada.

## Execução e evidências

```sh
# Ativar a instalação ESP-IDF 5.5.2 antes destes comandos.
bash firmware/espidf/tools/build_v020.sh core
sh firmware/espidf/tests/run_all.sh
# Playwright/Chromium devem estar instalados para este teste local.
node firmware/espidf/tests/web/test_flow.cjs
```

A suíte principal concluiu com exit 0, sanitizadores AddressSanitizer/UndefinedBehaviorSanitizer nos testes nativos e 24 testes Python aprovados. Inclui `run_store_transactions.sh`, `run_store_format.sh`, `run_j1708_active.sh`, `run_legacy_profile.sh`, `run_capture_integration.sh`, `run_telemetry.sh`, `run_trip.sh`, `run_worker_fairness.sh`, ensaio de campo indireto, J1939, UART, transporte Wi-Fi do Link e teste offline. As alterações finais do painel passaram novamente nos dois testes Python específicos e nos **21 cenários do navegador**; não houve modificação funcional posterior do controlador testado.

O teste local da função de ingestão v26, com Supabase/Deno/banco substituídos por mocks, passou com o novo GUARD_DECISION: payload preservado, ACK correto, nenhuma atualização de viagens, consumo ou `telemetry_latest`. Esse teste usa a cópia privada já disponível da função; ela não integra o pacote público por conter dados de autenticação. O executor e o novo caso de regressão estão no repositório. **Não foi feito deploy nem envio sintético à produção.**

Logs sem credenciais reais: `test_evidence/v0215_host.txt`, `v0215_build_core.txt`, `v0215_browser.txt`, `v0215_browser.json`, `v0215_cloud_mock.txt` e `v0215_panel.txt`. Falhas encontradas durante desenvolvimento foram corrigidas antes dos resultados finais: expiração de autorização não deve ser confundida com falha elétrica travada; teste do navegador agora aguarda a atualização da fase, em vez de supor que a mensagem de aceite já contém o diagnóstico novo.

## Matriz solicitada A–J

| Caso | Verificação executada e resultado |
|---|---|
| A — tráfego estável com pedidos | Controlador real, relógio/barramento mock, ~60 quadros/s, agendamento dos 17 PIDs. Permanece armado e em viagem; teste de campo completo verifica separadamente as três rodadas e autorização. PASS |
| B — checksum isolado | Pico sintético de seis inválidos e recuperação limpa. Não desarma; testes mantêm checksum separado de falha TX. PASS |
| C — variação de taxa | 60 → 5 → 120 → 25 quadros/s, mantendo tráfego válido. Taxa isolada não desarma. PASS |
| D — falhas persistentes/TX | Três janelas com 40 erros interrompem; falhas UART persistentes interrompem; overrun durante TX libera imediatamente; eco divergente libera TX e conta divergência, conservando política de repetição. Falha RX não inventa colisão/falha de backend. PASS |
| E — timeout repetido | Três pedidos sem resposta candidata e três falhas de backend disparam as proteções existentes; diagnóstico identifica a categoria/PID. PASS |
| F — parado/ligado/movimento | Variação de tráfego no controlador e testes existentes de movimento/velocidade recente: ensaio estacionado continua bloqueando movimento; modo de viagem autorizado não exige interação enquanto dirige. Estados e velocidades são simulados. PASS |
| G — internet | Upload mock falha/retoma sem alterar autorização do controlador; testes reais do transporte Link com HTTP mock e offline/ACK complementam a independência. Não é ensaio de RF/roteador no caminhão. PASS |
| H — reboot | Inicialização do controlador volta desarmada, sem trabalho; não restaura autorização. Testes de captura/NVS e recuperação verificam persistência separadamente. PASS |
| I — resposta espontânea | MID/PID igual dentro da janela pode contar uma candidata; segunda contagem é rejeitada; MID errado não pareia; nenhum teste transforma o resultado em comprovação externa. PASS |
| J — baseline antigo | `before_frames` histórico alterado para 1/UINT32_MAX não afeta a proteção atual. Referência atrasada é descartada/reaprendida. PASS |

Também passaram: relógio começando em zero, reversão do monotônico bloqueando TX, referência de três janelas contíguas, janela atrasada não contada como três janelas, trava de falha sem rearme após recuperação, manutenção das cadências/MIDs e de 1,2 s entre pedidos.

## UART e diagnóstico — código real exercitado

O runner de integração extrai as funções efetivas `j1708_task` e `finish_j1708` do fonte e liga os protocolos/decodificadores reais. Mock UART entrega eco fragmentado, quadro dividido entre leituras/idle, checksum inválido, excesso/agrupamento de bytes, MID errado, mensagem espontânea e FIFO overflow durante TX. Verificados: eco próprio não vira MID conflitante, CRC/enquadramento ficam separados, candidato não duplica, TX para quando perde RX e recepção passiva continua. Não se altera o algoritmo de particionamento para obter respostas artificiais.

O teste de auditoria extrai a serialização real. O registro guard representativo ocupa **522 bytes**; fixture de limites conservadores de contagem/uptime ocupa **731 bytes**, abaixo de 767. Falha em cada ponto de alocação cJSON foi injetada: arrays não perdem posições, não há vazamento e evidência incompleta não é gravada como sucesso. Perda de auditoria do caminho indireto mantém o bloqueio.

O teste inclui uma cópia imutável apenas de `healthy()` da 0.2.14. Uma janela **sintética** de 599 válidos/6 inválidos satisfaz o ramo antigo e interrompe; a candidata não interrompe por esse pico isolado. **Não é replay do instante real das 20:02:34.** Os contadores de dez segundos necessários não existem no histórico persistido.

## Duas horas offline e retorno

Teste virtual de 7.200 s, 433.624 quadros de entrada, 65 sensores, 1.624 pedidos e 1.624 respostas candidatas; zero gravações RAW contínuas. Exportador usa os construtores reais de OPERATIONAL/HEALTH/eventos. Persistência/codec/ACK exercitados em camada separada, com 14 reinicializações simuladas e retransmissões. A autorização do controlador não foi restaurada por esses reboots da camada de armazenamento.

| Cenário | Registros | Bytes físicos | Orçamento | Perda por lotação | Cloud mock após retorno |
|---|---:|---:|---:|---:|---|
| Sem UTC | 5904 | 1655121 | 1818624 | 0 | 5904 únicos, 5912 entregas com retries, 0 duplicados, 0 válidos pendentes |
| Com UTC | 5904 | 1732153 | 1818624 | 0 | 5904 únicos, 5912 entregas com retries, 0 duplicados, 0 válidos pendentes |

Os arquivos de evidência isolados após ACK foram preservados (685.617/686.537 bytes), como no código anterior; ocupam espaço em cenários futuros até revisão explícita. O teste não autoriza apagá-los e não promete duas horas adicionais quando a partição real já estiver ocupada. Não houve mudança no armazenamento nesta versão.

## Compilação, RAM e imagem

Build real do **Core**, ESP32-C6, ESP-IDF v5.5.2, aplicação 0.2.15, flash 8 MB, perfil J1708_ONLY. Aplicação cabe em OTA de 3.145.728 bytes. Tabela de partições continua com SHA256 `5e25baf13060f871fb6e9103f6cec4000a6c0e36bdf4d554fa037a26f3f91832`. Valores/hash finais estão em `test_evidence/v0215_image.json` e no manifesto do pacote.

Medição dos ELF/DWARF da 0.2.14 e candidata:

| Item | 0.2.14 | 0.2.15 | Diferença |
|---|---:|---:|---:|
| RAM estática, incluindo IRAM | 202148 | 202580 | +432 B |
| Estrutura do controlador | 688 | 1120 | +432 B, já incluídos acima |
| Item da fila de eventos | 232 | 392 | +160 B |
| Fila, 16 itens | 3712 | 6272 | +2560 B no heap |
| Acréscimo conhecido total | — | — | **2992 B** |

Não houve economia de RAM: o acréscimo guarda contadores independentes e o snapshot fiel. A coleta RX/TX continua sem alocações cJSON, e a gravação não ocorre no lock de tempo crítico. Heap livre mínimo, pilhas e latência na **placa candidata** ainda não foram medidos; o mínimo histórico de 28.564 B do boot 290 não substitui essa medição. Não se declara ausência de regressão física de memória com base apenas em ELF/testes host.

O Link não recebeu nova imagem; sua compatibilidade é coberta pelos testes existentes e pelo protocolo inalterado. Fontes J1939, escalas, MIDs/cadências, partições, codec, armazenamento/NVS, OTA e release funcional v0.1.0-b1 permanecem preservados.

## Pendências reais para aprovação/ensaio

- Não há captura externa simultânea nem medições elétricas da Rev.B.1. O diagnóstico continua indireto/candidato.
- Não se conhece o ramo real do OR antigo nem a causa física dos inválidos do incidente. Não há ZIPs completos dos Logs Mãe reprocessados nesta tarefa.
- Política estatística/persistência é proposta de engenharia documentada; não é certificação SAE nem ajuste comprovado em todas as condições de condução.
- Testar heap/pilha, resposta manual de parada, fluxo Cloud/ACK e novos eventos na placa **somente depois da aprovação**, inicialmente estacionado. Não autorizar TX apenas porque 17/17 candidatas apareceram.
- Nenhuma alteração foi feita para o problema separado de CAN/J1939.

Procedimento/rollback estão na [auditoria](REV_B1_0.2.15_AUDITORIA.md). Pacote é candidato para revisão; não foi implantado.
