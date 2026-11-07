# Rev B1 — conferência física

A tabela usa **pinos físicos do ESP32-C6-WROOM-1**, não os do MINI. Confirme o arquivo efetivamente fabricado, a orientação e o pino 1 antes de qualquer retrabalho.

## Bloqueios identificados

| Circuito | Evidência e efeito | Tratamento |
|---|---|---|
| EN do Core | Medidos 3,3 V em TP1 e 1,6 V em TP5/EN; TP5–U1/3 tem continuidade. | Investigar a carga. A 3,3 V, a liberação de reset exige aproximadamente 2,48 V no mínimo. |
| U8/TPL5010 | Footprint espelha 1↔3 e 4↔6. | Isolamento temporário por técnico SMD testa sua influência em EN; o reparo exige refazer ligações. |
| Q1/Q2 | Footprints espelham 1↔2 e afetam gate/source. | Corrigir e conferir as alimentações do Link e microSD. |
| Q3/Q4 | Mesmo espelhamento 1↔2 no ACC/watchdog. | Rever o funcionamento do conjunto, não apenas U8. |
| J5/SIM | C5/GND não recebeu net nos arquivos revisados. | Medir continuidade do contato GND do SIM até GND da placa e corrigir se ausente. |

O bootloader USB respondeu apesar de EN em 1,6 V. Isso confirma aquela comunicação, mas não garante operação sob carga ou em novos reinícios.

O firmware mantém GPIO1 do Core como entrada e não aciona GPIO16/17 por padrão. A opção **Acionamento de alimentação corrigido** libera essas saídas; marque somente após retrabalho e medições.

Esta versão **não gera pulsos DONE para U8**. Depois de reparar o watchdog, sua habilitação exige implementar e validar esse atendimento no firmware. Um TPL5010 corretamente alimentado, sem DONE, pode reiniciar o Core periodicamente; isso deve ser considerado no comissionamento e no teste de OTA.

## Pinagem usada

| Função | Módulo | GPIO | Pino WROOM |
|---|---|---:|---:|
| EN / TP5 | Core | CHIP_PU | 3 |
| USB D− / D+ | Ambos | 12 / 13 | 13 / 14 |
| BOOT | Ambos | 9 | 15 |
| Strap alto para download | Ambos | 8 | 10 |
| CAN RX | Core | 18 | 16 |
| CAN_MODE | Core | **2** | **27** |
| J1708 RX / DE | Core | 20 / 21 | 18 / 19 |
| SPI SCK / MOSI / MISO | Core | 6 / 7 / 8 | 6 / 7 / 10 |
| FRAM CS | Core | 10 | 11 |
| microSD CS | Core | **3** | **26** |
| SD_PWR_N | Core | 17 | 24 |
| LINK_PWR_N | Core | 16 | 25 |
| WDT_DONE, mantido como entrada | Core | 1 | 9 |
| GPS RX / TX | Link | 17 / 16 | 24 / 25 |
| GPS PPS | Link | 18 | 16 |
| LTE RX / TX do ESP | Link | **10 / 11** | **11 / 12** |
| LTE PWRKEY | Link | 0 | 8 |

UART22/23 entre Core e Link permanece sem uso. O enlace Wi‑Fi interno libera as duas UARTs roteáveis do Link para GPS/modem. A UART LP tem pinos fixos que não estão ligados ao GPS nesta PCB.

## Entrada J4

| Pino no projeto | Rede |
|---:|---|
| 1 / 2 | CAN H / CAN L |
| 3 | ACC |
| 4 / 6 | J1708 A / J1708 B |
| 5 | GND |
| 7 | +24 V de entrada |
| 8 | Sem conexão prevista |

A vista pelo lado das soldas espelha as posições visuais. Use a numeração do projeto e continuidade; a cor do fio não define polaridade.

## Particularidades

- **CAN:** SHDN/STB de U3 são ativos em alto; GPIO2 baixo permite operação normal. TXD tem pull-up sem conexão ao MCU. O código usa listen-only. O controlador é CAN clássico, mesmo com transceiver compatível com CAN FD.
- **J1708:** `/RE=0`, `DI=0` e `DE=0`; recepção passiva. O timeout é um caractere de 10 bits. Confira checksum, frames rejeitados e overruns no veículo.
- **GPS:** TX do ATGM336H vai ao RX17 do Link. O código recebe NMEA sem enviar comandos por TX16.
- **Modem:** RX10 recebe TX do modem; TX11 envia ao RX do modem. Q50 tem footprint diferente dos transistores originais: GPIO0 alto puxa PWRKEY para baixo.
- **SPI:** uma alimentação defeituosa pode permitir alimentação indevida pelos sinais compartilhados. SD/FRAM exigem conferir o reparo das chaves.

## U8: ligações funcionais corretas

| Pino físico TPL5010 | Função | Destino pretendido |
|---:|---|---|
| 1 | VDD | Alimentação do watchdog |
| 2 | GND | GND |
| 3 | DELAY/M_RST | R5 de 10,2 kΩ para GND |
| 4 | DONE | GPIO1 do Core, pino 9 do módulo |
| 5 | WAKE | Test point previsto |
| 6 | RSTn | CORE_EN, preservando R13/C19 |

O técnico precisa identificar o pino 1 e isolar ligações erradas antes de refazê-las com adaptador ou fios. Girar o CI não desfaz o espelhamento. A tabela define funções, não uma sequência de cortes ou pontes na placa.

O reparo definitivo também precisa rever a alimentação desligável de U8: manter EN alto com VDD do watchdog desligado exige respeitar os limites dos pinos ou prever isolamento/habilitação apropriados. Trocar quatro fios não valida o conjunto ACC/watchdog.

U8 pode ser retirado temporariamente para testar EN, preservando R13/C19. Isso desativa o watchdog externo; não comprova o estado do CI nem valida a placa inteira.

Antes de ativar GPS/celular, meça as alimentações do Link e do GPS, a saída para o modem e o GND do SIM. Ausência de fix, SIM ou rede pode decorrer dessas ligações.

Referências: [ESP32-C6-WROOM-1](https://www.espressif.com/sites/default/files/documentation/esp32-c6-wroom-1_wroom-1u_datasheet_en.pdf), [TPL5010-Q1](https://www.ti.com/lit/ds/symlink/tpl5010-q1.pdf), [TCAN3404-Q1](https://www.ti.com/lit/ds/symlink/tcan3404-q1.pdf), [SN65HVD1781-Q1](https://www.ti.com/lit/ds/symlink/sn65hvd1781-q1.pdf). Compare com os scripts de hardware e os arquivos enviados à fabricação.
