# J1939 v0.2.0 — cobertura e evidência executada

O inventário tem **76 linhas em 29 PGNs**. Todas recebem tratamento e preservação bruta. O resultado atual é **55 linhas com decodificação física/enum/grupo documentada, 15 com campos brutos ou interpretação parcial e 6 pendentes/indisponíveis/sem fórmula**. Isso não significa 76 sensores físicos aferidos.

A classificação exata por ID, unidade e evidência está em `J1939_COVERAGE.csv`. O catálogo é reproduzível por `python firmware/j1939/tools/generate_catalog.py`.

## Referências examinadas integralmente

- `REV_B1_CODEX_IMPLEMENTATION.md` e os dois inventários CSV.
- Decoder Android `TruckBoxJ1939Decoder.kt`, `TruckState.kt` e Core v0.1.0-b1 extraído da release.
- `references/DOCUMENTO_MESTRE_J1939_v1.1_2026-08-11.md` — 633 linhas.
- `references/truckbox_j1939_parser_v1.py` — 736 linhas, implementação de referência.
- `references/VALIDACAO_LOGS_MAE_PARSER_V1.md` — 142 linhas, relatório histórico.

Os arquivos originais dos dois Logs Mãe não foram disponibilizados neste ambiente. O relatório histórico informa os resultados obtidos em 11/08/2026; ele **não é um replay executado nesta entrega**. Os testes locais usam vetores construídos explicitamente a partir do Documento Mestre e os confrontam com o Python fornecido.

## Cobertura sem inflação

| Tratamento | Quantidade | IDs |
| --- | ---: | --- |
| Fórmula física, enum ou grupo documentado | 55 | 1,2,3,6,8,9,10,11,12,14,16,17,18,19,20,21,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,40,41,42,44,45,46,47,50,51,52,53,54,55,57,58,59,60,61,62,63,66,68,69,70 |
| Campos brutos/interpretação parcial, sem afirmar sensor físico | 15 | 7,13,15,22,43,48,49,56,64,65,67,71,72,73,74 |
| Sem fórmula fechada, pendente ou indisponível | 6 | 4,5,38,39,75,76 |

IDs 26 e 37 têm fórmula física, mas continuam **EM_VALIDACAO**. IDs 64/65 não afirmam ABS ativo ou lâmpada acesa: expõem somente B0/B5 brutos, pois a máscara nominal está ausente no Mestre/Python. ID15 mantém estacionamento bruto pelo mesmo motivo. IDs72/73 reconhecem os códigos prováveis D0/D2, sem convertê-los em afirmação física de farol ligado. O seletor ID47 preserva os códigos F2/F3/F4 já implementados no Android; o Mestre não acrescenta uma fórmula para comandos/top gear dos IDs48/49.

`validation` da observação é independente da confiança histórica do inventário. Campos brutos parciais são efetivamente EM_VALIDACAO, mesmo quando o inventário histórico dizia Validado. `raw_only=true` e valor primário nulo impedem anunciar um byte bruto como grandeza física. Curvas e calendário são grupos com componentes nomeados; não são artificialmente contados como sensores adicionais.

## Correções e políticas implementadas

