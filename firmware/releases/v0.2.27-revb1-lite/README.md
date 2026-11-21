# TruckBox Rev.B.1 Lite 0.2.27

Pacote para homologar Core e Link no Volvo FH 540 2013. Use a ordem de instalação, a validação segregada e o procedimento de rollback do guia antes de ativar a publicação. [Guia de instalação e arquitetura](../../espidf/REVB1_LITE.md)

| Arquivo | Uso |
| --- | --- |
| `truckbox_core_0.2.27-lite_ota.bin` | OTA manual do Core ESP32-C6 existente. Aquisição J1708 e multimídia local |
| `truckbox_link_0.2.27-lite_ota.bin` | OTA manual do Link ESP32-C6. Coleta, persistência e HTTPS |
| `Truckbox_Multimedia_0.3.2_RevB1_Lite.apk` | Aplicativo de oficina ao lado do já instalado. Selecionar o host do Core após instalar |
| `rollback/truckbox_core_0.2.24_ota.bin` | Arquivo 0.2.24 publicado, preservado byte a byte |
| `rollback/truckbox_link_0.2.24-rebuild_ota.bin` | Link legado compilado da base 0.2.24. Confira a versão física e prefira o slot anterior já conhecido |
| `MANIFEST.json`, `SHA256SUMS.txt` | Proveniência, arquivos e verificação de integridade |
| `evidence/` | Logs de teste, partições, perfis de compilação e tamanhos dos builds |

Na pasta extraída, execute `sha256sum -c SHA256SUMS.txt`. As imagens de app foram verificadas como ESP32-C6 com o papel correspondente e cabem nos slots de 3 MiB. O procedimento de instalação preserva o mapa OTA existente de 8 MiB, configurações e credenciais.

## Comparação de memória e tarefas

Comparação de builds ESP-IDF 5.5.2. A referência 0.2.24 foi reconstruída a partir de `2ef6b46` para o mesmo ESP32-C6. DIRAM estática inclui dados, BSS e código colocado nessa região. Os valores do mapa de linkedição não medem heap livre em execução.

| Build | BSS DIRAM | Dados DIRAM | DIRAM usada | Imagem de app |
| --- | ---: | ---: | ---: | ---: |
| Core 0.2.24 | 68.792 B | 14.796 B | 211.554 B | 1.591.184 B |
| Core Lite | 60.712 B | 14.504 B | 194.006 B | 1.435.088 B |
| Link legado, reconstrução 0.2.24 | 31.512 B | 14.520 B | 164.086 B | 1.475.088 B |
| Link Telemetry | 32.360 B | 14.512 B | 165.144 B | 1.495.008 B |

- **Core** libera 17.548 bytes de DIRAM estática e retira a fila de persistência de 64 itens × 776 bytes, ou 49.664 bytes de conteúdo além da estrutura FreeRTOS. `tb_store` (9.216 B), `tb_bridge` (10.240 B), `tb_snapshot` (6.144 B) e `tb_trip` (3.072 B) saem do perfil Lite. `tb_supervise` ocupa 2.048 B, uma redução líquida de 26.624 bytes de pilhas configuradas. A fila de eventos detalhados de TX também sai. `tb_decode`, `tb_j1708` e `tb_j1708_tx` mantêm as reservas anteriores. O ELF Lite contém `snapshot_handler` e `j1708_task` e dispensa `storage_task`, `tb_store_init` e `bridge_task`.
- **Link** ganha `tb_lite_poll` (6.144 B) e, se ativado, `tb_lite_https` (8.192 B). GPS, modem e relatório local continuam no Link. A alocação TLS é transitória e espera uma margem de memória. O estado de observação pode ser usado para validar o vínculo antes de habilitar as duas tarefas de publicação.
- A remoção de cópias e serialização foi verificada no código, ELF e teste de snapshot com alocador cJSON indisponível. A capacidade de execução com Wi‑Fi, multimídia e barramento ativos deve ser medida na placa. O mínimo de 428 bytes relatado no equipamento 0.2.24 é uma observação de campo, sem medição equivalente da candidata.

## Evidência dos ensaios

- Build e inspeção dos quatro binários pelo esptool, comparação do SHA da tabela de partições e conferência dos perfis no `sdkconfig`.
- Testes host de snapshot com storage indisponível, cache com/sem leitura recente, OOM, troca de boot ID, hora inválida e retomada. Testes da fila com ACK inválido/duplicado, retomada, final incompleto, backup de compactação e cota. Testes de reservas de sequência, PID 250 ausente e regressão de contador. Os testes host exercitam C real e substituem acesso a hardware por stubs ou arquivos. O ambiente local permite ASan/UBSan, com detecção de vazamentos desabilitada para executar os scripts que a forçavam.
- Regressões de UART, guarda, eco verificado de MID 172, ausência de consentimento para TX, falha de armazenamento e ensaios sintéticos do sistema legado. O log informa quais casos usam stubs ou reprodução de capturas.
- Testes Android passaram no GitHub Actions, incluindo perda de Internet com sensores locais, timestamp de captura antigo apesar de recebimento recente e distinção do PID 250 de uma estimativa. O APK foi conferido pelo hash do artefato e pelo manifesto compilado. [Execução do GitHub Actions](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/44943369702)

## Aceite ainda necessário

Priorize confirmação do vínculo pelo UID, latência efetiva de envio até 30 s, perda/reconexão do Starlink, duas horas de atividade, heap mínimo e maior bloco, alimentação na partida e resposta física J1708 em bancada. A Cloud foi consultada em leitura. A publicação em produção começa após a homologação do par e a ativação pelo proprietário. A Rev.B.1 ainda depende da integridade de J4, EN, watchdog e chaves de alimentação. A versão anterior do Core e o Link de serviço permanecem disponíveis no rollback.
