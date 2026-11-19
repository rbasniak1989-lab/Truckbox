# Alimentação USB e bateria de reserva

A alimentação principal é a tomada do acendedor do Volvo, um carregador USB automotivo para rede de 24 V e o USB-C da H707. Use carregador regulado de 5 V que entregue pelo menos os 2 A de pico exigidos pela LILYGO. Dimensione inicialmente para 3 A disponíveis, com cabo curto e baixa resistência. Meça no conector sob carga, inclusive com o LTE transmitindo. Corrente nominal do carregador por si só não assegura a tensão. A placa recebe 5 V pelo USB-C. [Requisitos elétricos H707](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD)

| Rota | Carregador da célula | Entrada H707 | Ligação da 18650 |
|---|---|---|---|
| 1. Comutação original | Circuito H707 | USB-C vindo do carregador automotivo | No P2/porta-célula da revisão confirmada |
| 2. UPS externo | Circuito do UPS | USB-C vindo exclusivamente da saída regulada do UPS | Somente no conector BAT do UPS, via berço próprio |

## Primeira etapa · alimentação nativa

Na folha 1, P2 é o porta-18650. A proteção da célula envolve U18/DW01FA e U17/8205A no esquema consultado. Confira os componentes no lote. A folha 4 mostra ramos separados, VBUS → U16/JW52222 → VCC_4V2 e VVBAT → U11/TCS9208 → VDD_4V2. A comutação com MOSFETs Q5/Q7 e D8 relaciona VBUS, ramos de 4,2 V e o caminho da célula. DVDD4V2 alimenta o modem e o LDO U12/ME6217-3.3 para VCC3V3. A variante de circuito e o estado da bateria influenciam o transiente. A fabricante informa que a placa pode reiniciar ao trocar de fonte. [Esquema H707 V1.1, folhas 1 e 4](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/main/schematic/esp32s3/T-SIM7670G-S3-V1.1.pdf) · [Nota Battery Protector da LILYGO](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD)

1. Confira que a placa física realmente possui P2/berço da 18650, a polaridade marcada e o comprimento disponível. Uma 18650 protegida pode ser mais longa que uma célula nua. Use célula nova de 3,6–3,7 V nominal e 4,2 V ao fim da carga, com proteção contra sobrecarga, descarga profunda, curto e sobrecorrente. Aproxime a capacidade de 3.000–3.500 mAh se a corrente e o encaixe forem compatíveis. Documente o modelo e ficha da célula.
2. Insira e prenda a célula com a placa desenergizada. Passe o teste de operação só em bateria e a ativação PWR conforme o guia H707. Se a célula protegida não couber ou tiver contato instável, use a rota externa com berço próprio. Evite adaptadores improvisados no V_BAT.
3. Com bateria e USB, meça corrente, VBUS, terminais da célula, VCC3V3 e DVDD4V2. Confirme o limite de carga da placa e o comportamento próximo de 4,2 V. O manual H707 indica até 500 mA no carregamento USB. Repita corte e retorno de USB nas condições do [roteiro de bancada](TESTES_BANCADA.md).
4. Aprove essa rota somente se cumprir, nos dois sentidos e sob carga, cada critério de continuidade. Registre modelo e estado de carga, amplitude e duração real de cada queda, formas de onda, resets, erros do SD e contadores de telemetria.

## Segunda etapa · UPS externo

