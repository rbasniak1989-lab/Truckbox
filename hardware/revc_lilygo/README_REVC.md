# TruckBox Rev.C · LILYGO H707

A Rev.C reúne o Core e o Link em uma LILYGO T-SIM7670G S3 H707 com interface J1708/J1587 montada à mão, microSD, SIM7670G e uma 18650 de reserva. A alimentação principal entra pelo USB-C via carregador automotivo. O protótipo precisa passar pelos testes elétricos e de continuidade antes de uso no caminhão.

```text
Volvo J1708 A/B/GND ── rabicho ── J2 ── perfurada U1 ── J1 ── UART2
                                                                 │
Tomada do Volvo ── carregador 5 V ── alimentação USB-C          ESP32-S3
                                                                 ├─ microSD SPI · telemetria e recuperação
                                                                 ├─ Wi-Fi · Starlink e multimídia local
                                                                 └─ UART1 · SIM7670G LTE/PPP e GNSS
```

A 18650 fica no porta-célula da H707 ou no UPS externo, conforme a rota que passar no ensaio. Veja os dois caminhos em [Alimentação e bateria](ALIMENTACAO_BATERIA.md).

| Documento | Uso |
|---|---|
| [Pinagem](PINAGEM_LILYGO.md) | Identificar versão, headers, microSD e modem |
| [Esquema J1708](ESQUEMA_J1708.md) | Entender e medir a interface e adaptar o J4 |
| [Layout da perfurada](LAYOUT_PERFURADA.md) | Encontrar furos e ligar todos os jumpers |
| [Montagem sobreposta](MONTAGEM_SOBREPOSTA.md) | Montar base, espaçadores, isolante e caixa |
| [BOM](BOM_REVC.csv) e [reaproveitamento](REAPROVEITAMENTO_REVB1.md) | Separar peças e conferir a doadora |
| [Guia do técnico](GUIA_DO_TECNICO.md) | Montar, inspecionar, energizar e comissionar |
| [Testes de bancada](TESTES_BANCADA.md) e [J1708](TESTES_J1708.md) | Executar e registrar os ensaios |
| [Relatório](RELATORIO_REVC.md) | Consultar decisões, resultados e pendências |

## Firmware

O alvo `TRUCKBOX_REVC_LILYGO_S3` mantém o papel `core` no protocolo local. Ele reutiliza decodificação, diagnóstico, viagens, cálculo de combustível, agendador e checkpoints. A coleta é J1708/J1587. Continuam disponíveis leituras como RPM, velocidade, pressão, temperatura, pedal e transmissão, conforme os PIDs recebidos. PID 194 alimenta diagnóstico e PID 250 alimenta a reconciliação depois de validar a origem física/MID. O registro operacional usa intervalo inicial de 10 s e saúde de 60 s. A disponibilidade de dados depende da ECU e da qualidade da recepção.

O microSD FAT32 integrado é a mídia de telemetria. O log segmentado faz `fflush`, `fsync` e fechamento antes de contabilizar a gravação. Após falha, o armazenamento reconfere CRC, isola caudas inválidas e retém IDs e ACK em NVS. O replay mantém as pendências da mídia original. Uma fila RAM de 64 registros absorve interrupções curtas e reporta descarte quando fica cheia. O GPS preferencial é o SIM7670G via AT/CMUX e PPP opera como rota alternativa. Se o firmware do modem recusar CMUX, a sessão PPP simples pode operar e o fix do modem expira até voltar ao canal AT. Quando configurado, o envio ao endpoint separado de GPS reutiliza o diário `gps_spool` em flash. Os registros operacionais com GPS continuam indo ao microSD.

O envio do lote já usado por `truckbox-ingest` passa direto do ESP32-S3 à nuvem, usando o mesmo contrato HTTPS e confirmação `ack_seq`. Wi-Fi precede PPP na seleção de rota. Em perda de transporte no Wi-Fi, o firmware tenta PPP disponível para o mesmo lote. O AP/mDNS e `/api/live`, `/api/state`, diagnóstico e rotas do gateway atendem a multimídia com o contrato existente. O TCP 35000 emite frames RAW `TB1` de CAN em perfis legados. No perfil J1708-only aplica-se o contrato HTTP local, sem gerar frames CAN artificiais. O AP mantém serviço local quando resta só LTE. O compartilhamento de internet do AP continua associado ao uplink Wi-Fi. Nenhuma alteração na infraestrutura de nuvem é necessária para compilar.

## Compilar e gravar a unidade de bancada

Use ESP-IDF 5.5.2 com toolchain ESP32-S3. Os builds da Rev.B.1 continuam com os papéis `core` e `link`, ESP32-C6 e seus diretórios de build. Para a Rev.C, na raiz do repositório:

```sh
cd firmware/espidf
idf.py -B build-revc -DTRUCKBOX_REVC_LILYGO_S3=ON build
IDF_PATH=/caminho/esp-idf-v5.5.2 bash tests/run_all.sh
```

A primeira gravação da H707 inclui bootloader, tabela de partições e imagem. Com a unidade identificada e separada do equipamento em serviço, execute `idf.py -B build-revc -DTRUCKBOX_REVC_LILYGO_S3=ON -p PORTA flash monitor`. Substitua PORTA pelo dispositivo da H707. Os offsets ficam em `build-revc/flasher_args.json`. Para OTA posterior da mesma revisão, use apenas o arquivo de aplicação `truckbox_revc_lilygo_s3.bin` no painel local. A política de imagem valida projeto, ESP32-S3, flash de 16 MB e SHA-256 presente. Ela rejeita as imagens Core/Link de C6.

O build inicial deixa `CONFIG_TB_REVC_J1708_TX` desligado. Para qualificar a transmissão em bancada isolada, use diretório separado:

```sh
idf.py -B build-revc-tx -DTRUCKBOX_REVC_LILYGO_S3=ON menuconfig
idf.py -B build-revc-tx -DTRUCKBOX_REVC_LILYGO_S3=ON build
```

Em `Truckbox`, marque `Rev.C J1708 TX (bench qualification)` para esse build. Comece com JP1 aberto. Habilite TX fisicamente apenas no ensaio previsto. Um build com TX disponível ainda precisa de topologia validada, MIDs explícitos e configuração/autorização do agendador. Confira o [procedimento e critérios](TESTES_J1708.md). Na configuração nova, use os IDs dados pelo simulador ou medidos na rede de destino. O build de bancada só deve ser usado no veículo após registro dos resultados físicos e liberação da montagem.
