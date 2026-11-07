# TruckBox | Documento Mestre de Decodificação J1939

> Transcrição fiel em texto e tabelas do documento de referência **v1.1 de 11/08/2026**.
> Fonte: arquivo original `TruckBox_Documento_Mestre_J1939_v1.1_2026-08-11.docx`. As regras são as documentadas na data, NÃO novas validações.
> Não atribuir status VALIDADO a sinal sem fórmula/bitmask explícita. Use o arquivo de cobertura de 76 sinais e as capturas quando disponíveis.

TRUCKBOX

Documento Mestre de Decodificação J1939

Volvo FH 540 2013 I-Shift Euro 5

Versão 1.1 — 11 de agosto de 2026

Objetivo: permitir continuidade imediata do desenvolvimento e da validação do parser TruckBox em uma nova conversa, sem refazer a investigação dos Logs Mãe.

## 1. Resumo executivo

A varredura principal dos dois Logs Mãe pós-mudança de rede CAN foi concluída. O conjunto possui 46 PGNs únicas; as mensagens relevantes para o computador de bordo foram decodificadas, validadas ou classificadas como aliases/artefatos. O TruckBox V1 já dispõe de sinais suficientes para motor, transmissão, velocidade/distância, cruise, VEB, peso estimado, pressões, temperaturas, ambiente e cálculo derivado de torque/potência/consumo.

Não voltar aos logs anteriores à mudança de rede CAN: eles não são referência canônica para este trabalho.

O hardware V1 permanece em uma única rede CAN do motor; sinais ausentes devem aparecer como N/A, não justificar mudança para dual-CAN nesta fase.

O parser deve preferir PGNs padrão canônicas quando existirem; PGNs proprietárias agregadoras/duplicadas são usadas apenas quando trazem função exclusiva.

Aliases do logger devem ser reconhecidos por assinatura específica; não se deve rejeitar globalmente a PGN oficial correspondente.

## 2. Fontes canônicas

| Fonte | Arquivo / referência | Uso |
| --- | --- | --- |
| Log Mãe C — carregado | TruckBox_2026-08-02_23-06-38(1).zip | 9.080.207 frames; ~4h47m51s; 44 PGNs. Referência principal de operação carregada. |
| Log Mãe V — vazio | TruckBox_2026-08-03_23-21-21.zip | 9.845.292 frames; ~5h25m45s; 45 PGNs. Referência principal de operação vazia. Combustível real externo: 113,9 L. |
| Banco TechTool/J1939 | PGN_202608092314.csv; SPN_202608092314.csv; SPNPGN_202608092314.csv; _Text__202608092314.csv; _SourceAddress__202608092314.csv | Identidade oficial de PGNs/SPNs, unidades, offsets e Source Addresses. |
| Dicionário consolidado | J1939_Dicionario_Consolidado.xlsx | Cruzamento de PGN, SPN, textos e Source Address. Observação: a exportação não contém explicitamente posição inicial e resolução/escala de todos os SPNs. |

## 3. Metodologia e níveis de confiança

Uma PGN só é considerada fechada quando a interpretação é sustentada por múltiplas evidências: identidade do CAN ID/SA, comportamento temporal, range/resolução, comparação carregado × vazio, correlação com sinais já validados, banco TechTool/J1939 e coerência mecânica. Proximidade numérica isolada não é suficiente.

| Status | Significado |
| --- | --- |
| VALIDADO | Função e fórmula/estado suficientemente comprovados para uso no TruckBox. |
| PARCIAL | Função geral forte, mas algum enum, escala ou nome físico ainda exige teste dirigido. |
| REJEITADO / ALIAS | Frame observado é artefato/corrupção de captura para esta assinatura específica. |
| INDISPONÍVEL | Sinal previsto no padrão, mas não publicado na rede/log atual. |
| PROTOCOLO / RARO | Mensagem de controle, transporte ou ocorrência rara sem prioridade para V1. |

## 4. Inventário dos Logs Mãe

União das PGNs observadas (46):

0, 1024, 53248, 57600, 58112, 60160, 60416, 61440, 61441, 61442, 61443, 61444, 61445, 61456, 65110, 65118, 65132, 65150, 65214, 65215, 65217, 65246, 65247, 65249, 65251, 65254, 65258, 65262, 65263, 65265, 65269, 65270, 65272, 65278, 65282, 65286, 65292, 65295, 65311, 65317, 65320, 65321, 65323, 65340, 65343, 196607