- PGN reconstruída da ID estendida de 29 bits; em PDU1 o destino não compõe a PGN. Assinaturas de alias são classificadas por **PGN+SA**, sem tradução de PGN, rejeição global da PGN oficial ou descarte bruto.
- SAE `FF`, `FE` e `FB..FD` geram indisponível, erro e reservado. Tamanho, faixa e estados são verificados; amostras inválidas não viram números válidos.
- Potência usa a referência documentada **2643 Nm** sem multiplicar novamente pela curva máxima. Expõe componentes de torque Nm, potência cv e consumo estimado `kW×0,25×0,9720031611625598`; o consumo permanece marcado como calibração experimental. A integração persistente de consumo pertence ao helper Core próprio.
- PGN65251: curva de cinco pontos, high idle, referência B19:B20, limites de torque B26/B27 e rotação estendida B28:B29. Engine Gain Kp e limites B24/B25 ficam brutos/parciais. Referência/curva divergentes do perfil Mestre são SUSPEITO_PERFIL e não são valores utilizáveis.
- PGN65249: P5 RPM em B14:B15, **torque P5 em B18**, referência raw930 em B16:B17 sem escala física inventada.
- PGN65258: preserva canais1F/2F por SA; a estimativa do bogie usa a média dos canais recentes, nunca soma. Canal de outra ECU ou antigo não entra na média.
- Peso de composição PGN65295 continua estimativa Volvo, sem valor legal de balança.
- Cruise reconhece OFF C7, ON DF e transição D7. Limite superior vem de **B3 recebido e bit de validade**, nunca é fabricado somando5 ao setpoint. VEB usa enum documentado0..4, sem aceitar reservado/erro/NA como estágio.
- I-Roll canônico é a assinatura ASCII de65321; Economy, marcha e seletor ASCII são preservados. C0/C1 em65295 ficam como redundância.
- Calendário conserva quartos de segundo, `ceil(day_raw/4)`, mês/dia e anos bissextos. Offsets locais permanecem raw: não transforma hora CAN em UTC fictício nem a aplica ao relógio do sistema.
- Distância ID18 no decoder é delta acumulado **do boot**, separado do total persistente e estimado do Core. Primeiro valor, reset, troca de domínio e lacunas não produzem zero fictício ou delta desconhecido. O helper `tbx_trip` evita dupla contagem entre contador e velocidade, inclusive na recuperação após reboot.

## Transporte CAN clássico

`tbx_j1939_decode()` aceita somente DLC físico0..8. Mensagens de30bytes (65251) e19bytes (65249) usam `tbx_j1939_decode_payload()` apenas depois de remontagem validada.

`tbx_j1939_tp` é receptor **TP.BAM passivo**, sem transmissão: quatro sessões, payload máximo64bytes, prazo1250ms entre pacotes, verificação de BAM global, origem/destino, tamanho, quantidade e sequência. Pacotes faltantes, duplicados, fora de ordem, sem sessão ou vencidos são contados e rejeitados. O payload completo guarda os IDs inicial/final de captura e os tempos de início/conclusão. RTS/CTS não é implementado nesta versão. Os frames TP brutos continuam persistidos, inclusive quando a remontagem falha.

A existência de registros agregados com DLC30 nos Logs Mãe não prova CAN FD nem autoriza tratar uma trama CAN clássica como30bytes. O transporte realmente usado pela ECU na Rev.B.1 precisa ser confirmado em bancada; se for somente RTS/CTS, os brutos estarão disponíveis e será necessário ampliar o remontador passivo.

## Verificação executada

- `tests/run_j1939.sh`: C11 estrito com warnings tratados como erros. Catálogo76/29, estados, escalas numéricas, NA/erro/reservado, fontes, canais, deltas, faixas, aliases específicos, frames/DLC inválidos. **PASS**.
- `firmware/j1939/tests/test_reference_equivalence.py`: **80 comparações escalares/componentes C×Python V1**, incluindo curvas de motor e VEB/P5, calendário, temperaturas, pressões, velocidades, torque/potência, cruise e I-Roll. Casos adicionais exercitam diferenças intencionais de segurança: reservado/NA, perfil corrompido, calendário inválido, ausência de máscara e estados desconhecidos. **PASS**.
- `tests/run_j1939_tp.sh`: reconstrução30/19bytes, interleaving, capacidade, sequência, duplicata, órfão, tamanhos, timeout, rollback monotônico e IDs de captura. **PASS**.
- Decoder e TP foram também executados com AddressSanitizer/UndefinedBehaviorSanitizer. **PASS**.
- `tests/run_trip.sh`: checkpoint, boot, reset, salto, SA/MID, prioridade e ausência de dupla contagem. **PASS**; teste da captura/NVS real é registrado no relatório geral.

Os testes acima não comprovam o transceiver, alimentação, pinagem física, montagem microSD ou comportamento temporal na rede real. Esses itens continuam explicitamente separados dos testes de software no relatório da release.
