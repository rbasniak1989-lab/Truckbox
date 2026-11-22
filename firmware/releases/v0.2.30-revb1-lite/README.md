# TruckBox Rev.B.1 Lite 0.2.30

O Core integra as leituras do PID 183 do MID 128. O Link calcula a média de vazão entre consultas, nominalmente a cada 10 s. A Cloud soma os volumes medidos nesses intervalos e confirma a média da viagem no encerramento com a diferença do PID 250 e do odômetro. A agenda do PID 250 continua em 10 s no perfil normal.

| Arquivo | Uso |
| --- | --- |
| [Core Lite 0.2.30](truckbox_core_0.2.30-lite_ota.bin) | Imagem OTA do Core ESP32-C6. Adiciona o integrador local e a origem dos contadores ao snapshot. |
| [Link Lite 0.2.30](truckbox_link_0.2.30-lite_ota.bin) | Imagem OTA do Link ESP32-C6. Encaminha a medição e mostra a janela no diagnóstico. |
| [SHA256SUMS.txt](SHA256SUMS.txt) | Hashes das imagens de aplicativo. |
| [TESTES.txt](TESTES.txt) | Resultados dos ensaios e cabeçalhos dos BINs. |
| [Revisão SQL](../../espidf/cloud/fuel_window_review.sql) | Agregação das janelas de vazão e validação do PID 250 no encerramento. |
| [Reversão SQL](../../espidf/cloud/fuel_window_rollback.sql) | Restaura as funções anteriores e retira o gatilho da revisão. |

## Origem da divergência