43 PGNs são comuns aos dois logs. Exclusivas do carregado: 58112 (3 frames). Exclusivas do vazio: 57600 (2 frames) e 60416 (4 frames).

## 5. Mapa rápido das PGNs úteis

| PGN | Nome | Status | Uso principal |
| --- | --- | --- | --- |
| 61443 / F003 | EEC2 | VALIDADO | Pedal/acelerador e switches. |
| 61444 / F004 | EEC1 | VALIDADO | RPM, torque real/demanda; base para Nm, potência e consumo derivado. |
| 61440 / F000 | ERC1 | VALIDADO | Torque real do retarder/freio motor. |
| 61441 / F001 | EBC1 | VALIDADO | ABS/EBS; warning ABS/EBS. |
| 61442 / F002 | ETC1 | VALIDADO | Rotações entrada/saída, clutch slip. |
| 61445 / F005 | ETC2 | VALIDADO | Marcha selecionada/atual e relação. |
| 65217 / FEC1 | VDHR | VALIDADO | Distância de alta resolução. |
| 65247 / FEDF | EEC3 | VALIDADO | Nominal Friction Percent Torque. |
| 65249 / FEE1 | RC | VALIDADO | Curva/configuração do retarder VEB. |
| 65251 / FEE3 | EC1 | VALIDADO (núcleo) | Curva de torque do motor e Reference Torque 2643 Nm; campos secundários parciais. |
| 65254 / FEE6 | TD | VALIDADO | Hora/data via painel e tacógrafo. |
| 65258 / FEEA | VW | VALIDADO | Peso estimado do bogie de tração. |
| 65262 / FEEE | ET1 | VALIDADO | Temperaturas de água/óleo. |
| 65263 / FEEF | EFL/P1 | VALIDADO | Pressões de combustível/óleo/cárter; nível de óleo. |
| 65265 / FEF1 | CCVS1 | VALIDADO | Velocidade, estacionamento, cruise set. |
| 65269 / FEF5 | AMB | VALIDADO | Temperatura ambiente. |
| 65270 / FEF6 | IC1 | VALIDADO | Pressão manifold/turbo, temperatura do ar, pressão de entrada. |
| 65272 / FEF8 | TRF1 | VALIDADO | Temperatura de óleo da transmissão. |
| 65282 / FF02 | Volvo prop. | VALIDADO funcional | Agregador de pedal/velocidade/cruise; preferir PGNs padrão. |
| 65286 / FF06 | Volvo prop. | VALIDADO | Seletor I-Shift A/M/N e comandos de troca. |
| 65292 / FF0C | Volvo prop. | VALIDADO | Indicador de top gear. |
| 65295 / FF0F | Volvo prop. | VALIDADO | Peso estimado do conjunto; I-Roll secundário. |
| 65317 / FF25 | Volvo PropB | PARCIAL | Controles de iluminação; D0/D2 provável baixo/alto. |
| 65321 / FF29 | Volvo prop. | VALIDADO | I-Roll canônico em ASCII; modo A/M/N e economia. |
| 65323 / FF2B | Volvo prop. | VALIDADO (núcleo) | Cruise, VEB, limite cruise +5 e veículo parado. |
| 65340 / FF3C | Volvo prop. | VALIDADO funcional | Cruise + / Cruise -. |

## 6. Decodificações detalhadas

### 6.1 PGN 61443 / F003 — EEC2

Status: VALIDADO. Sinal principal de pedal/acelerador.

| Campo | Decodificação / estado |
| --- | --- |
| B1 | SPN91 Accelerator Pedal Position = B1 × 0,4 % |
| B0 (low nibble) | Switches associados ao pedal; comportamento validado funcionalmente. |

pedalPct = B1 * 0.4;

### 6.2 PGN 61444 / F004 — EEC1

Status: VALIDADO. Mensagem central para motor e modelo de consumo.

| Campo | Decodificação / estado |
| --- | --- |
| B2 | SPN513 Actual Engine Percent Torque = B2 - 125 % |
| B3:B4 LE | SPN190 Engine Speed = raw × 0,125 rpm |
| Outros | Torque demand/mode e starter mode existem no padrão; starter exige log de partida para validação prática. |

Com a PGN65251, usar Reference Torque = 2643 Nm para converter torque percentual em Nm.

