# TruckBox Health Engine v0.1

## Princípio
O TruckBox não mostra “% de desgaste físico” sem sensor que sustente essa afirmação. Ele mostra:
- condição atual;
- índice de estresse 0–100;
- tendência contra baseline do próprio caminhão;
- confiança do cálculo.

## Cálculos implementados

### 1. Lubrificação do motor
Entradas: PGN 65263 pressão do óleo + PGN 61444 RPM + PGN 65262 temperatura do óleo.
- Aprende `pressão_bar / (RPM/1000)` com óleo 85–112 °C, RPM 850–1650 e torque 15–90%.
- Após 120 s comparáveis, compara o valor atual com o baseline.
- <85% do baseline: atenção.
- <72% do baseline: crítico.
- Pressão absoluta <1,5 bar com motor funcionando: atenção provisória; <1,0 bar: crítico provisório.
- O baseline maduro não absorve desvios grandes e passa a mudar muito lentamente.

### 2. Estresse térmico do motor
Entradas: PGN 65262 água + óleo.
- Óleo >=115 °C ou água >=100 °C: atenção.
- Óleo >=125 °C ou água >=105 °C: crítico.
Faixas provisórias de campo, não limites oficiais Volvo.

### 3. Arrefecimento
Entradas: água + óleo + torque.
- Observa temperatura da água e diferença óleo/água sob carga.
- Delta óleo/água >32 °C com torque >=55% gera atenção.

### 4. Admissão / turbo
Entradas: PGN 65270 boost + temperatura de admissão, PGN 65269 ambiente, PGN 61444 torque/RPM, PGN 61443 pedal.
- Aprende boost normalizado por torque em alta demanda.
- <75% do baseline: atenção; <60%: crítico.
- Em velocidade >40 km/h, admissão >55 °C acima do ambiente: atenção; >75 °C: crítico.

### 5. Alimentação de combustível
Entradas: PGN 65263 pressão + torque + pedal.
- Aprende pressão típica em alta demanda.
- <85% do baseline: atenção; <70%: crítico.

### 6. Índice de estresse do motor
0–100 combinando aproximadamente:
- torque: 45 pontos;
- temperatura do óleo: 25;
- água: 15;
- RPM alto: 15.
Também guarda média acumulada e segundos em estresse severo.

### 7. Embreagem
Entradas: PGN 61442 slip + torque + marcha + velocidade.
- Só acusa desgaste suspeito com marcha estabilizada; não confunde slip normal de troca.
- >=7% sob carga/marcha estável: atenção.
- >=15%: crítico.

### 8. Temperatura I-Shift
Entrada: PGN 65272/TRF1.
- >=105 °C: atenção.
- >=115 °C: crítico.
Faixas provisórias.

### 9. Qualidade das trocas
Entradas: PGN 61445 Selected/Current Gear + PGN 61442 slip.
- Mede tempo entre Selected Gear diferente e engate da marcha alvo.
- Guarda EWMA da duração e do pico de slip.
- Após 5 trocas sai de “aprendizado”.
- Duração média >1,8 s ou pico slip médio >15%: atenção.
- Duração >2,5 s ou pico >25%: crítico.

### 10. Caça de marchas
Entrada: Current Gear PGN 61445.
- 8+ trocas em 60 s: atenção.
- 12+ em 60 s: crítico.

### 11. Estresse da transmissão
0–100 combinando torque, slip, temperatura da caixa e troca em andamento.

### 12. Trem de força / diferencial
Estimativa baseada em torque do motor (referência 2643 Nm usada pelo parser), relação da marcha e peso.
- Mostra esforço mecânico estimado 0–100.
- NÃO afirma desgaste físico do diferencial porque ainda não temos temperatura/vibração/óleo do diferencial.

## Baselines e confiança
- <120 s comparáveis: confiança baixa / aprendizado.
- 120–899 s: média.
- >=900 s: alta.

Baselines persistidos localmente:
- pressão de óleo normalizada por RPM;
- boost normalizado por torque;
- pressão de combustível sob carga;
- estatísticas de troca;
- índices acumulados de estresse.
