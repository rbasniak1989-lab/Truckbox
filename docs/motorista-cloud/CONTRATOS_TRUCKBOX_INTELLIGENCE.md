# Contratos Cloud do TruckBox Intelligence

## Auditoria da integração

| Canal publicado | Comportamento conferido |
| --- | --- |
| `multimedia-next-v1` v5, GET `/v1/state` | A sessão de campo encaminha a consulta assinada à função `truckbox_multimedia_field_test_v1`. A função inclui `fuelRange` com o resultado de `truckbox_fuel_range_metrics`. O handler da sessão Supabase Auth padrão ainda precisa projetar os mesmos campos para o veículo autorizado. |
| `truckbox_multimedia_trip_extras_v1` | Consulta interna de rota e jornada. Usa veículo, central, nonce vigente e versão da política. A chamada direta requer `service_role`; a Multimídia deve receber os dados no canal assinado que já utiliza. |
| `truckbox-trip-v1` v4 | `plans` retorna a rota vinculada e resumos. `plan_truck` solicita uma alternativa ao HERE e normaliza o primeiro plano sem aviso crítico. A resposta atual contém um único `route`, sem altimetria. `attach` grava o vínculo pelo ID da viagem existente. |
| `truckbox_trip_fuel_forecast` | Projeção legada baseada na previsão cadastrada e em ajustes com a média da viagem. O valor original é útil para auditoria. A projeção legada fica fora do novo contrato topográfico. |
| `truckbox_rolling_fuel_metrics`, `truckbox_trip_50km_analysis` | Já fornecem janelas e blocos por odômetro e contadores. A rotina de blocos trabalha com `trip_fuel_l`; a adaptação ao Lite deve conferir alinhamento e qualidade dos contadores antes de alimentar histórico ou aprendizado. |
| Carga Alerta `cargaalerta-freight-enrich-v1` v10 | Solicita `return=summary,polyline,elevation` quando habilitado. Decodifica o Flexible Polyline 3D, valida o comprimento e amostra o perfil. O projeto já tem cache de rotas, registro de elevação e controle de consultas. |
| `truckbox-app` v23 e `truckbox_ai` | A simulação legada já usa elevação e memória de estrada. As tabelas de geografia e memória servem como ponto de partida, com validação de qualidade, sentido, veículo e carga para treinamento. |

A Multimídia disponível no repositório parte do APK Next 0.2.4 e do contrato assinado da Cloud. O Motorista parte do 0.6.7. A inspeção do caso real foi de leitura. O vínculo HERE ativo tinha geometria e carecia de perfil de elevação e previsão no momento da consulta. Os APKs usam as autorizações existentes e a configuração pública de projeto já adotada.