rpm = LE16(B3,B4) * 0.125;
actualTorquePct = B2 - 125;
actualTorqueNm = 2643.0 * max(actualTorquePct, 0) / 100.0;

### 6.3 PGN 65265 / FEF1 — CCVS1

Status: VALIDADO.

| Campo | Decodificação / estado |
| --- | --- |
| B1:B2 LE | Vehicle Speed = raw / 256 km/h |
| B0 | Parking/estado de estacionamento validado funcionalmente. |
| B5 | Cruise Set Speed. |

vehicleSpeedKph = LE16(B1,B2) / 256.0;

### 6.4 PGN 65217 / FEC1 — VDHR

Status: VALIDADO.

| Campo | Decodificação / estado |
| --- | --- |
| Distância | High-resolution vehicle distance; resolução operacional confirmada: 5 m/count. |

Usar como distância CAN principal quando disponível; GPS permanece útil para rota e validação.

### 6.5 PGN 65263 / FEEF — EFL/P1

Status: VALIDADO / forte conforme campo.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | Fuel Delivery Pressure = B0 × 0,04 bar |
| B1 | Blow-by: FF / indisponível |
| B2 | Engine Oil Level = B2 × 0,4 % |
| B3 | Engine Oil Pressure = B3 × 0,04 bar |
| B4:B5 | Crankcase Pressure = raw / 128 - 250 kPa |
| B6 | Coolant Pressure: FF / indisponível |
| B7 | Coolant Level = B7 × 0,4 % (provável/forte). |

fuelPressureBar = B0 * 0.04;
oilLevelPct = B2 * 0.4;
oilPressureBar = B3 * 0.04;
crankcasePressureKPa = LE16(B4,B5) / 128.0 - 250.0;

### 6.6 PGN 65270 / FEF6 — IC1

Status: VALIDADO / forte.

| Campo | Decodificação / estado |
| --- | --- |
| B1 | Intake Manifold Pressure = B1 × 2 kPa; boost relativo operacional = B1 × 0,02 bar |
| B2 | Intake Manifold Temperature = B2 - 40 °C |
| B3 | Air Inlet Pressure = B3 × 2 kPa absoluto |
| B4 | Air Cleaner Differential Pressure = B4 × 0,05 kPa; geralmente 0. |

B3 - B1 resulta em pressão atmosférica aproximada de 91,93 kPa no log analisado.

### 6.7 PGN 65269 / FEF5 — AMB

Status: VALIDADO. SA23.

| Campo | Decodificação / estado |
| --- | --- |
| B3:B4 LE | Ambient Air Temperature = raw × 0,03125 - 273 °C |

ambientTempC = LE16(B3,B4) * 0.03125 - 273.0;

### 6.8 PGN 65262 / FEEE — ET1

Status: VALIDADO.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | Coolant Temperature = B0 - 40 °C |
| B1 | Fuel Temperature; no FH observado espelha coolant/fallback |
| B2:B3 LE | Engine Oil Temperature = raw × 0,03125 - 273 °C |
| B4-B7 | Indisponíveis/FF no veículo analisado. |

### 6.9 PGN 65272 / FEF8 — TRF1

Status: VALIDADO. SA03 / transmissão.

| Campo | Decodificação / estado |
| --- | --- |
| B4:B5 LE | Transmission Oil Temperature = raw × 0,03125 - 273 °C |

### 6.10 PGN 61442 / F002 — ETC1

Status: VALIDADO. SA03; ~10 ms.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | Estados ETC1. |
| B1:B2 LE | Transmission Output Shaft Speed = raw × 0,125 rpm |
| B3 | Clutch Slip = B3 × 0,4 % |
| B5:B6 LE | Transmission Input Shaft Speed = raw × 0,125 rpm |

### 6.11 PGN 61445 / F005 — ETC2

Status: VALIDADO.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | Selected Gear = B0 - 125 |
| B1:B2 LE | Actual Gear Ratio = raw × 0,001 |
| B3 | Current Gear = B3 - 125 |

### 6.12 PGN 61440 / F000 — ERC1

Status: VALIDADO. SA15 / Engine Compression Braking.

| Campo | Decodificação / estado |
| --- | --- |
| B1 | Actual Retarder Percent Torque = B1 - 125 %. Valores negativos durante frenagem. |

Usar esta PGN para torque efetivo do freio motor; a posição da alavanca vem da 65323.

### 6.13 PGN 61441 / F001 — EBC1

