# TruckBox Parser J1939 V1 — validação contra os Logs Mãe

Data: 11/08/2026

## Escopo

Esta validação foi feita sobre o parser novo `truckbox_j1939_parser_v1.py`, construído de forma independente do antigo `TruckBox_PGN_Validator_v1.py`.

Fontes canônicas:

- Log Mãe C (carregado): `TruckBox_2026-08-02_23-06-38(1).zip` — 9.080.207 frames.
- Log Mãe V (vazio): `TruckBox_2026-08-03_23-21-21.zip` — 9.845.292 frames.
- Referência externa do Log Mãe V: 113,9 L de combustível.
- Documento Mestre J1939 v1.0 de 11/08/2026.

O processamento em massa foi feito diretamente nos bytes dos frames para evitar custo desnecessário de criação de milhões de objetos Python. Em paralelo, os decoders do parser foram exercitados com frames reais de todas as PGNs implementadas e com testes unitários.

## Resultado geral

**Status: APROVADO PARA V1 como parser de referência.**

- 27 PGNs úteis/funcionais possuem decoder ou tratamento explícito.
- 11 testes automatizados passaram, incluindo um smoke test com frames reais de todas as PGNs implementadas.
- Aliases conhecidos do logger são classificados por `PGN + SA` e não são decodificados como PGNs oficiais.
- Frames isolados corrompidos não substituem o último estado válido em sinais críticos.
- O consumo derivado com Reference Torque 2643 Nm foi recalibrado contra o Log Mãe vazio.

## Validação principal

| Item | Carregado | Vazio | Conclusão |
|---|---:|---:|---|
| Distância 65217 — delta total | 286,49 km | 342,96 km | Dois contadores da 65217 variam igual; resolução 5 m/count confirmada |
| Peso 65258 canal 1F — média | 22.487,69 kg | 16.190,66 kg | Coerente |
| Peso 65258 canal 2F — média | 22.487,94 kg | 16.190,14 kg | Coerente |
| Peso bogie operacional — média dos canais | 22.487,81 kg | 16.190,40 kg | Diferença carregado-vazio = 6.297,41 kg; não somar 1F+2F |
| Peso conjunto 65295 — média filtrada | 68,162 t | 23,604 t | Forte separação carregado/vazio |
| Reference Torque 65251 = 2643 Nm | 3371/3376 frames | 3789/3791 frames | Valor modal/canônico extremamente estável |
| Curva VEB 65249 payload canônico | 3389/3390 frames | 3803/3804 frames | Configuração estática confirmada |

### Faixas operacionais observadas

Carregado:

- RPM: 576,875 a 1.940,75 rpm; média 1.082,36 rpm.
- Torque real: 0 a 100%; média 42,84%.
- Pedal: 0 a 100%; média 33,40%.
- Velocidade: 0 a 109,0 km/h; média 59,72 km/h.
- Água: 65 a 80 °C; média 77,03 °C.
- Óleo do motor: 72,53 a 101,59 °C; média 94,57 °C.
- Pressão de óleo: 1,92 a 4,60 bar; média 3,30 bar.
- Pressão de combustível: 3,44 a 5,84 bar; média 4,35 bar.
- Boost relativo: 0,04 a 2,38 bar; média 0,756 bar.
- Temperatura ambiente: 17,09 a 27,50 °C.

Vazio:

- RPM: 528,5 a 1.732,5 rpm; média 1.021,39 rpm.
- Torque real: 0 a 100%; média 29,44%.
- Pedal: 0 a 100%; média 9,91%.
- Velocidade: 0 a 110,10 km/h; média 63,41 km/h.
- Água: 58 a 79 °C; média 76,05 °C.
- Óleo do motor: 63,09 a 95,78 °C; média 90,59 °C.
- Pressão de óleo: 2,00 a 4,56 bar; média 3,23 bar.
- Pressão de combustível: 3,44 a 5,56 bar; média 4,28 bar.
- Boost relativo: 0,04 a 2,26 bar; média 0,430 bar.
- Temperatura ambiente: 18,50 a 27,38 °C.

As diferenças de torque, pedal e boost entre carregado e vazio são coerentes com a condição operacional e reforçam que os decoders estão lendo grandezas físicas, não coincidências numéricas.

## Consumo derivado — recalibração concluída