O Routing v8 oferece elevação embutida no polyline, dispensando uma segunda chamada quando o plano e a geometria da resposta trouxerem o perfil. A conta Carga Alerta já produz esse tipo de resposta. A disponibilidade e a tarifação do plano usado pela chave de roteamento TruckBox precisam ser conferidas na conta HERE. Caso o serviço não forneça elevação para uma alternativa, publique essa indisponibilidade e registre quaisquer custos de um complemento antes de ativá-lo. [Geometria e elevação HERE](https://docs.here.com/routing/docs/routing-v8-route-geometry)

## Motorista e cálculo de alternativas

Manter `POST /functions/v1/truckbox-trip-v1` com o mesmo envelope `device_uid`, `token`, `action`, `payload`. O cadastro `DRIVER_APP` já autoriza o seletor para o veículo ativo. Usar `tripClientUid` para resolução da viagem atual e verificar novamente no momento de persistir e vincular.

No resultado de `plans`, mantenha `route` e `plans`. Declare `capabilities: {"version":1,"waypoints":true}` ao aceitar todos os pontos intermediários na ordem solicitada. O aplicativo só oferece essa edição quando a capacidade é afirmativa.

Exemplo de `payload` de `plan_truck` com identificadores e dimensões de teste.

```json
{
  "tripClientUid": "viagem-de-teste",
  "clientId": "00000000-0000-4000-8000-000000000001",
  "useGps": false,
  "alternatives": 3,
  "waypoints": ["Município intermediário/PR"],
  "tollPayer": "CARRIER",
  "profile": {"heightCm": 420, "widthCm": 260, "lengthCm": 2500, "axles": 9, "grossKg": 74000, "weightKg": 69000}
}
```

- `alternatives` é o número máximo adicional à principal. Aceite de zero a três e encaminhe para `alternatives` no HERE. Valide a principal e cada alternativa separadamente, descarte opções com avisos críticos e devolva apenas as disponíveis. Mantenha os outros avisos da fonte no plano.
- `waypoints` são até cinco endereços de até 160 caracteres. Geocodifique e encaminhe como `via` na ordem recebida, sem substituir pontos silenciosamente. Retorne `appliedWaypoints` com os textos da entrada quando todos foram aplicados.
- `grossKg` é o PBTC informado e `weightKg` o peso bruto atual. Use limitações de veículo, cobertura regional, dimensões e restrições divulgadas pelo HERE. Um trajeto sem aviso crítico não comprova liberação legal.
- `useGps` requer a regra existente de GPS do celular com captura recente, precisão e veículo conferidos. Marque a origem como `Posição do caminhão` para distinguir um plano remanescente.
- Preserve a idempotência por veículo, viagem e `clientId`. Grave todas as alternativas antes de responder, com IDs de plano independentes e a validade do provedor. `plans` precisa refletir a mesma seleção após a consulta seguinte do app. Mantenha `route` como o primeiro plano para clientes existentes e acrescente `routes` com um a quatro planos completos.

Cada objeto em `routes` tem os campos já usados `id`, `tripId`, `provider`, `origin`, `destination`, `distanceM` (m), `durationSec` (s), `polyline` (Google polyline 1e-5 no contrato atual), `tollCoverage`, `tolls`, `warnings`, `createdAt` e `validUntil`. Os campos complementares seguem este exemplo.

```json
{
  "revision": 1,
  "mainRoads": ["BR-000"],
  "ascentM": 65.4,
  "descentM": 48.2,
  "elevation": [{"distanceM": 0, "altitudeM": 620}, {"distanceM": 1000, "altitudeM": 650}],
  "forecast": {
    "method": "TRUCK_TERRAIN_HISTORY_V1",
    "tripId": "00000000-0000-4000-8000-000000000002",
    "routeId": "00000000-0000-4000-8000-000000000003",
    "routeRevision": 1,
    "modelVersion": "terrain-1",
    "confidence": "MEDIUM",
    "historySamples": 250,
    "fuelLiters": 84.5,
    "averageKml": 1.7,
    "balanceAtArrivalLiters": 90.5,
    "reserveLiters": 35,
    "refuelRequired": false,
    "cost": {"fuelCents": 60756, "operatingCents": 60756, "complete": true, "tollPayer": "CARRIER"}
  }
}
```

Distância, altitude, ascensão e descida são em metros. O perfil é ordenado por distância cumulativa e aceita até 1.200 pontos em cada resposta móvel. `forecast` pertence a um plano e a uma revisão. Publique `fuelLiters` como soma das previsões por segmento. `averageKml` corresponde à distância total coberta dividida pela soma de litros. Inclua moeda BRL e o preço de referência com data na evidência interna. Valores móveis são centavos inteiros. Classifique o custo comparável segundo o pagador `DRIVER`, `CARRIER` ou `UNKNOWN`, mantendo valores parciais visíveis e `complete:false` se faltar tarifa, preço, pagador ou outro dado necessário à comparação. Aplique o mesmo critério de custo e a mesma base de preço a todas as alternativas da consulta.

## Multimídia e acompanhamento

Use `GET /functions/v1/multimedia-next-v1/v1/state` já autenticado pela sessão e prova de dispositivo. Acrescente as projeções ao JSON existente para ambos os modos autorizados. O servidor pode obter rota, políticas e jornada pelas funções de sessão que já existem. Preserve as chaves no backend e omita valores financeiros quando a política não os liberar.

- `fuelRange` mantém os campos da função atual. Opcionalmente inclua `updatedAt` em ISO-8601 UTC ou com offset para a hora do cálculo da fonte. Hoje o app indica a hora da consulta quando só recebe `receivedAt`. Capacidade, saldo, referência de abastecimento, consumo, parciais e autonomia são fornecidos pela Cloud.
- `route` preserva a mesma identidade, geometria, elevação, pedágios e validade da rota vinculada. Acrescente `revision`, `remainingM` e `progressSampledAt` com posição recente projetada na geometria. `drivenPolylines` opcional contém até 32 polilinhas observadas e separadas por lacunas. O progresso deve considerar sentido, erro de GPS, desvios e ambiguidades de caminhos paralelos.
- `fuelHistory` contém somente a viagem autorizada e os intervalos observados. `throughKm` é o progresso medido desde o odômetro inicial, `sampledAt` a hora da fonte, e `source` um entre `PID250_RECONCILED`, `PID183_INTEGRATED` e `MIXED_DISJOINT`. Mantenha as linhas em ordem e disjuntas. Cada linha declara `fromKm`, `toKm`, `coveredKm`, `liters` e, quando disponível, `idleLiters`, `altitudeM` e `plannedLiters`. O último só se aplica a intervalos cobertos por uma previsão topográfica identificável e requer `predictionMethod: "TRUCK_TERRAIN_HISTORY_V1"`. Inclua até 4.320 linhas e prefira blocos consolidados com fronteiras de 50 km e cortes dos últimos 10, 50 e 100 km. Os clientes somam distâncias e litros dos intervalos selecionados para formar a média. Sobreposição ou lacunas precisam ser representadas na origem.
- `routeForecast` refere-se à viagem e à revisão da rota exibida. `next50` e `next100` limitam-se ao saldo de distância até o destino. Ausência de GPS utilizável, perfil ou base confiável retorna campos nulos. `reachM` mede alcance na rota à frente com a reserva configurada. Informe se o alcance chegou ao destino e publique o déficit quando houver.

Exemplo sintético de `routeForecast` quando restam 35 km.

```json
{
  "method": "TRUCK_TERRAIN_HISTORY_V1",
  "tripId": "00000000-0000-4000-8000-000000000002",
  "routeId": "00000000-0000-4000-8000-000000000003",
  "routeRevision": 1,
  "modelVersion": "terrain-1",
  "confidence": "MEDIUM",
  "historySamples": 250,
  "computedAt": "2026-11-25T18:00:00Z",
  "horizons": {
    "next50": {"distanceM": 35000, "liters": 20, "averageKml": 1.75, "ascentM": 50, "descentM": 41, "terrain": "MIXED", "fuelAtEndLiters": 51},
    "next100": {"distanceM": 35000, "liters": 20, "averageKml": 1.75, "fuelAtEndLiters": 51},
    "destination": {"distanceM": 35000, "liters": 20, "averageKml": 1.75, "fuelAtEndLiters": 51}
  },
  "smartRange": {"reachM": 35000, "reserveLiters": 35, "deficitLiters": 0},
  "comparison": {
    "originalAt": null,
    "comparisonStartedAt": "2026-11-25T15:00:00Z",
    "plannedToPositionLiters": 81,
    "observedLiters": 82,
    "differenceLiters": 1,
    "differenceCents": 719,
    "projectedTotalLiters": 102,
    "projectedDifferenceLiters": 1,
    "projectedDifferenceCents": 719
  }
}
```

`terrain` pode ser `ASCENT`, `DESCENT`, `FLAT` ou `MIXED`. Use `HIGH`, `MEDIUM`, `LOW` para confiança e publique suporte amostral e cobertura na evidência interna. Baixa confiança pode usar um modelo conservador parametrizado e validado, sempre baseado em perfis e fontes identificáveis. O Motorista e a Multimídia rejeitam previsões de método desconhecido ou de outra revisão. Valores de marcha lenta já fazem parte do total do mesmo intervalo, sem acréscimo na soma.

## Modelo, histórico e revisões

1. Percorra o traçado na ordem de viagem e reconstrua a distância acumulada por seção. Calcule subidas, descidas e rampas sobre amostras ordenadas com tolerância ao ruído. Guarde limites em metros ao longo da geometria, altitude, inclinação, tipo de via, direção e motivo de qualidade por segmento. Retenha o bruto com a política de armazenamento do provedor.
2. Ligue o intervalo percorrido ao veículo, horário, posição, rodovia e sentido verificados. Use carga, peso bruto quando medido, condição vazia ou carregada, velocidade, RPM, torque, marcha e tempo de lenta se observados. Reveja a memória de percurso existente e enriqueça apenas intervalos históricos com GPS utilizável. Os sinais e os registros brutos permanecem disponíveis para auditoria.
3. Use diferenças do PID 250 do mesmo contador e intervalo quando a origem, o odômetro, a unidade e a continuidade forem compatíveis. Complete os trechos disjuntos por integração ponderada do PID 183. Separe reinícios, saltos, lacunas e instantes sem comunicação. Meça lenta quando houver evidência temporal de veículo parado e motor em funcionamento. Preserve a origem de cada intervalo.
4. Compare primeiro a mesma estrada, sentido e faixa de peso daquele veículo. Depois compare terrenos e cargas equivalentes em outras estradas do mesmo caminhão. Em seguida use histórico agregado por relevo e carga. Use prior conservador validado e de baixa confiança somente quando houver sustentação para os parâmetros e para o perfil adiante. Guarde quantos quilômetros e litros suportam cada escolha. Nunca adote a média acumulada em serra para trechos planos distantes.
5. Persista previsão por segmento e versão junto com o observado, cobertura e erro. Recalibre somente com intervalos válidos, mantenha aprendizado individual por caminhão e monitore erro por classe de carga, inclinação, sentido e fonte de combustível.
6. Registre uma revisão imutável ao vincular ou trocar uma rota. Atualize o ponteiro de vínculo existente em transação, condicionada à viagem ativa. Preserve previsões e revisões anteriores. A primeira previsão depois da partida recebe referência de hora e de progresso. Mantenha `originalAt:null` quando não existia previsão inicial. Compare previsto e realizado sobre a mesma janela e registre os desvios e o novo restante separadamente.

## Atualização, cache e validação

- A Multimídia consulta o snapshot existente a cada dez segundos. Atualize o ponteiro da rota, progresso, horizontes e alertas quando houver leitura nova pertinente. Reuse a mesma geometria e os segmentos armazenados até que a revisão, o perfil, a configuração física, o percurso ou a validade exigir nova consulta.
- Mantenha a resposta móvel abaixo do limite atual de 512 KiB. Use geometria simplificada e dados compactos, preservando curvas e quebras. Alvo de latência do snapshot abaixo de um segundo no servidor para dados armazenados. Evite consulta HERE, treinamento, varredura longa de telemetria ou enriquecimento topográfico por tick de GPS.
- A seleção explícita e o recálculo por desvio usam o endpoint atual. Mantenha limite por veículo e cache por rota, opções, dimensões, peso, pontos intermediários e versão do provedor. Cálculo de novos planos pode demorar mais que o snapshot. Devolva dados parciais com disponibilidade explícita quando necessário.
- A Multimídia guarda o último saldo, histórico recebido, traçado e previsão verificados em memória durante a sessão. Na perda de rede, os dados retidos carregam a idade da última consulta e expiram com a autorização ou validade da rota. Logout, recusas e troca de identidade limpam o conteúdo. A preservação após fechar o processo exige um contrato de cache offline vinculado ao veículo e ao dispositivo com prazo, revogação e conteúdo permitido. O consumo local continua pelo Core, independentemente da Cloud.
- Para atualização local futura de horizontes, envie segmentos pré-calculados com `fromM`, `toM`, `predictedLiters`, `ascentM`, `descentM`, fonte, confiança e versão. Com GPS local utilizável e posição projetada na revisão correta, o app pode recortar valores já calculados pela Cloud. Sem esses dados, retenha o último cálculo e sua idade.
- Homologue com viagem sintética iniciada numa serra, trecho plano à frente e alternativas com comprimentos e consumo distintos. Verifique que o modelo não usa o valor médio acumulado para toda a rota, que trechos sem topografia ou cobertura mostram ausência, que pedágio incompleto não gera ranking de custo, e que a troca de rota preserva a revisão original e os contadores. Leia a viagem real para conferir compatibilidade sem executar `plan_truck` ou `attach` de teste nela.