Status: VALIDADO. SA11.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | ABS Active bit validado. |
| B5 | Amber ABS/EBS warning ativo e coerente com a lâmpada observada. |

### 6.14 PGN 65132 / FE6C — TCO1

Status: VALIDADO. SAEE; ~20 Hz.

| Campo | Decodificação / estado |
| --- | --- |
| B0 bits 6-7 | Vehicle Motion: 0 = parado; 1 = movimento detectado |
| B0 bits 0-2 | Driver 1 Working State; comportamento coerente, enum nominal não totalmente fechado |
| B4:B5 LE | Tachograph Output Shaft Speed = raw × 0,125 rpm |
| B6:B7 LE | Tachograph Vehicle Speed = raw / 256 km/h |

Filtrar raras duplicações/artefatos do logger.

### 6.15 PGN 65247 / FEDF — EEC3

Status: VALIDADO. SA00 / ECM; ~4 Hz.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | SPN514 Nominal Friction Percent Torque = B0 - 125 % |
| B1:B2 | Desired Operating Speed: FF FF / indisponível |
| Demais | Indisponíveis no veículo atual. |

### 6.16 PGN 65249 / FEE1 — RC Retarder Configuration

Status: VALIDADO. SA0F; 19 bytes; quase estática.

| Campo | Decodificação / estado |
| --- | --- |
| B0 nibbles | Tipo/localização: raw 3/0; enum nominal ainda não necessário para V1 |
| B1 | Control method raw 4; enum nominal não fechado |
| Curva P1 | 900 rpm / -81 % |
| Curva P2 | 2300 rpm / -92 % |
| Curva P3 | 1200 rpm / -91 % |
| Curva P4 | 1920 rpm / -96 % |
| B14:B15 + B18 | P5 = 1680 rpm / -100 %; torque P5 em B18. |
| B16:B17 | Reference retarder torque raw 930; escala física não resolvida. |

Curva validada dinamicamente contra PGN61440 Actual Retarder Torque; especialmente 1680 rpm / -100 %.

### 6.17 PGN 65251 / FEE3 — EC1 Engine Configuration 1

Status: NÚCLEO VALIDADO; campos secundários permanecem parciais. Log observado com SA00/ECM, CAN ID 18FEE300, DLC 30, embora a definição TechTool moderna tenha 39 bytes.

| Campo | Decodificação / estado |
| --- | --- |
| B0:B1 + B2 | 600 rpm / 52 % |
| B3:B4 + B5 | 2110 rpm / 54 % |
| B6:B7 + B8 | 1200 rpm / 99 % |
| B9:B10 + B11 | 1500 rpm / 94 % |
| B12:B13 + B14 | 1800 rpm / 81 % |
| B15:B16 | High Idle = 2110 rpm |
| B17:B18 | Maximum Momentary Override Speed Point 7: FF FF |
| B19:B20 LE | SPN544 Engine Reference Torque = 2643 Nm |
| B21:B22 | Engine Gain Kp raw 18400; escala física exata pendente |
| B23 | Max Momentary Override Time: FF |
| B24 | Requested Speed Control Lower Limit: provável 500 rpm; escala ainda PARCIAL |
| B25 | Requested Speed Control Upper Limit: provável 2500 rpm; escala ainda PARCIAL |
| B26 | Torque-control lower limit = 0 % |
| B27 | Torque-control upper limit = 100 % |
| B28:B29 LE | Extended requested speed upper ≈ 3000 rpm; forte/validado operacionalmente |

Os 30 bytes observados correspondem exatamente ao subconjunto até SPN1712; não ler os 9 bytes finais ausentes.

Torque máximo aproximado pela curva: 600/1374 Nm; 1200/2617 Nm; 1500/2484 Nm; 1800/2141 Nm; 2110/1427 Nm.

rpm1 = LE16(B0,B1) * 0.125; tq1Pct = B2 - 125;
rpm2 = LE16(B3,B4) * 0.125; tq2Pct = B5 - 125;
rpm3 = LE16(B6,B7) * 0.125; tq3Pct = B8 - 125;
rpm4 = LE16(B9,B10) * 0.125; tq4Pct = B11 - 125;
rpm5 = LE16(B12,B13) * 0.125; tq5Pct = B14 - 125;
highIdleRpm = LE16(B15,B16) * 0.125;
referenceTorqueNm = LE16(B19,B20); // 2643
torqueControlMinPct = B26 - 125;
torqueControlMaxPct = B27 - 125;
extendedSpeedUpperRpm = LE16(B28,B29) * 0.125;

