# TruckBox Rev.B.1 Lite 0.2.31 · Consumo contínuo J1708

O Core integra a vazão passiva do PID 183 a cada quadro válido e fecha janelas próprias de 10 s. O Link grava e reenvia as janelas. A Cloud usa os litros já integrados e confere o acumulado com o PID 250. A versão está preparada para homologação. A bancada física e a ativação no Volvo estão pendentes.

| Entrega | Arquivo |
| --- | --- |
| Core Lite 0.2.31 OTA | [BIN do Core](truckbox_core_0.2.31-lite_ota.bin) |
| Link Lite 0.2.31 OTA | [BIN do Link](truckbox_link_0.2.31-lite_ota.bin) |
| Integridade e ensaios | [SHA-256](SHA256SUMS.txt) · [Resultados](TESTES.txt) |
| Supabase | [Migração de combustível](../../espidf/cloud/migrations/20261122215843_truckbox_fuel_continuous.sql) · [Patch do truckbox-ingest v27](../../espidf/cloud/truckbox-ingest-continuous-v27.patch) |
| APK Next 0.2.3 Consumo | [APK de homologação](TruckBox_Next_0.2.3_Consumo.apk) · [Código e projeção da multimídia](https://github.com/rbasniak1989-lab/Truckbox/tree/feat/revb1-continuous-fuel-next) |

## Causa e comparação numérica

Na 0.2.27, o Link enviava um snapshot aproximadamente a cada 10 s com `fuel_rate_lph` da leitura mais recente. A função `truckbox_apply_trip_rate_tail` usava a média das duas amostras vizinhas vezes o intervalo. `truckbox_rebuild_trip_fuel_rate` repetia o mesmo cálculo no histórico. A 0.2.30 já integrava o PID 183 no Core e exportava acumuladores, mas o início e o fim da janela exibida seguiam a consulta do Link. A 0.2.31 define as fronteiras no Core e transmite o registro consolidado.

| Trecho de ensaio com instantes conhecidos | Uma leitura final por 10 s | Cálculo legado com duas leituras | Janela do Core |
| --- | ---: | ---: | ---: |
| 9 s a 2 L/h e 1 s a 50 L/h | 0,138889 L | 0,072222 L | **0,018889 L e 6,80004 L/h** |
| 4 s a 10 L/h, 5,25 s a 80 L/h e 0,75 s a 36 L/h | 0,100000 L | 0,063889 L | **0,135278 L e 48,70008 L/h** |

No segundo ensaio, a próxima leitura chegou em 11,25 s. O Core atribuiu 0,75 s da vazão anterior à janela que terminou em 10 s e 1,25 s à seguinte. A comparação usa exatamente os mesmos quadros sintéticos. O cálculo legado considera a vazão em cache no início e no fim do trecho.

A consulta em produção foi somente leitura. Entre 21:22:40 e 21:34:00 UTC de 22/11, a Cloud tinha 68 amostras do trecho com vazão e mediana de 10,001 s entre snapshots. As 68 tinham PID 250 marcado como provisório. No histórico anterior, 141 registros `J1708_FAST` traziam PID 183, com mediana de 9,998 s entre registros. São saídas publicadas em lote. Os quadros passivos com carimbo individual e os eventos de TX/resposta do PID 250 para o trecho atual estavam ausentes na consulta. Por isso a cadência física do barramento e uma comparação medida no mesmo trecho do Volvo continuam pendentes.

## Medição no Core e transporte

- O decodificador entrega o PID 183 do MID 128 com tempo monotônico em milissegundos. O Core retém a última vazão válida até a próxima leitura válida, com no máximo 3 s entre as duas. Leituras negativas, inválidas ou acima de 1.000 L/h quebram a cobertura. Esse limite de 3 s precisa ser confrontado com a cadência do Volvo.
- A duração de uma janela é sempre 10.000 ms no relógio monotônico. O Core registra litros em microlitros a partir do acumulador de ponto fixo, tempo coberto, contagem, mínima e máxima das leituras, e total do boot. Uma fronteira em meio a duas leituras válidas é dividida no ponto correto. Em silêncio, a janela fecha após uma espera de até 3 s acrescida do período da tarefa supervisora. A média é `litros × 3600 / 10` quando a janela tem cobertura completa. Janelas parciais exibem litros observados e cobertura.
- Cada registro tem identidade composta por dispositivo, Core, boot e número da janela. O Core conserva as últimas 12 janelas. O Link solicita até três por consulta e avança o cursor quando o journal durável confirma a escrita. A fila de 1 MiB retém as amostras durante a queda de Internet. Se o Link reiniciar e reapresentar uma janela, a identidade da janela no banco retira a duplicata inclusive quando o novo envio usa outra sequência HTTP.
- Onde duas janelas conhecidas do mesmo boot enquadram uma lacuna de transporte, a Cloud consegue recuperar o volume observado pela diferença dos acumuladores, dentro dos limites físicos e de tempo. A cobertura do trecho ausente permanece indeterminada. Se as janelas reaparecerem, substituem esse volume recuperado sem nova soma. Janelas que ainda estavam apenas na RAM podem se perder numa queda do Core. O journal do Link e o banco preservam tudo que já confirmaram.
- O contrato compacto em `raw.w183[]` usa os oito campos `[slot, fim_utc_ms, microlitros, tempo_coberto_ms, leituras, mínima_mL_h, máxima_mL_h, acumulado_boot_uL]`. Se houver contador, seguem `[uptime_pid250_ms, ticks, mid, flags, acumulado_pid183_no_contador_uL, cobertura_no_contador_ms]`. Os bits 1 e 2 de `flags` representam correlação temporal com TX e origem/unidade validada na configuração. PID 250 e odômetro continuam também disponíveis nos contratos existentes.
- O Core pode marcar o acumulador local da viagem quando recebe um `START` contemporâneo pela rota de referência existente. Uma repetição do mesmo identificador conserva a referência. Com odômetro validado e fresco, a multimídia Next pode mostrar distância, litros e média local mesmo sem Internet. A referência local é volátil e independente das referências oficiais. A média oficial da viagem em andamento depende da consolidação na Cloud, inclusive quando ainda falta referência eletrônica local daquela viagem.

## PID 250, calibração e referência manual

O scheduler de 10 s e as proteções de transmissão seguem o comportamento existente. O diagnóstico local separa TX observado pelo scheduler, respostas candidatas, valores efetivamente decodificados/correlacionados, idade do cache, última correlação válida, latência, timeout, erro de TX e conflito. O vínculo temporal é evidência operacional. A validação da origem no veículo continua exigida antes de habilitar o uso contábil.

A resolução é **0,473176473 L por incremento**. A reconciliação usa pares de referências da mesma origem e boot, em intervalos de pelo menos 300 s, 10 incrementos e 95% de cobertura. Regressão, troca de MID ou de boot iniciam uma nova série. Um contador inalterado permite que a estimativa continue subindo. O desvio dentro de um incremento fica na faixa de incerteza. Fora dela, usa-se somente a correção sobre o trecho observado, limitada a 5% do volume medido, desde que as duas fontes permaneçam na faixa de plausibilidade. A Cloud usa o último trecho verificável e preserva a correção já incorporada quando a leitura perde validade. Os litros acumulados da ECU entram como referência de comparação.

A viagem Guarani/Mamborê-PR → São Francisco do Sul-SC está ancorada em **742 km e 542 L**, média de **1,369 km/L** no ponto do ajuste. A base de distância em uso é **742 km** às **21:22:39,609642 UTC de 22/11/2026**, quando a referência do odômetro era **1.205.072,442 km**. Na leitura durante o desenvolvimento, já havia **742,1609 km e 542,7665 L** no acumulado em produção. O SQL valida a combinação de viagem, caminhão, âncora e data, e congela o acumulado efetivamente lançado no momento da instalação. Janelas anteriores ou que atravessem esse corte permanecem fora da nova soma. A Edge conserva a seleção de odômetro e o limite de plausibilidade da v27. Uma nova intervenção manual registrada tem precedência e suspende a publicação automática até que a referência seja estabelecida novamente.

A fonte `J1587_PID183_WINDOW_CORE_V2` identifica a soma nova. A projeção leva o estado de cobertura e a qualidade até a multimídia Next. O contrato legado continua válido para amostras antigas, e numa reversão volta a integrar entre pontos legados somente se o intervalo não coincide com janelas do Core. Rebuild da viagem protegida retoma da referência gravada. Viagens encerradas pela trilha nova conservam seus totais. A recuperação perto do encerramento deve ocorrer antes de fechar a viagem de teste, para que as últimas janelas sejam atribuídas à operação ativa.

## Testes e limites

- O acumulador mediu **864 L em 24 h simuladas** a 36 L/h, usando **1.088 B** para a estrutura fixa. Cruzamento de fronteira, leitura inválida, silêncio, relógio monotônico, referência local e recuperação de páginas passaram.
- O Link preservou **2.100 registros em 5 h simuladas** usando **950.400 B**, com reinicialização e corte de gravação. O replay não perdeu nem duplicou registros admitidos. Com 1 MiB e o fixture cheio, a primeira perda de saúde ocorreu em 310,9 min e o primeiro descarte operacional em 331,1 min. A duração física varia conforme o preenchimento e o espaço livre.
- PostgreSQL isolado verificou avanço a partir de 742/542, deduplicação, perda de uma janela, recuperação pelo acumulador, chegada atrasada, reboot, contador regressivo, um degrau de PID 250, reconciliação longa, queda de validade, fallback legado e proteção no fechamento. A Edge v27 com Supabase simulado preservou a âncora de distância, gravou `w183`, confirmou o envio e manteve o snapshot mais novo no painel.
- A suíte de firmware foi executada com ASan/UBSan em host. Os ensaios de 24 h e replay são simulados. Heap livre e mínimo, maior bloco contíguo, watchdog, cadência passiva real e latência do PID 250 precisam de verificação no ESP32-C6 físico.
- As imagens são compiladas com ESP-IDF 5.5.2 e a tabela de partições existente. Em relação à 0.2.30, a D/IRAM estática aumentou cerca de 1,2 KiB no Core e 0,9 KiB no Link. Use os mapas de [TESTES.txt](TESTES.txt) para os valores exatos. A nova estrutura dispensa lista de quadros e escrita de flash por leitura.
- O APK passou pelos testes JVM, lint, checagem do pacote e testes isolados dos dois canais Cloud. Seis capturas de 1920×1080 reproduziram pixel a pixel a interface aprovada no mesmo estado dos dados de teste. O pacote `br.com.truckbox.multimedia.next.gps.consumo` usa assinatura de homologação e instala ao lado da versão anterior. [Workflow da multimídia](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/45076468458)

## Homologação e instalação

1. Guarde os BINs anteriores, o último ACK do Link e uma cópia da definição das duas funções de consumo e do estado da viagem. As migrações conferem as assinaturas das funções locais antes de permitir a aplicação. A migração de consumo precisa preceder o primeiro firmware que envia `w183`.
2. Em ambiente de homologação, aplique a migração do cálculo e o patch sobre um arquivo da Edge `truckbox-ingest` v27 cuja fonte tenha SHA-256 `045f0cf2db4fc51071fc69a3d31ebe54fbe097594ef70b52056c3005f4a874f5`. Veja [patch](../../espidf/cloud/truckbox-ingest-continuous-v27.patch). A projeção Next tem uma [migração própria para o acesso de campo](https://github.com/rbasniak1989-lab/Truckbox/blob/feat/revb1-continuous-fuel-next/backend-proposal/v1/supabase/migrations/20261122220644_multimedia_fuel_projection.sql) e o código do serviço regular. Aplique e valide ambos no ambiente de teste autorizado.
3. Com o veículo estacionado ou em bancada, confirme `sha256sum -c SHA256SUMS.txt`. Faça OTA no Link primeiro e depois no Core, somente com as imagens de aplicativo do papel correspondente. Verifique que os dois exibem `0.2.31-lite`, há comunicação local e o Link avança o ACK. O APK Next de homologação usa pacote separado para poder testar ao lado da versão instalada.
4. Guarde um trecho com variação de carga. Faça a coleta sem interação do motorista em movimento. No Core, registre `GET /api/fuel` antes e depois da rodada. Por janela, confira `window.duration_ms=10000`, cobertura, contagem de atualizações, mínimo, máximo, litros e média. Os totais `valid`, `invalid`, `gaps` e os espaçamentos do PID 183 indicam se o intervalo de validação é adequado. Uma captura passiva com timestamp por quadro permite comparar a integração dos mesmos 10 s com o método antigo.
5. Em `/api/fuel`, compare `pid250.sent`, `matched_valid`, `last_valid_match_uptime_ms`, `last_valid_match_latency_ms`, `age_ms`, `flags`, `timeouts` e `guard_reason`. A diferença nos contadores entre duas leituras identifica requisições realmente executadas naquele período. A ausência de TX ou de origem validada mantém a estimativa.
6. Corte só a saída para Internet durante 15 min. Deixe a LAN ativa e anote ocupação, heap mínimo e boot do Link. Restaure o acesso e espere a fila esvaziar. Compare as sequências e os identificadores de janela abaixo. Confira `fuel_window_summary` da viagem, incluindo `baseLitres`, `manualLitres`, `integratedLitres`, `recoveredLitres`, `reconciliationLitres`, `quality` e a média. Teste também uma troca de boot e a visita aos monitores do J1708, OTA e GPS.

```sql
select s.ts,s.seq,s.raw->>'boot_id' as boot,w.value->>0 as janela,
       (w.value->>2)::numeric/1000000 as litros,w.value->>3 as coberto_ms
from public.telemetry_samples s
cross join lateral jsonb_array_elements(s.raw->'w183') w
where s.trip_id='<id-da-viagem-de-teste>'::uuid
  and s.ts>=now()-interval '1 hour'
order by s.ts,s.seq,(w.value->>0)::bigint;
```

## Reversão

Preserve as filas, a NVS e o estado da viagem. Se precisar retornar aos BINs anteriores, drene primeiro as janelas pendentes sempre que houver Internet. Volte ao firmware pelo painel OTA com o veículo estacionado. A migração do banco continua protegendo a âncora e permite que os pontos legados voltem a contribuir em intervalos próprios. A tabela de janelas e a referência continuam disponíveis para auditoria. Restaurar as funções legadas diretamente durante a viagem reintroduziria a reconstrução histórica.

## Reprodução

Fonte de firmware `0201560b928a6813be159c8808c41290eafdae88`. Execute a partir de `firmware/espidf` após ativar o ESP-IDF 5.5.2 para ESP32-C6.

```sh
idf.py -B build-core-231 -DTRUCKBOX_ROLE=core -DTRUCKBOX_REVB1_LITE=ON \
  -DTRUCKBOX_REVB1_CONFIRMED_CORE_UID= -DPROJECT_VER=0.2.31-lite build
idf.py -B build-link-231 -DTRUCKBOX_ROLE=link -DTRUCKBOX_REVB1_LITE=ON -DPROJECT_VER=0.2.31-lite build
sh tests/run_all.sh
npm install --prefix /tmp/truckbox-fuel-test @electric-sql/pglite
TRUCKBOX_TEST_PGLITE=/tmp/truckbox-fuel-test/node_modules/@electric-sql/pglite node cloud/test_continuous_fuel.mjs
patch -o /tmp/truckbox-ingest-native.ts /caminho/local/index.ts < cloud/truckbox-ingest-continuous-v27.patch
node cloud/test_ingest_mock.mjs --source /tmp/truckbox-ingest-native.ts --v25 --v26 --verify-fuel-window --verify-continuous-fuel
```