A baseline histórica usava 2600 Nm. No Log Mãe vazio:

- Reference Torque 2600 Nm, C0 = 1,0: **115,274230 L**.
- Reference Torque 2643 Nm, C0 = 1,0: **117,180689 L**.
- Combustível externo real: **113,9 L**.

Novo fator global V1:

`C0 = 113,9 / 117,18068886089881 = 0,9720031611625598`

O `FuelPowerAccumulator` do parser usa agora este C0 por padrão. Rodando a própria classe sobre os 970.769 frames EEC1 do Log Mãe vazio:

- Resultado: **113,900000000 L**.
- Erro numérico contra a referência: aproximadamente 4,3e-11 L.

Este C0 deve continuar classificado como calibração V1/provisória até ser confrontado com outras viagens e abastecimentos reais. Ele substitui o antigo fator 0,988012, que não deve ser reutilizado com Reference Torque 2643 Nm.

## Correção importante encontrada durante a validação

### PGN 65249 / FEE1 — P5 da curva do retarder

O layout correto observado é:

- P1: B2:B3 rpm; B4 torque.
- P2: B5:B6 rpm; B7 torque.
- P3: B8:B9 rpm; B10 torque.
- P4: B11:B12 rpm; B13 torque.
- P5 rpm: B14:B15.
- Reference Retarder Torque raw: B16:B17 = 930.
- **P5 torque: B18 = -100%.**

Portanto o P5 não segue o mesmo bloco de três bytes dos quatro primeiros pontos. O parser já implementa esta exceção.

## Corrupções raras observadas e tratamento adotado

Os Logs Mãe contêm raríssimos frames com bytes corrompidos dentro de PGNs válidas. Exemplos:

- 65251: Reference Torque diferente de 2643 em 5/3376 frames carregados e 2/3791 frames vazios; o valor modal é 2643 Nm.
- 61442: input shaft acima de 3000 rpm em 398/1.726.873 frames carregados e 490/1.941.513 frames vazios; muitos correspondem a valores reservados próximos de `FFxx`.
- 65272: temperatura de óleo da transmissão fisicamente impossível em 4/17.259 frames carregados e 10/19.388 frames vazios.
- 65295: 41 frames carregados e 54 vazios produziram peso estimado fora da janela de 10–100 t.

Regra do parser V1: sinal `suspect_alias` ou `unavailable` é preservado para diagnóstico, mas **não substitui o último valor válido no estado operacional**.

## Aliases do logger implementados

O parser não rejeita globalmente essas PGNs; reconhece apenas as assinaturas `PGN + SA` observadas nos Logs Mãe.

- 61456 + SA 0..5 -> provável 61440..61445.
- 65150 + SA108 -> 65132.
- 65246 + SA223 -> 65247; SA193 -> 65217.
- 65214 + SA191 -> 65215.
- 65118 + SA86 -> 65110.
- 65311 + SA02/06/0C/0F -> 65282/65286/65292/65295.
- 65278 + SA E6/EA/EE/EF/F1/F5/F6/F8 -> 65254/65258/65262/65263/65265/65269/65270/65272.
- 65343 + SA25/28/29/2B/3C -> 65317/65320/65321/65323/65340.

## Itens que permanecem parciais por decisão consciente

O parser não inventa máscaras ou escalas ausentes no Documento Mestre. Permanecem parciais/raw:

- PGN 61441: máscara nominal exata de ABS Active e Amber ABS/EBS warning.
- PGN 65282: agregador Volvo; preferir PGNs padrão.
- PGN 65286: byte/enum exato do seletor I-Shift não transcrito no Mestre.
- PGN 65292: byte/enum exato de top gear não transcrito no Mestre.
- PGN 65317: iluminação; D0/D2 são hipóteses fortes, não nominalização final.
- PGN 65254: offsets locais B6/B7 mantidos raw/partial.
- PGN 65251: Engine Gain Kp e limites B24/B25 permanecem parciais.

## Próxima etapa recomendada

O parser Python V1 passa a ser a **implementação de referência**. A próxima peça pode ser o novo Validador V2, separado do parser, para automatizar regressão contra Logs Mãe e novos logs de campo. Depois, os mesmos decoders podem ser portados para Kotlin/Android mantendo testes golden equivalentes.