A primeira opção para qualificação é o **UUGear Witty Pi 4 L3V7**. É um módulo comercial de uma célula Li-Ion 4,2 V, entrada USB-C 5 V, carregamento anunciado de até 1 A e separação da bateria quando 5 V está disponível. Na perda de entrada, seu conversor MP3423 eleva a tensão da célula. A saída anunciada é de até 5 V/3 A. Esses dados selecionam o candidato para bancada. A ficha não publica a margem de transiente da H707 com o cabinho escolhido. [Página do módulo](https://www.uugear.com/product/witty-pi-4-l3v7/) · [Manual, §§1.1, 7.7–7.11 e 11.4](https://www.uugear.com/doc/WittyPi4L3V7_UserManual.pdf)

```text
Tomada do Volvo ── carregador automotivo 5 V ── USB-C IN do UPS
                                                      │
18650 protegida ── berço 1S com retenção ── BAT do UPS  │
                                                      ▼
                  saída 5 V/GND do UPS ── USB-C da H707 ── J1 ── U1
```

- Retire a célula da H707 e deixe P2 e V_BAT livres. O módulo externo passa a ser o único carregador conectado à célula. Meça o PH2.0 de BAT antes de crimpar. Polaridade de um cabo de outro fabricante pode diferir.
- Use saída regulada do header de 40 vias, pinos físicos 2/4 para 5 V e 6/9 para GND de acordo com o desenho do manual. Use conector com retenção, dois contatos de alimentação e retorno dimensionados, e rabicho curto até um receptáculo USB-A de 5 V dedicado. Ligue um cabo USB-A/C compatível à H707. Confirme a polaridade e tensão antes de conectar. Mantenha livres os pinos de controle da Raspberry Pi nessa instalação. Durante gravação USB com computador, desconecte o cabo do UPS da H707 e alimente por uma única fonte.
- O módulo tem chave eletrônica e configurações que precisam de comissionamento. Na bancada, configure o estado de partida como ON e o auto-on no retorno de 5 V pelo procedimento do fabricante. Verifique como a unidade instalada se comporta sem o sinal SYS_UP da Raspberry Pi. Faça teste de partida a frio, perda breve de entrada, retorno durante descarga e retorno após atuação do corte da célula. Valide o ajuste de desligamento com célula simulada ou fonte apropriada e mantenha a proteção própria da célula.
- Corrente estimada no lado da célula em pico de saída de 2 A, com 3,2 V e eficiência de 85%, é `5 × 2 / (3,2 × 0,85) ≈ 3,7 A`. Para 3 A de saída, a estimativa sobe a 5,5 A. Selecione célula, proteção, porta-célula, conector e chicote sob essa carga, com margem de pico documentada. Uma célula protegida de descarga nominal ≥6 A é um ponto de partida para testar 3 A na saída. A corrente de carga somada à carga ativa pode exceder a saída nominal do carregador USB. Observe o sistema sob pior caso na entrada e limite a carga do módulo conforme seu manual e a corrente da célula.
- Nos testes, confirme 5 V na H707 inclusive em regime USB/bypass, modo bateria, queda parcial e retorno. Meça o percurso até o USB-C, não apenas a saída do módulo. Caso esse candidato não alcance a tensão, corrente, partida automática ou continuidade requeridas, qualifique outro UPS comercial com caminho de carga compartilhado para uma célula, anti-retorno, saída sempre ativa sob carga, limitação e proteção equivalentes e os mesmos ensaios.

Prenda a 18650 e mantenha invólucro, abas e terminais íntegros. Evite caixa sobre painel ou junto de aquecimento. A faixa de carga e descarga vem da ficha da célula. Valide a temperatura da célula em carga e com a caixa fechada, incluindo a condição de veículo estacionado. Se a faixa não puder ser garantida, não libere a instalação. Use proteção mecânica dos polos, alívio de tração e retire a alimentação pelo cabo previsto para intervenções.

## Diagnóstico

`boot_id` avança em cada inicialização com NVS disponível. `reset_reason`, `uptime_ms`, o registro `BOOT_DIAGNOSTIC` e a saúde periódica permitem detectar reset ou reinício entre transições. A fonte principal pode desaparecer sem mudar a tensão do ESP ou do USB medido após um UPS. O GPIO4 da H707 mede um nó do circuito de bateria condicionado pelo USB. `power.primary_present`, `power.battery_mv` e `power.primary_transitions` ficam nulos até existir sinal e calibração confiáveis. Registre os eventos de corte/retorno pelo gerador de bancada e pelas capturas de osciloscópio.
