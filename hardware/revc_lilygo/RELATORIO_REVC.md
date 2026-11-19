# Relatório de engenharia · TruckBox Rev.C

Data da verificação digital **19/11/2026**. Branch `codex/truckbox-revc-lilygo-s3`, a partir de `aff4696` (`fix/revb1-v0226-stack-ota-repair`). Essa base contém o Core/Link que será reaproveitado e a correção de pilha do diagnóstico. É a base de comparação do PR porque a linha `main` consultada não contém o firmware ESP-IDF da Rev.B.1.

## Decisões de projeto

| Tema | Resultado |
|---|---|
| Auditoria Rev.B.1 | Comparadas as branches de hardware e recuperação, os BOM/CPL e o PCB produzido pelos Runs 317/318. Conferidos também os identificadores dos pads U4, D4 e J4 nos Gerbers de cobre. Redes e posições constam em [Reaproveitamento](REAPROVEITAMENTO_REVB1.md) |
| Placa principal | H707 V1.1 N16R8. GPIO6 RX e GPIO7 DE por UART2. microSD por SPI2, modem por UART1. A revisão física deve corresponder ao PDF consultado |
| Perfurada | Grade 19 × 11 e aproximadamente 49,7 × 30 mm, com SN65HVD1781 a 3,3 V, filtro/bias da B1, D4 e JP1 de habilitação física. Base isolante e travessas para montagem sobreposta. Desenhos, mapa de fios e BOM disponíveis |
| J1708 | Com DI baixo o driver é dominante quando DE está ativo. A UART invertida em DE solta o par nos bits recessivos. RO fica ativo, com eco byte a byte, guarda de acesso, aborto, checksum e diagnóstico. A edição SAE J1708_201609 foi identificada. A conformidade integral com o texto normativo e formas de onda reais fica para a qualificação |
| Alimentação | USB-C da H707 e 18650 protegida de uma célula. A comutação original é a primeira candidata a ensaio. Se falhar, o Witty Pi 4 L3V7 é a primeira alternativa de bancada com berço independente e único carregador da célula. A continuidade da instalação depende das medições |
| Operação local e nuvem | Core e Link no mesmo alvo. HTTPS direto pelo contrato de ingestão existente, com Wi-Fi prioritário e PPP alternativo. AP e APIs locais da multimídia preservados. TCP 35000 continua no perfil RAW CAN legado. A Rev.C serve a J1708 pela API HTTP |

## Firmware entregue

- O alvo `TRUCKBOX_REVC_LILYGO_S3` usa dois slots OTA de 4 MiB, flash de 16 MB, PSRAM OPI e CPU a 240 MHz. O perfil passivo é o padrão. O perfil TX separado ainda exige topologia, configuração e autorização. JP1 permite desabilitar fisicamente o driver.
- A fila de persistência de 64 registros aloca aproximadamente 49 KiB de dados em PSRAM. UART, pilhas de tarefas e alocações com requisito interno usam memória compatível. `/api/state` registra memória interna, PSRAM e recuperação. A saúde usa o heap interno no alvo Rev.C.
- A telemetria usa o microSD integrado com confirmação de escrita, CRC, checkpoint de seq/ACK e retomada do cartão identificado. Os estados, viagens, contadores e PIDs permanecem no pipeline da Rev.B.1. O envio GPS opcional mantém o diário dedicado, além das posições agregadas ao registro operacional no SD.
- O SIM7670G usa comando AT para fix GNSS e PPP para dados. O modo CMUX permite usar os dois canais na mesma UART quando aceito pelo modem. Para ler o estado de alimentação, o firmware oferece `boot_id`, causa, uptime, eventos de boot e campos de fonte/tensão nulos até aferição.
- O destino OTA Rev.C exige o projeto `truckbox_revc_lilygo_s3`, ESP32-S3 e flash de 16 MB. Os alvos ESP32-C6 continuam compiláveis para Core e Link e com a política OTA de seu hardware.

## Verificações executadas

Os quatro builds concluíram em ESP-IDF v5.5.2 (`30aaf645`), GCC 14.2.0 das toolchains Espressif, CMake 3.30.2 e Ninja 1.13.0. O cabeçalho da imagem passiva foi lido com esptool e indicou ESP32-S3, flash 16 MB, versão `0.1.0-revc`, checksum e hash válidos. O sdkconfig efetivo confirma OPI, clock e TX bloqueado no perfil inicial.

| Build local | Aplicação | Tamanho em bytes |
|---|---|---:|
| Rev.C passivo | `truckbox_revc_lilygo_s3.bin` | 1.568.736 |
| Rev.C TX de bancada | `truckbox_revc_lilygo_s3.bin` | 1.572.624 |
| Rev.B.1 Core | `truckbox_core.bin` | 1.601.904 |
| Rev.B.1 Link | `truckbox_link.bin` | 1.476.480 |

- `firmware/espidf/tests/run_all.sh` passou, incluindo 34 testes Python, regressões C com sanitizadores, política de OTA, testes do painel e mocks de ingestão. O teste de UART exercitou C6, S3 passivo e S3 com TX. O teste Wi-Fi comprovou seleção e tentativa por PPP em falha de transporte no mock. O teste de GNSS cobriu coordenadas em graus/minutos e decimais, UTC e fix ausente.
- As simulações de armazenamento e viagem incluíram 7.200 s virtuais offline e trajetória de 300 km, com reboots e ACKs simulados. O caso de 300 km armazenou 1.207 linhas únicas no mock da nuvem com 1.208 entregas e terminou sem pendências válidas. Essa suíte exercita o motor de armazenamento e o contrato da Rev.B.1 reaproveitados. O barramento SPI e o cartão H707 precisam de ensaio físico.
- Conferidos 31 jumpers, nove redes, separação de TX e DE com JP1 aberto, quatro furos mecânicos, 32 itens da BOM e os checksums das mensagens de bancada. Os quatro SVGs foram renderizados. A conferência dos Runs 317/318 foi documental.
- O build e os testes usaram fontes locais, fixtures e emuladores de periféricos. Nenhuma unidade recebeu flash ou OTA e não houve ingestão na nuvem de produção.

## Pendências para liberação

| Ponto | Evidência a anexar |
|---|---|
| Revisão H707, suporte e polaridade | Fotos, espessuras, continuidade U5.15/16/10/11 e modelo de célula |
| Rev.B.1 doadora e chicote | Fotos, correspondência com o lote e continuidade das cavidades J4.4/6/5 |
| J1708 e proteção | Formas de onda em repouso, TX, eco, colisão e reset. Carga, modo comum, margem e transientes |
| Alimentação | Matriz de cortes, amplitudes/durações e medições nas três alimentações. Seleção da rota com zero resets |
| Endurance, armazenamento e viagens | Cartão real, ensaios de queda, contagem de registros, restauração de checkpoint e tempo prolongado |
| Rede, GNSS, multimídia e OTA | Versão do modem, CMUX, fallback com DNS, fluxo com credenciais de teste, atualização e rollback na bancada |

A liberação para montagem no Volvo depende dos resultados de [Testes de bancada](TESTES_BANCADA.md) e [Testes J1708](TESTES_J1708.md). A primeira conexão permanece passiva.
