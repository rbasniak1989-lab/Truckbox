# Pinagem LILYGO H707

A referência é **T-SIM7670G S3 V1.1, H707, ESP32-S3-WROOM-1-N16R8**. O PDF tem carimbos reaproveitados de outros projetos. Foram usadas as redes das folhas 1 a 4 e o mapa H707 do fabricante. O técnico deve identificar a revisão na serigrafia, comparar fotos da placa, conferir a orientação de U5/U6 e confirmar os quatro pontos antes de soldar. Se o PCB ou o módulo divergir, suspenda essa ligação e obtenha o esquema específico. [PDF oficial V1.1](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/main/schematic/esp32s3/T-SIM7670G-S3-V1.1.pdf) · [Guia H707](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD)

## Rabicho de quatro vias

| Ligação Rev.C | H707 | Rede da folha 2 | Direção no ESP |
|---|---|---|---|
| F04, J1.1 | U5 pino 16 | VCC3V3 | Alimentação U1 e bias |
| G04, J1.2 | U5 pino 15 | GND | Referência |
| H04, J1.3 | U5 pino 11 | GPIO6, UART2 RX | Entrada a partir de RO |
| I04, J1.4 | U5 pino 10 | GPIO7, UART2 TX invertido | Saída a JP1 e DE quando qualificada |

As duas trilhas GPIO6/GPIO7 na folha 2 vão do módulo aos headers. Esses pinos não têm função de boot e não são usados pelos periféricos da placa nua. Acessórios SimShield ou DC Shield podem ocupá-los. A Rev.C usa apenas a H707 e a perfurada. Conte o header pela identificação de V_BAT no U5.1 e pelo par GND/3V3 em U5.15/16. Confirme GPIO6/GPIO7 no esquema pela continuidade ao módulo quando os pads forem acessíveis. J1 usa as posições abaixo do desenho da perfurada, independentemente da cor do rabicho.

## Redes ocupadas

| Recurso | GPIO ou rede H707 | Nota |
|---|---|---|
| Modem UART1 | ESP RX10, TX11 | 115.200 bit/s. IO10 recebe TX do SIM7670G pelo tradutor. IO11 envia para RX do modem |
| PWRKEY | GPIO18 | Pulso alto no ESP aciona o transistor que puxa PWRKEY baixo no modem |
| Modem RESET e DTR | GPIO17, GPIO9 | RESET ativo baixo. DTR baixo mantém modem acordado |
| Modem RI | GPIO3 | Reservado. Também é strap do ESP |
| Antena GNSS | GPIO4 **do modem** | `AT+CGDRT=4,1` e `AT+CGSETV=4,1` habilitam GPS_VDD pela folha 3 |
| microSD SPI2 | SCK21, MOSI14, MISO47, CS13 | Alimentação VCC3V3 permanente. SPI com limite inicial de 10 MHz |
| USB ESP | GPIO19 D−, GPIO20 D+ | USB-C com USB Serial/JTAG. Porta de serviço do modem é separada no esquema |
| Flash / PSRAM | Barramento dedicado do módulo. GPIO35–37 ocupados na variante OPI | 16 MB QSPI e 8 MB OPI no N16R8. Reserve também GPIO26–34 |
| Boot e serial | GPIO0, 3, 45, 46 e UART0 GPIO43/44 | Preserve straps, RST/EN e UART de diagnóstico |
| LED | GPIO12 | Ativo em nível baixo |
| Bateria | GPIO4 ADC, VVBAT via R63/R62 de 100 kΩ | Leitura condicionada pela alimentação e sujeita a validação |
| Entrada de 5 V | USB-C, VBUS também em U6.16 | Use o USB-C. VBUS é 5 V, sem ligação ao J1 |
| V_BAT | U5.1 | Acesso ao domínio da célula conforme a revisão. Mantenha livre na Rev.C |
| GPIO5, GPIO8, GPIO15/16, GPIO1/2, GPIO38–42, GPIO48 | Outros pads ou funções de placa/acessórios | Mantenha sem ligações nesta montagem |

GPIO4 do ESP e GPIO4 do modem pertencem a domínios diferentes. O guia H707 informa que o ADC do ESP pode indicar zero em condição de USB presente. Q7/Q5 e as redes V_BAT/VVBAT variam entre revisões. A Rev.C deixa a tensão e a presença da fonte principal como desconhecidas no diagnóstico até existir leitura aferida na configuração escolhida. [Guia H707](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD) · [Datasheet ESP32-S3-WROOM-1](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
