# Rev.B.1 — Catálogo de sinais J1708 recebidos passivamente

Origem: TruckBox J1708 Matriz de Validação, 29/10/2026 (Validator v2.2).
A matriz identifica 47 PIDs com status PASSIVE_SEEN e mais o PID 247,
que apareceu no período inicial sem requisição embora tenha outro status.
O PID 188 está rotulado PASSIVE_SEEN mas não apareceu no baseline desse
teste; sua classificação passiva deve ser confirmada no replay.
O resumo geral antigo mencionou 50 registros; ainda é necessário
conciliar a diferença antes de anunciar 50 PIDs únicos.

## O que está codificado agora

- 48 PIDs no catálogo de recepção, sem emitir requisições.
- 17 sinais numéricos com escalas SAE J1587 provisórias.
- 31 PIDs com **decodificação detalhada agora implementada** no módulo
  `tbx_j1587_passive_full.c/.h`, além do catalogador básico.
- Resultado numérico marcado EM_VALIDACAO, jamais VALIDADO automaticamente.
- Valor indisponível, comprimento incorreto e PID não conhecido distinguidos.
- MID, PID e bytes de cada valor preservados; integração deve acrescentar
  timestamp, origem, validade temporal e versão da fórmula.
- PID 128 é controle/solicitação, não sensor; PID 194 é pacote de DTCs.
- PID 440, página 2, requer remover envelope de página antes do decoder.

## Decodificação dos 31 PIDs antes brutos

O código detalhado agora interpreta **31 de 31**, classificados assim:

- **13 escalares**: 36 (desgaste embreagem), 46 (pressão reservatório),
  86 (cruzeiro programado), 105/110 (admissão e líquido de arrefecimento),
  106 (pressão absoluta de admissão), 111 (nível arrefecimento),
  122 (freio motor), 171/174/175/177 (temperaturas) e 249 (voltas totais).
- **13 grupos de estado/bitfields**: 43, 44, 49, 70, 71, 83, 85, 89, 97,
  150, 151, 155 e os grupos de bits de indicação da transmissão 162/163
  são tratados como **texto ASCII**, não como bitfields.
- **5 formatos especiais**: 128 (pedido dirigido por MID), 162/163 (marcha
  selecionada/atingida em ASCII e marcador Volvo quando presente),
  194 (DTCs estruturados com SID/PID, FMI, ativo/inativo, contador),
  251 (relógio) e 252 (data). O grupo especial tem 6 PIDs; a separação
  exata é 13 escalares + 12 PIDs de estado + 6 especiais = 31.

Nota de contagem: **12 PIDs de estado** são 43,44,49,70,71,83,85,89,
97,150,151,155. A lista completa contém 31 tipos.

Exemplos da captura de 29/10/2026:
- PID36 `00` → desgaste 0,0% (interpretação SAE preliminar).
- PID46 `70` → 7,7168 bar.
- PID105 `54` → 28,89 °C.
- PID106 `4E` → 134,472 kPa absolutos.
- PID110 `A9` → 76,11 °C.
- PID171 `E8 00` → 14,44 °C.
- PID175 `0E 03` → 90,83 °C.
- PID162 `20 41` → faixa selecionada "A".
- PID163 `31 32` → faixa atingida "12".
- PID194 `12 B5 01` → DTC **SID 18 / FMI 5**, ativo, 1 ocorrência,
  conforme layout SAE; descrição causal requer catálogo do fabricante.
- PID249 `2B 1A 01 00` → 72.235.000 revoluções acumuladas.
- PID251 `00 0B 00` → 00:11:00.
- PID252 `78 0A 29` → 30/10/2026.

**Atenção:** todos permanecem EM_VALIDACAO. PIDs 155, 162/163 e 194
podem depender de detalhes de aplicação Volvo. PID128 é mensagem de
controle e não um sensor. O novo módulo não transmite nada na rede.

A API de entrada é `tbx_j1587_decode_passive_full()` e exige apenas
os bytes de valor após o parser J1708/J1587 retirar MID, PID, prefixo
de comprimento e envelope de página. Preserva bruto, MID, PID,
interpretação e status. Arquivos de teste:
`tests/test_tbx_j1587_passive_full.c`.

## Escalas numéricas provisórias

| PID | Fórmula aplicada | Unidade |
| --- | --- | --- |
| 84 | valor × 0,804672 | km/h |
| 91 | valor × 0,4 | % |
| 92, 96, 98 | valor × 0,5 | % |
| 100 | valor × 0,0345 | bar óleo |
| 102 | valor × 0,00862 | bar turbo relativa |
| 168 | uint16 LE × 0,05 | V |
| 183 | uint16 LE × 0,059147059125 | L/h |
| 184, 185 | uint16 LE × 0,00166072 | km/L |
| 187, 188, 190 | uint16 LE × 0,25 | rpm |
| 245 | uint32 LE × 0,1609344 | km |
| 247 | uint32 LE × 0,05 | horas |
| 440 | uint16 LE × 0,00125 | bar relativa |

Em especial PID 185 teve amostra incompatível com consumo habitual;
não utilizá-lo para custos, médias oficiais ou indicadores sem validar.
Campos de temperatura ainda não foram convertidos para evitar aplicar
offsets incorretos. Cada grandeza duplicada na J1939 precisa de regra
de preferência de fonte e comparação antes de entrar no painel.

## Testes portáteis

    cc -std=c11 -Wall -Wextra -Werror -pedantic src/tbx_j1587_passive.c tests/test_tbx_j1587_passive.c -lm -o /tmp/test_passive && /tmp/test_passive

## Pendente para firmware compilável

Extrair fonte ESP-IDF do ZIP v0.1.0-b1 já funcional, conectar parser RX
que já funciona ao decodificador passivo, tratar framing e mensagens de
múltiplos PIDs corretamente, preservar logs e fazer replay/compilação.
Não substituir driver estável nem transmitir nada a partir deste módulo.
Esta branch ainda não produziu novo .bin nem foi gravada na Rev.B.1.