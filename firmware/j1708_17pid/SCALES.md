# SAE J1587 — escalas provisórias da Rev.B.1

**Política aprovada:** usar desde a primeira versão de teste os 17 PIDs, com as escalas SAE J1587, **todos com status `EM_VALIDACAO`** (independentemente de serem numericamente decodificáveis).

Base técnica: SAE J1587 Revised FEB2002, Appendix A (escalas e máscaras). Base de campo: matriz Validator v2.2 em 29/10/2026; correlações temporais não são comprovação de resposta causal.

| PID | Dados | Conversão/representação | Unidade |
|---|---:|---|---|
| 26 | 1 byte | byte × 0,4 | % ventoinha estimada |
| 31 | 1 byte | byte × 0,4 | % posição range |
| 32 | 1 byte | byte × 0,4 | % posição splitter |
| 33 | 1 byte | byte × 0,4 | % posição cilindro embreagem |
| 34 | 1 byte | 4 campos de 2 bits: 0 desligado, 1 ligado, 2 erro, 3 indisponível | estados de atuadores |
| 37 | 1 byte | byte × 0,0689 | bar ar da transmissão |
| 62 | 1 byte | bits 1–2: 0 sem inibição, 1 inibido, 2 erro, 3 indisponível | estado retarder |
| 63 | 1 byte | 4 campos de 2 bits: 0 desligado, 1 ligado, 2 erro, 3 indisponível | estados da transmissão |
| 68 | 1 byte | byte × 0,5 | % limite torque |
| 108 | 1 byte | byte × 0,431 | kPa pressão atmosférica |
| 117 | 1 byte | byte × 0,0414 | bar ar primário |
| 118 | 1 byte | byte × 0,0414 | bar ar secundário |
| 158 | 2 bytes LE | uint16 × 0,05 | V com chave |
| 160 | 2 bytes LE | uint16 × 0,25 | rpm eixo principal |
| 235 | 4 bytes LE | uint32 × 0,05 | horas em marcha lenta (total) |
| 236 | 4 bytes LE | uint32 × 0,473176473 | litros em marcha lenta (total) |
| 250 | 4 bytes LE | uint32 × 0,473176473 | litros totais na ECU |

`0,473176473` litros = 0,125 galão americano, mais preciso que o valor SAE arredondado (0,473 L).

## Exemplos reproduzidos da captura de 29/10
- PID 26 `FA` → 100,0% (percentual **estimado**, não rotação real do ventilador)
- PID 33 `5D` → 37,2%
- PID 37 `71` → 7,7857 bar
- PID 68 `81` → 64,5% do limite permitido
- PID 108 `C8` → 86,2 kPa
- PIDs 117/118 `B8` → 7,6176 bar
- PID 158 `20 02` → 27,2 V
- PID 160 `A1 0F` → 1000,25 rpm
- PID 235 `10 0F 00 00` → 192,8 h acumuladas
- PID 236 `9A 03 00 00` → ≈436,27 L acumulados
- PID 250 `47 F5 00 00` → ≈29.711,22 L acumulados
- PID 62 `FF` → indisponível (não interpretar como falha ativa).

## Contrato do decodificador
1. A função recebe somente o **payload do PID**, depois de o parser J1587 verificar formato e checksum. O prefixo de comprimento dos PIDs 235/236/250 não entra no payload.
2. Preservar `MID`, `PID`, bytes brutos, comprimento, fator, unidade e valor convertido. A integração deverá acrescentar timestamp, qualidade temporal, ID de captura, versão da tabela de escalas e origem (passivo/solicitado).
3. Um valor convertido **não prova resposta causal** ao pedido: o Core deverá validar MID, comparação com baseline e timing.
4. `EM_VALIDACAO` é etiqueta de confiança do sinal. Não exibir como diagnóstico confirmado nem usar em atuação/segurança. `INDISPONIVEL` e `TAMANHO_INVALIDO` não podem entrar nas médias/alertas.
5. PIDs 34/63 são máscaras de estados; o decodificador preserva quatro campos independentes. PID 62 trata o par menos significativo e bits superiores reservados.
6. Contadores PID 235/236/250 são **acumulados da ECU**, nunca zerar fisicamente para uma viagem; usar diferenças de leituras, respeitando rollovers, resets e origem da ECU.
7. Nunca alterar o firmware `v0.1.0-b1` funcional. Integrar, compilar e validar primeiro nesta branch, com o veículo parado.

## Teste do decodificador
```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic src/tbx_j1587_decoder.c tests/test_tbx_j1587_decoder.c -lm -o /tmp/test_decoder && /tmp/test_decoder
```