### 6.18 PGN 65254 / FEE6 — TD Time/Date

Status: VALIDADO. Dois emissores: SA23/Instrument Cluster #1 a ~0,1 Hz e SAEE/tacógrafo a ~1 Hz.

| Campo | Decodificação / estado |
| --- | --- |
| B0 | Seconds = B0 × 0,25 s |
| B1 | Minutes |
| B2 | Hours |
| B3 | Month |
| B4 | Day com resolução 0,25 dia/count; exibição prática: ceil(B4/4) |
| B5 | Year = B5 + 1985 |
| B6 | Local minute offset |
| B7 | Local hour offset |

No SAEE do veículo: offset -3 h e -13 min; após correção, relógio fica tipicamente ~2 s do painel.

Horário principal do TruckBox: Android/GPS; CAN é fallback/diagnóstico.

### 6.19 PGN 65258 / FEEA — VW Vehicle Weight

Status: VALIDADO. SA17; ~1 Hz.

| Campo | Decodificação / estado |
| --- | --- |
| B0 = 0x1F | SPN928 Axle Location: eixo físico 2 / primeiro eixo de tração |
| B0 = 0x2F | SPN928 Axle Location: eixo físico 3 / segundo eixo de tração |
| B1:B2 LE | SPN582 Axle Weight = raw × 0,5 kg |
| B3-B7 | FF / indisponíveis |

No FH analisado, 1F e 2F publicam praticamente o mesmo valor; NÃO SOMAR.

Tratar operacionalmente como leitura redundante da carga estimada do bogie/grupo de tração; usar média/filtragem.

Média carregado ~22,49 t; vazio ~16,19 t; diferença ~6,30 t.

weightKg = LE16(B1,B2) * 0.5;
// guardar 1F e 2F e usar média filtrada; não somar

### 6.20 PGN 65295 / FF0F — Volvo proprietária

Status: VALIDADO. SA03; ~10 Hz.

| Campo | Decodificação / estado |
| --- | --- |
| B2 | C0 = I-Roll inativo; C1 = I-Roll ativo |
| B5 | Peso estimado do conjunto ≈ 1 tonelada/count |
| B0 | Speed-state funcional. |

Carregado tipicamente valores na faixa dos 70; vazio ~23-24. Diferença de ~50 counts coerente com ~50 t de carga.

Para I-Roll, preferir 65321 como sinal canônico; usar 65295 sobretudo para peso estimado do conjunto.

### 6.21 PGN 65321 / FF29 — Volvo proprietária I-Shift/I-Roll

Status: VALIDADO. SA03; ~100 ms; conteúdo ASCII de 8 caracteres.

| Campo | Decodificação / estado |
| --- | --- |
| ASCII " AN E+00" | I-Roll ativo (assinatura exata observada) |
| B1 | A/M/N |
| B2-B3 | Marcha |
| B4 | E = modo Economy |

Sinal canônico de I-Roll para o TruckBox.

### 6.22 PGN 65323 / FF2B — Volvo proprietária Cruise/VEB

Status: NÚCLEO VALIDADO. SA17 / Speed-based control.

| Campo | Decodificação / estado |
| --- | --- |
| B0 C7 | Cruise OFF |
| B0 DF | Cruise ON |
| B0 D7 | Transição |
| B1 bits 5-7 | 0=VEB OFF; 1=Auto; 2=VEB estágio 1; 3=estágio 2; 4=estágio 3 |
| B1 bit0 | Validade do B3 |
| B2 CF | VEB abaixo do estágio máximo |
| B2 DF | VEB estágio 3 selecionado |
| B3 | Limite superior associado ao cruise; 526/526 amostras = cruise set + 5 km/h. Nome Volvo exato ainda não conhecido. |
| B4 F7 | Veículo parado confirmado |
| B4 F3 | Rodando/transição |
| B5-B7 | Sem função operacional útil fechada |

cruiseActive = (B0 == 0xDF);
cruiseTransition = (B0 == 0xD7);
vebLever = (B1 >> 5) & 0x07;
b3Valid = (B1 & 0x01) != 0;
vebMaxStageSelected = (B2 == 0xDF);
if (b3Valid && B3 != 0xFF) cruiseUpperSpeedKph = B3;
vehicleStationary = (B4 & 0x04) != 0;

