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
- 31 PIDs conservados como payload bruto até revisão das máscaras,
  temperaturas, códigos de falha e formatos especiais.
- Resultado numérico marcado EM_VALIDACAO, jamais VALIDADO automaticamente.
- Valor indisponível, comprimento incorreto e PID não conhecido distinguidos.
- MID, PID e bytes de cada valor preservados; integração deve acrescentar
  timestamp, origem, validade temporal e versão da fórmula.
- PID 128 é controle/solicitação, não sensor; PID 194 é pacote de DTCs.
- PID 440, página 2, requer remover envelope de página antes do decoder.

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