Na versão antiga com J1939, o `FuelPowerAccumulator` do Android usava torque e RPM do PGN 61444 para estimar potência, convertia para L/h e integrava os intervalos entre quadros. A operação calculava os litros pela diferença entre acumulados. A viagem de 22 de setembro analisada usou 460,66992 L (`3882,26831 − 3421,59839`) em 734,5 km, ou 1,59 km/L. [Decodificador antigo](https://github.com/rbasniak1989-lab/Truckbox/blob/main/app/src/main/java/br/com/truckbox/app/parser/TruckBoxJ1939Decoder.kt)

Na versão Lite anterior, a Cloud integrava a partir da vazão instantânea dos snapshots. A diferença entre as leituras dentro de cada intervalo ficava invisível para esse cálculo, e a integração pulava lacunas longas. Na viagem examinada, a Cloud registrava 416,03 km, 201,95 L estimados e 2,06 km/L, com fator de calibração 1,0. Dois intervalos sem vazão aproveitável somaram 207,77 km de avanço observável do odômetro sem litros correspondentes. Os 567 registros Lite identificados com PID 250 estavam marcados como provisórios, e faltava a referência inicial da viagem. O volume desses trechos e a média confirmada da viagem ficam indeterminados pelas leituras disponíveis.

## Cálculo e qualidade dos dados

- O Core conserva o instante, o tempo coberto e os litros acumulados desde o boot em RAM. Integra a última vazão pelo tempo até a leitura válida seguinte. Uma lacuna entre quadros superior a 3 s ou uma leitura inválida interrompe a cobertura. A média da janela é `litros medidos × 3600 ÷ segundos cobertos`. Por exemplo, 20 L/h por 2 s e 80 L/h por 8 s dão **68 L/h e 0,188889 L**. Se a janela só tiver cobertura parcial, a média se refere à parte observada.
- `fuel_rate_lph` continua sendo a vazão instantânea. `telemetry_collector.pid183_window` no Link traz `span_ms`, `covered_ms`, `litres` e `average_lph` quando disponíveis. `raw.f183` leva os três acumuladores na amostra persistida e o SQL calcula `raw.pid183_window` na agregação da viagem. Um intervalo com o mesmo boot pode recuperar volume que o Core mediu mesmo quando o Link ficou momentaneamente sem snapshots. Entre boots, o intervalo sem referência fica sem volume imputado.
- A média de viagem estimada usa a distância e o diesel somado das janelas válidas. O rótulo da origem é `J1587_PID183_WINDOW_CLOUD_V1`. Quando falta um par de fechamento, os dados estimados que a Cloud já calculou prevalecem sobre valores locais antigos enviados pelo cliente. Ajustes manuais registrados têm precedência.
- Para confirmar a média no encerramento, o PID 250 e o odômetro de cada ponta precisam estar validados na configuração do Core, ter até 5 s de diferença entre si e ficar a até 30 s do início ou do encerramento. A leitura final precisa preceder o fechamento. As duas pontas precisam vir do mesmo dispositivo, Core e MIDs, com odômetro a até 300 m dos limites da viagem, pelo menos 10 km entre referências e contador sem regressão observada. O resultado confirmado é `Δ odômetro ÷ Δ PID 250`, gravado como `J1587_PID250_ECU_TOTAL_DELTA`. A comparação com o estimado pode usar `Δ PID 250 ÷ litros estimados` da mesma viagem. Esse quociente fica para conferência e nenhum fator é gravado automaticamente para as próximas viagens.
- O SQL mantém o cálculo legado quando os registros só têm vazão instantânea. O endpoint `truckbox-ingest` v26 recebeu os novos campos em `raw` no teste, com ACK válido. O painel da multimídia Next tem uma projeção própria de consumo verificado. O novo cálculo é aplicado nos registros de viagem da Cloud após a revisão SQL. A publicação nessa projeção segue uma etapa separada do serviço da multimídia.

## Ensaios e recursos

| Ensaio | Resultado |
| --- | --- |
| Suíte ESP-IDF em host | Aprovada com ASan e UBSan. A coleta J1708, o scheduler do PID 250 e as rotinas de recuperação passaram. O detector de leaks ficou desativado no ambiente. |
| PostgreSQL isolado | 68 L/h ponderados, cobertura parcial, replay e duplicatas, reinício do Core, contador provisório ou regressivo, ajuste manual, fechamento e reversão. |
| Edge v26 em teste com Supabase simulado | Campos `f183` e `a250` persistidos integralmente, ACK correspondente e chamada da agregação. |
| Journal offline com campos novos e sensores preenchidos | 320 min, 2.240 registros, 939.330 B e 320 lotes, com corte simulado na gravação e sem perda ou duplicata entre registros admitidos. |
| Cota de 1 MiB no cenário denso | Primeiro descarte de saúde aos 334,9 min. Primeiro descarte operacional aos 355,0 min. A retenção física depende do preenchimento e do espaço livre do Link. |

O mapa do Core passou de 194.006 para 194.046 B de D/IRAM estática. O mapa do Link passou de 171.192 para 171.416 B. O limite da fila permanece em 1 MiB de flash, sujeito ao espaço útil do volume. Os BINs têm 1.436.992 B e 1.514.096 B. Ambos cabem nos slots de 3 MiB e foram inspecionados como ESP32-C6. O mapa de partições é idêntico ao da 0.2.29. O próximo ensaio físico precisa medir heap mínimo, maior bloco livre e evolução dos contadores com rede e multimídia ligadas. [Referência 0.2.29](../v0.2.29-revb1-lite/README.md)

## Instalação e verificação

1. Teste primeiro numa operação de homologação. Guarde os BINs anteriores, o status de ambos os ESPs, o último ACK do Link e as funções atuais do SQL. A revisão valida a assinatura das duas funções capturadas e aborta se a versão de origem divergir. A Cloud de produção ainda usa o cálculo anterior.
2. Com os equipamentos em bancada ou com o caminhão estacionado, confira `sha256sum -c SHA256SUMS.txt`. Pelo painel OTA, carregue a imagem do Link no Link e depois a do Core no Core. Confira `version=0.2.30-lite` e `role` de cada placa após o boot. As imagens são de aplicativo para OTA e usam a tabela de partições existente.
3. No Link, confira `core_online`, avanço de `last_durable_ack` e `telemetry_collector.pid183_window`. Use um período com variação de vazão e confira tempo total, tempo coberto e litros. Na Cloud de teste, confira os mesmos contadores no `raw.f183` e, com a revisão aplicada, o cálculo em `raw.pid183_window`.
4. Valide origem, unidade e continuidade do PID 250 e do odômetro no veículo. Confirme as duas leituras pelo procedimento de configuração existente. Antes de fechar a viagem de teste, espere o Link confirmar o envio recente. Com as duas pontas disponíveis, confira os deltas e `fuel_source` do registro encerrado. Faça um segundo teste com apenas a vazão e confirme a origem estimada. Aguarde o próximo ciclo de agregação da Cloud ao conferir as amostras em atraso.
5. Registre os resultados na bancada ou em teste de rodagem acompanhado. Verifique a curva de vazão, os totais da viagem, a recepção passiva, os contadores de requisições J1708, a continuidade da multimídia e a recuperação da fila após queda de Internet.

## Rollback

Deixe o Link drenar a fila se a conexão permitir, registre o último ACK e volte às imagens anteriores pelo painel OTA. O formato dos segmentos é compatível com a 0.2.29. Preserve NVS, FAT e os slots OTA. Se a revisão SQL tiver sido aplicada, rode a reversão no mesmo ambiente autorizado para restaurar as duas funções e retirar o gatilho. Ela conserva os registros e resultados gravados. [Link 0.2.29](../v0.2.29-revb1-lite/truckbox_link_0.2.29-lite_ota.bin) · [Core 0.2.28](../v0.2.28-revb1-lite/truckbox_core_0.2.28-lite_j1708_ota.bin)

## Reprodução

Fonte `1e4ae6dc3e77d18b00100611e658491bd1a6456d`, alvo ESP32-C6, ESP-IDF 5.5.2. Execute na pasta `firmware/espidf`.

```sh
idf.py -B build-core-lite-230 -DTRUCKBOX_ROLE=core -DTRUCKBOX_REVB1_LITE=ON \
  -DTRUCKBOX_REVB1_CONFIRMED_CORE_UID= -DPROJECT_VER=0.2.30-lite build
idf.py -B build-link-lite-230 -DTRUCKBOX_ROLE=link -DTRUCKBOX_REVB1_LITE=ON \
  -DPROJECT_VER=0.2.30-lite build
sh tests/run_all.sh
npm install --prefix /tmp/truckbox-fuel-tests @electric-sql/pglite
TRUCKBOX_TEST_PGLITE=/tmp/truckbox-fuel-tests/node_modules/@electric-sql/pglite node cloud/test_lite_fuel_windows.mjs
node cloud/test_ingest_mock.mjs --source /caminho/local/truckbox-ingest-v26.ts --v25 --v26 --verify-fuel-window
```