### 6.23 PGN 65340 / FF3C — Volvo proprietária

Status: VALIDADO funcional. SA17; ~100 ms.

| Campo | Decodificação / estado |
| --- | --- |
| B6 C1 | Estado normal |
| B6 C5 | Cruise - |
| B6 D1 | Cruise + |

Útil para detectar ação do motorista nos comandos do cruise.

### 6.24 PGN 65317 / FF25 — Volvo Proprietary B / Lighting

Status: PARCIAL. Mensagem real, CAN ID 0CFF2537, SA55 = Lighting - Operator Controls, ~1 Hz.

| Campo | Decodificação / estado |
| --- | --- |
| B1 C0 | Hipótese forte: iluminação principal inativa/inicialização |
| B1 D0 | Hipótese forte: farol baixo |
| B1 D2 | Hipótese forte: farol alto |
| B1 D1 | Estado raríssimo/transitório |
| B0 00 | Repouso/sem comando provável |
| B0 44/50/60 | Comandos auxiliares de iluminação ainda não identificados |
| B2-B5 | FF |
| B6-B7 | Sem função útil fechada |

Teste futuro: log curto dirigido com caminhão parado e comandos de farol baixo, alto, lampejo, setas e emergência acionados em sequência conhecida.

## 7. PGNs observadas que são aliases/artefatos do logger

As regras abaixo valem para as assinaturas observadas nos Logs Mãe. Não significam que a PGN oficial não exista no padrão.

| PGN observada | Classificação | PGN provável/original | Evidência principal |
| --- | --- | --- | --- |
| 61456 / F010 | REJEITADO / alias | 61440..61445 conforme SA 00..05 | Alias reconhecido por PGN+SA; o byte baixo da PGN original aparece no SA observado. |
| 65150 / FE7E | REJEITADO / alias | 65132 / TCO1 | Alias observado do tacógrafo. |
| 65246 / FEDE (frames atuais) | REJEITADO / alias | 65247 ou 65217 conforme SA | AIR2 oficial existe, mas os 83 frames observados são aliases SADF/SA C1. |
| 65278 / FEFE | REJEITADO / alias | FEE6/FEEA/FEEE/FEEF/FEF1/FEF5/FEF6/FEF8 | SA E6/EA/EE/EF/F1/F5/F6/F8 → FEE6/FEEA/FEEE/FEEF/FEF1/FEF5/FEF6/FEF8. |
| 65214 / FEBE | REJEITADO / alias | 65215 / EBC2 | 405/405 frames no slot temporal perdido da EBC2; CAN ID 18FEBEBF. EEC4 real não observada. |
| 65118 / FE5E | REJEITADO / alias | 65110 / FE56 | 15/15 frames com payload FE FF FE FF FF FF FF FF e exatamente no slot de 1 s perdido da 65110. |
| 65311 / FF1F | REJEITADO / alias | 65282/65286/65292/65295 | SA 02/06/0C/0F → 65282/65286/65292/65295. Regra específica dos Logs Mãe. |
| 65343 / FF3F | REJEITADO / alias | 65317/65320/65321/65323/65340 | SA 25/28/29/2B/3C → PGNs FF25/FF28/FF29/FF2B/FF3C; não decodificar como FF3F real. |

## 8. Mensagens com pouca utilidade / indisponíveis

| PGN / sinal | Status | Observação |
| --- | --- | --- |
| 65110 / FE56 AT1T1I | INDISPONÍVEL na prática | Mensagem SCR/ARLA legítima; payload observado majoritariamente FE/FF, sem dado útil fechado. |
| 65320 / FF28 | Sem uso fechado | SA23 estática; hipótese antiga de transmissão rejeitada. |
| Bateria/alternador | INDISPONÍVEL | PGN65271 e PGN65237 ausentes; sem SA26 variável útil na rede CAN atual. |
| Starter mode | PENDENTE teste dirigido | SPN1675 existe em 61444; necessário log desde antes da partida para fechar. |

## 9. Grandezas derivadas para o TruckBox

### 9.1 Torque instantâneo, torque disponível e potência

A PGN61444 fornece torque percentual real e RPM; a PGN65251 fornece Reference Torque e a curva máxima por rotação. No padrão J1939, Actual Engine Percent Torque é referenciado ao Reference Torque. Portanto, não multiplicar o torque real pela porcentagem da curva novamente.

