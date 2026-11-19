# Auditoria e reaproveitamento da Rev.B.1

Foram examinadas as fontes de conversão para J1708 e a documentação de bancada para o Core WROOM. O script inicial cita um Core MINI e seu GPIO14 para DE. O Core da Rev.B.1 documentado usa RX20/DE21, ambos com numeração do ESP32-C6. Na Rev.C valem os pinos GPIO6/GPIO7 do ESP32-S3. [Conversão Rev.B](https://github.com/rbasniak1989-lab/Truckbox/blob/1c0bb007f711a997096e1d5959a1c6e30a833dd7/hardware/reva/apply_revb_j1708.py) · [HARDWARE.md da recuperação](https://github.com/rbasniak1989-lab/Truckbox/blob/c46e4fd/firmware/espidf/HARDWARE.md) · [ACTIVE_J1708.md](https://github.com/rbasniak1989-lab/Truckbox/blob/c46e4fd/firmware/espidf/ACTIVE_J1708.md)

| Ref. doadora | Valor/encapsulamento do projeto | Rede ou função | Uso Rev.C |
|---|---|---|---|
| U4 | SN65HVD1781QDRQ1, SOIC-8, C2878178 | RO, /RE, DE, DI, GND, A_INT, B_INT, 3V3_MAIN | Prioridade. Instala no adaptador U1 |
| R53 / R54 | 47 Ω ±1%, 0603, C23182 | Série A e B entre nós internos e J4 | Usar novos axiais |
| R55 / R56 | 4,7 kΩ ±1%, 0603, C23162 | A_INT para 3V3 e B_INT para GND | Usar novos axiais |
| R4 | 10 kΩ, 0603 | DE para GND | Usar novo axial |
| C52 / C53 | 2,2 nF / 50 V X7R, 0603, C93190 | A_INT e B_INT para GND | Usar novos radiais |
| D4 | ESD2CAN24DCKRQ1, SC70-3, C7469913 | Supressor no lado externo de R53/R54, pino 1 em A, 2 em B e 3 em GND nos Runs 317 e 318 | Usar novo em adaptador SC70 ou recuperar da doadora indisponível para uso |
| D3 | Mesmo part number de D4 | Supressor de CAN1 na Rev.B.1 | D4 é a referência do J1708. D3 só serve como peça equivalente se identificado e íntegro |

U4 fica no agrupamento J1708 da placa antiga, junto a R53/R54 e ao D4. Os CPLs dos dois lotes indicam U4 no topo a 0°, em (X 72,00, Y −48,50) mm. D4 está em (X 87,00, Y −49,50), R53 em (X 83,50, Y −50,15) e R54 em (X 83,50, Y −48,85). O CPL usa Y negativo em relação ao gerador KiCad. Confirme a serigrafia e siga os dois resistores de 47 Ω até J4.4/J4.6, com a doadora desligada. Use o datasheet para a orientação física. [Gerador BOM/CPL](https://github.com/rbasniak1989-lab/Truckbox/blob/1c0bb007f711a997096e1d5959a1c6e30a833dd7/hardware/reva/generate_revb_jlc_package.py) · [Datasheet ESD2CAN24-Q1](https://www.ti.com/lit/ds/symlink/esd2can24-q1.pdf)

## Identificação do lote

- A branch `hardware-revb-j1708-4g` aponta para `1c0bb007` e a branch `rescue/revb1-hardware-validation-20261107` para `c46e4fd`. A recuperação registra as evidências de EN, chaves MOSFET, watchdog e Nano SIM e inclui o cruzamento documental do BOM/CPL do Run 317. [Recuperação da Rev.B.1](https://github.com/rbasniak1989-lab/Truckbox/blob/c46e4fd/hardware/reva/REVB1_RECUPERACAO_BANCADA.md)
- O Run 317 está associado a `aaaf36e4` e o Run 318 a `c187965a`. Foram abertos os artefatos `truckbox-reva-pcb-ci` dos dois runs. Ambos contêm 122 posições e confirmam as redes de U4, D3/D4, R4, R53–R56, C52/C53 e J4. O J3 muda de USB4105-GF-A-120 para USB4105-GF-A-060 no BOM. Os hashes SHA-256 do BOM, do CPL e do ZIP de Gerbers do Run 317 coincidem com a auditoria de recuperação. O CPL dos dois runs tem o mesmo hash. Compare o pacote com o pedido efetivo e a placa física antes de dessoldar. [Run 317](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/35845377865) · [Run 318](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/35855033386)

## Retirada do U4

1. Se houver uma única Rev.B.1 operacional, preserve-a e monte o primeiro circuito com um U4 novo. Use uma placa danificada ou excedente como doadora se disponível. Remova o U4 de uma placa funcional somente depois de existir uma alternativa testada.
2. Desconecte veículo, USB e bateria. Fotografe as duas faces, marque pino 1 e confirme a função do CI pela continuidade. Proteja componentes vizinhos com máscara térmica adequada.
3. Em bancada ESD, use fluxo e estação de ar quente com temperatura e vazão compatíveis com a solda e o encapsulamento. Suspenda o CI apenas depois de todas as juntas fundirem. Remova excesso de solda com malha, inspecione pinos e limpe.
4. Solde no adaptador alinhando pino 1. Teste continuidade dos oito terminais individualmente, curto entre adjacentes e a saída RO em repouso. Repita os testes elétricos com o CI recuperado. Se houver dúvida sobre danos térmicos, identifique a peça e use a alternativa nova verificada.
