# TruckBox Rev.B.1 v0.2.0-wdt1 — VERSÃO DE TESTES

Correção do watchdog observado em campo na v0.2.0. Leia `WDT_FIX_REPORT.md`.
O Core limita os lotes do decoder e do gravador para deixar executar IDLE/USB.
A confirmação final de Wi-Fi e operação sustentada depende de novo ensaio na placa.
A versão anterior permanece arquivada, sem sobrescrita.

Firmware derivado do fonte real de `truckbox-b1.zip`, branch funcional
`firmware-revb1-ota-v0.1.0-b1`. Projeto ESP-IDF5.5.2, ESP32-C6-WROOM-1, flash8MB.
A release v0.1.0-b1 permanece intacta. Nenhuma placa foi gravada e não foi feito merge.

## Conteúdo e estado

- Core e Link compilam separadamente, preservando projetos `truckbox_core` e `truckbox_link`, partições e protocolos legados.
- J1939: catálogo76/29PGNs, **55 decodificações físicas/enums/grupos**, 15 parciais/brutas e6 pendentes/indisponíveis. Consulte `../j1939/J1939_COVERAGE.csv` e `../j1939/IMPLEMENTACAO_v0.2.0.md`.
- TP.BAM passivo para EC1/RC de30/19bytes; CAN clássico continua DLCmáximo8.
- J1587: todos os48PIDs passivos e17PIDs solicitáveis, com unidades, bytes e EM_VALIDACAO. Os17 estão no catálogo inicial; transmissão inicia desarmada e sem MIDs presumidos.
- Coleta bruta e decodificada persistente, referência monotônica/boot/seq, GPS opcional sem posição fictícia. MicroSD usa os mesmos pinos e gates da base, sem formatação automática. Flash interna é fallback explícito, não microSD fictício.
- TCP35000 `TB1`, Wi-Fi, setup, APIs legadas, ACK/replay, OTA autenticada, FRAM, GNSS e modem do Link preservados. Alterações de preferências de fonte e consumo canônico são documentadas.
- APIs novas `/api/sensors`, `/api/sensors/catalog`, `/api/j1708/requests`; dados versionados no formato2.

## Limites concretos

A validação física de transceivers, alimentação, microSD, forma de onda TX,
colisões reais e funcionamento prolongado exige a Rev.B.1. Não existe porta USB
serial da placa neste ambiente. Os ZIPs originais dos Logs Mãe não estão aqui;
as novas referências foram lidas, e os testes comparam vetores construídos a
partir delas com o parser Python. Não foi reprocessado o conjunto de milhões
de frames dos Logs Mãe.

A Cloud atual aceita GPS ausente e conserva o payload em `raw`, mas seu ingest
v24 trata RAW/DECODED como operacional e pode esvaziar o estado do painel.
A correção local em `cloud/` precisa ser revisada, testada e implantada antes de
usar o gateway antigo com a nova fila de registros. Não foi implantada aqui.
Não afirmar integração Cloud de ponta a ponta validada enquanto isso estiver pendente.

## Compilar, testar e empacotar

Mantenha a estrutura extraída `firmware/espidf`, `firmware/j1708_17pid` e
`firmware/j1939`: os módulos irmãos são dependências do projeto, não opcionais.
Ative ESP-IDF5.5.2 e siga `BUILD_ENVIRONMENT.md`:

```sh
cd firmware/espidf
bash tools/build_v020.sh all
sh tests/run_all.sh
python tools/build_release.py --output-dir release --package Truckbox_B1_firmware_0.2.0-wdt1_TESTES.zip
```

O pacote contém fontes completos, Core/Link `.bin`, bootloaders, tabela de
partições, dados OTA, manifesto/hash e ferramentas Windows. Compilar e empacotar
não gravam dispositivos. Para atualização manual e recuperação, leia
`UPGRADE_RECOVERY.md`. Para testes ativos leia `ACTIVE_J1708.md`.

Os resultados executados e pendentes estão em `TEST_RESULTS.md` e `PENDING.md`.