T_ref = 2643 Nm
T_actual = T_ref * ActualEnginePercentTorque / 100
T_max_curve(rpm) = T_ref * interpolatedCurvePercent(rpm) / 100
relativeLoadAvailable = T_actual / T_max_curve(rpm)
P_kW = T_actual * rpm / 9550
P_cv = P_kW * 1.35962

Uso recomendado: armazenar por trecho de 50 km consumo, peso estimado, potência média, torque médio, carga relativa disponível, RPM média, tempo em I-Roll e tempo de VEB. A comparação de eficiência histórica deve preferir condições semelhantes, e não apenas km/L bruto.

### 9.2 Consumo derivado — V1 recalibrado

A solução canônica do projeto permanece derivar consumo a partir da potência mecânica estimada, e não usar uma PGN de litros. A recalibração pendente foi concluída nos Logs Mãe: com Reference Torque = 2643 Nm, o modelo base sem correção produz 117,180689 L no Log Mãe vazio; a referência externa é 113,9 L.

Modelo V1:
TorqueNm = 2643 * max(torquePct,0) / 100
PowerkW = TorqueNm * RPM / 9550
L_per_h = PowerkW * 0.25 * C0
C0 = 0.9720031611625598
Liters = sum(L_per_h * dt / 3600)

Validação: a baseline histórica com 2600 Nm resultou em 115,274230 L (coerente com o valor histórico ~115,282 L). Com 2643 Nm e C0=1,0, o resultado é 117,180689 L. O novo C0 V1 = 113,9 / 117,18068886089881 = 0,9720031611625598. Rodando a classe FuelPowerAccumulator do parser sobre os 970.769 frames EEC1 do Log Mãe vazio, o resultado é 113,900000000 L. O antigo C0 ~0,988012 fica obsoleto para T_ref=2643 Nm.

Modelo de correção dinâmica experimental (não fechado):
L/h = Pmotor * (210/840) * C0 * [1 + Kp * (Pfuel - Pref(RPM,carga,torque)) / Pref]

PGN65263 / pressão de combustível é candidata a termo de correção, não fonte direta de litros.

A antiga hipótese PGN65304/SA0/B4 para consumo fica registrada apenas como histórico experimental de fase anterior. Na arquitetura atual, não tratá-la como fonte canônica sem nova validação nos Logs Mãe pós-mudança de rede.

## 10. Estratégia de parser TruckBox

Identificar por PGN + SA + DLC; não confiar apenas no número da PGN quando houver assinaturas conhecidas de alias.

Aplicar little-endian somente onde fechado; nunca inferir posição/escala ausente apenas pelo banco TechTool.

Separar camada “raw J1939” de camada “derived metrics”. Torque Nm, potência, carga relativa e consumo são derivados.

Manter status/qualidade por sinal: valid, unavailable, suspect/alias, stale e source.

Para 65258, manter canais 1F e 2F, mas expor ao app um único driveBogieEstimatedWeightKg filtrado; nunca somar os dois.

Para peso total, usar 65295 como estimatedCombinationWeight; manter separado de 65258.

Para I-Roll, usar 65321 como canônico; 65295 serve como redundância/contexto.

Frames com qualidade unavailable ou suspect/alias não devem substituir o último valor operacional válido; preservar o frame suspeito separadamente para diagnóstico.

Para VEB: alavanca/estágio em 65323; torque efetivo do retarder em 61440.

## 11. Modelo de dados recomendado

EngineState {
  rpm
  actualTorquePct
  actualTorqueNm
  maxTorqueAtRpmNm
  relativeLoadAvailable
  powerKw
  coolantTempC
  oilTempC
  oilPressureBar
  fuelPressureBar
  crankcasePressureKPa
  boostRelativeBar
  intakeTempC
  ambientTempC
}

TransmissionState {
  currentGear
  selectedGear
  gearRatio
  inputShaftRpm
  outputShaftRpm
  transmissionOilTempC
  iRollActive
}

VehicleState {
  speedKph
  distanceHighRes
  combinationWeightEstimatedKg
  driveBogieEstimatedWeightKg
  cruiseActive
  cruiseSetSpeedKph
  cruiseUpperSpeedKph
  vehicleStationary
  absActive
  absEbsWarning
}

RetarderState {
  vebLeverStage
  maxStageSelected
  actualRetarderTorquePct
}

## 12. Pendências reais — não repetir investigação já fechada

| Prioridade | Pendência | Como validar |
| --- | --- | --- |
| Média | 65317: identificar D0/D2 e 44/50/60 nominalmente | Log curto dirigido, veículo parado, comandos de iluminação em sequência conhecida. |
| Baixa | 65251: escala exata Engine Gain Kp e B24/B25 | Buscar definição primária/TechTool adicional ou observar requisição/diagnóstico; não bloqueia V1. |
| Baixa | 65323: nome Volvo exato de B3 | Valor já validado como cruise set +5; nomenclatura pode ser descoberta em documentação Volvo/TechTool. |
| Baixa | Starter mode SPN1675 | Log desde chave desligada, partida e estabilização. |

## 13. Decisões de produto relacionadas

Tablet Android em paisagem fixa; Compose/Material3; modo escuro; menu inferior único.

Abas: Início, Consumo, Viagens, Manutenção/Diagnóstico, Logger e Configurações.

Modo Motorista e Modo Desenvolvedor.

Ao encerrar viagem carregada, iniciar automaticamente modo vazio e continuar distância, tempo, consumo e rota até a próxima viagem carregada.

Consumo: comparar segmentos de 50 km com histórico semelhante; não mostrar apenas uma “melhor média” sem considerar peso/carga/condição.

Hardware V1: ESP32 DevKit + transceptor CAN 3,3 V + GPS + buck 24→5 V; uma única rede CAN do motor.

## 14. Checklist para iniciar a próxima conversa

A próxima conversa pode começar diretamente com este documento e a instrução: “Use este Documento Mestre como estado atual do projeto TruckBox. Não reabra PGNs já fechadas, salvo se houver nova evidência. O Parser J1939 V1 em Python é a implementação de referência e o consumo usa T_ref=2643 Nm com C0=0,9720031611625598. Próxima etapa: construir o Validador V2 separado e depois portar os decoders para Kotlin/Android.”

Carregar os Logs Mãe apenas se for necessário reprocessar estatísticas ou testar o parser.

Carregar os CSVs TechTool apenas quando for necessário consultar nova PGN/SPN ou nomenclatura.

Preservar as regras de aliases do logger para evitar falsas descobertas.

Atualizar este documento sempre que uma nova PGN for validada ou uma regra do parser mudar.

## 15. Parser J1939 V1 — implementação e validação

Em 11/08/2026 foi criado do zero o parser de referência TruckBox J1939 V1, independente do antigo PGN Validator. A implementação foi validada contra os dois Logs Mãe e recebeu testes automatizados com frames reais.

Consumo V1: Reference Torque 2643 Nm; C0 = 0,9720031611625598; Log Mãe vazio = 113,900000000 L.

PGN65249: P5 rpm em B14:B15; Reference Retarder Torque raw em B16:B17 = 930; torque P5 em B18 = -100%.

PGN65258: médias carregado 22,488 t e vazio 16,190 t; 1F/2F continuam redundantes e nunca são somados.

PGN65217: delta do Log Mãe vazio = 342,96 km nos dois contadores de alta resolução.

Frames raros corrompidos são marcados suspect/alias e não sobrescrevem o último estado válido.

Aliases do logger são reconhecidos por PGN+SA; a PGN oficial correspondente não é rejeitada globalmente.

## 16. Anexo — Regras críticas em uma página

| Regra | Decisão |
| --- | --- |
| Torque Nm | 2643 × Actual Engine Percent Torque / 100. |
| Potência | TorqueNm × RPM / 9550. |
| Peso conjunto | 65295 B5 ≈ 1 t/count. |
| Peso bogie | 65258 B1:B2 × 0,5 kg; 1F/2F redundantes; NÃO SOMAR. |
| I-Roll | 65321 canônica. |
| VEB estágio | 65323 B1 bits 5–7. |
| VEB torque real | 61440 B1 - 125 %. |
| Velocidade | 65265 B1:B2 / 256 km/h. |
| RPM | 61444 B3:B4 × 0,125 rpm. |
| Pedal | 61443 B1 × 0,4 %. |
| Consumo | Derivado de potência: 2643 Nm; L/h = PowerkW × 0,25 × 0,9720031611625598. |
| Logs válidos | Somente Logs Mãe pós-mudança de rede CAN; parser V1 validado contra ambos. |

Fim do Documento Mestre v1.1