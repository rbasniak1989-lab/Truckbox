# Core Rev.B.1 0.2.25 — auditoria e resultado de software

Data:15/11/2026. Candidata, não instalada. Branch `fix/revb1-v0225-native-polling`. Hardware ESP32-C6/8MB; ESP-IDF5.5.2; GCC RISC-V14.2.0. Perfil J1708_ONLY preservado, código J1939 mantido. Nenhum reset/OTA/configuração de produção, transmissão no veículo, deploy ou alteração de viagens foi executado pelo desenvolvimento.

## Fonte correta e causa reproduzida

A0.2.24 instalada corresponde à PR27, commit `a9cd6bddfa6c1d176a7f8f9a8b46855dcd7ab3aa`. O artefato da execução Actions43645022308 tem SHA256 `2703ac418c1c7c88dd0ecdd398c2cc5161140043879683b5b15a6769883c508b`; seu ELF `db58841eac1f12a73d1f30db39f4ed73f14ca3ecc50f639eb4b507a13acf1916` coincide com o boot602 na Cloud. Não foi adotada uma versão antiga por semelhança do número. `main` não continha os fontes correspondentes.

Às02:47:55 de Brasília, `rx=AC80218231` era idêntico ao TX informado, mas o contador estava em4/5bytes e a montagem terminou21.589µs após o início. No código antigo:

1. `tbx_j1708_active_tick()` abortava o TX aos20.000µs; isso deve continuar.
2. A exclusão do eco no quadro completo exigia que todos os bytes tivessem sido reconhecidos enquanto TX estava ativo. Um quinto byte processado depois do aborto não completava esse contador.
3. `finish_j1708()` interpretava o quadro com MID172 como externo, registrava `jguard` em NVS e parava o scheduler.
4. `request_task()` fornecia ao controlador automático `local_conflict` a partir dos MIDs observados/guarda persistente. Nos boots seguintes o modo desejado podia continuar ON, mas `tbx_j1708_auto_tick()` permanecia BLOCKED.

O replay com os fontes **anteriores à correção** reproduziu: `echo=4/5`, `echo_timeouts=1`, `persistent_guard=172`, `safety_stopped=1`, `armed=0`. A reprodução corrigida cobre1..4/5bytes, preserva o aborto elétrico, reconhece uma única cópia exata montada tardiamente e mantém os demais PIDs operando. Não incrementa `echo_ok` por essa identificação tardia.

`candidate_only` e os predicados frame/crc/persist do evento eram informações do monitor estatístico de RX, não critérios independentes que comprovassem a existência de outro transmissor com MID172. O evento é compatível com falso positivo e a trajetória de software foi reproduzida. Não há captura elétrica independente para excluir toda colisão física ocorrida naquele instante.

## Alterações direcionadas

- `tbx_j1708_active.c`: token de exclusão de uma cópia exata do TX entregue à UART, com validade limitada ao timeout da transação; vida útil separada da energização do driver. Mismatch continua liberando TX imediatamente. Problemas do eco e respostas ausentes não fazem parada global do polling nativo.
- `capture.c`, `tbx_j1708_auto.c`: antiga guarda persistente preservada como evidência, ignorada como autorização de bloqueio da produção. Nenhuma gravação automática OFF após suspeita. MID salvo respeitado inclusive após edição; reconfiguração entre transações e nova observação RX. Ensaios manuais continuam separados da produção.
- `tbx_j1708_requests.c`: um único scheduler; falha de um PID é contabilizada nele e a próxima tentativa respeita sua cadência, sem retentativa imediata que monopolize a fila. Sem mudanças nos bytes de requisição ou nos destinos existentes.
- `tbx_j1708_rx_guard.c`: alteração estatística de checksum/framing vira diagnóstico e redução de ritmo na produção, sem atribuição automática ao TX. Perda real de RX, UART cega enquanto TX, maioria inválida persistente, relógio inválido e parada manual continuam protegidos; recuperação exige medidas recentes saudáveis.
- `config.c`: migração `v225=critical-10s-v1`, gravada uma vez. Só altera117/118/250 nas duas tabelas de cadência. Preserva MID, alvos, outros períodos, credenciais e desativação explícita. Configuração não é escrita a cada ciclo.
- `tbx_fuel_account.*`, `trip_checkpoint.h`, `accounting_json.h`: média com extremos odômetro/combustível compatíveis, sem usar contador de vida como combustível da viagem e sem criar segundo total independente. Metadados adicionais A/B com CRC; layout v4 existente intacto. Captura do odômetro SAE do MID configurado passa a ser utilizada tecnicamente mesmo com validação física pendente, mantendo a qualidade EM VALIDAÇÃO.
- `telemetry.c`:117/118/250 no conjunto de10s; raw/MID/idade preservados. Resposta velha não é recarimbada como nova. API `telemetry_v2.critical` usa os MIDs configurados e marca DESATUALIZADO após10s; origens alternativas continuam no cache com seu MID.
- `tb_web.c`: diagnóstico mínimo sem cJSON quando o orçamento livre é inferior a40KiB ou maior bloco inferior a1KiB. Indicadores de memória/RX/TX continuam disponíveis. Fora dessa condição permanece o diagnóstico compacto e a paginação existentes. Nenhuma alteração de layout.

Continuam: taxa global máxima2consultas/s, uma pendente, mínimo500ms entre envios, idle de pelo menos5ms, checksum, destino e PID corretos, prazo450ms, liberação elétrica do TX em20ms, deferimento por barramento ocupado, suspensão OTA, parada manual, inicialização após RX saudável e política independente de internet/Android. Eco e resposta candidata não viram validação externa. Conflito físico comprovado exige investigação/correção da instalação; os eventos não são declarados sucesso.

## Frequências

As frequências não críticas abaixo são os padrões históricos; valores personalizados salvos são mantidos. Os três períodos críticos são migrados a10s uma vez. Expedições já existentes em parada/início/fim de operação continuam passando pela mesma fila e limite global.

|PID|MID destino|Normal|Arrancada|
|---|---:|---:|---:|
|26|128|60s|60s|
|31|130|10s|2s|
|32|130|30s|10s|
|33|130|5s|2s|
|34|130|15s|5s|
|37|130|30s|15s|
|62|144|60s|60s|
|63|130|15s|5s|
|68|128|60s|60s|
|108|128|60s|60s|
|117|140|10s|10s|
|118|140|10s|10s|
|158|128|60s|60s|
|160|130|30s|10s|
|235|128|900s|900s|
|236|128|900s|900s|
|250|128|10s|10s|

Candidatas já existentes, preservadas:93/MID12860s,94/12830s,107/128300s,153/12830s,154/128300s,161/13010s,182/128300s,188/128900s,244/128300s,439/128300s. Não foi ampliada a lista.

117/118/250 começam em fases1/4/7s após iniciar a produção. Os prazos seguem o ciclo de10s; prioridade/arbitragem podem adiar o envio. O teste específico com117 sempre silencioso mediu intervalos entre9,99 e11s no relógio virtual, sem interromper118/250. Isso é resultado de simulação, não latência garantida no caminhão. A carga média teórica dos27PIDs é aproximadamente1,12consulta/s normal e2,25/s na janela de arrancada, antes de eventuais expedições. As metas de arrancada somadas excedem o limite global de2/s: os parâmetros secundários são adiados, preservando117/118/250 e31/33 com prioridade. Não se promete cumprir simultaneamente todas as metas na janela acelerada. O tráfego efetivo pode ser menor.

## Combustível, média e persistência

PID183 integra `L/h × dt/3600` com timestamps monotônicos; lacunas permanecem estimadas/desconhecidas. PID250 do MID configurado, com bytes/escala/ordem válidos, substitui a estimativa do mesmo intervalo. A leitura inalterada é aceita; redução/salto/duplicata ou tempo incompatível não avançam a âncora. Entre boots exige referência UTC compatível para reconciliar um intervalo; sem ela preserva saldo e não inventa duração.

Os exemplos automatizados9L→10L e5L→4L terminam em14L confirmados;12L→13L termina em13L, nunca25L. A média consolidada usa a diferença do odômetro e o combustível confirmado dos mesmos extremos, exclui o combustível ainda provisório e não apresenta divisão por zero. Uma nova operação reinicia apenas a cobertura dessa média; não apaga os saldos gerais nem entrega a ela um intervalo inteiro que atravessou outra operação. A cobertura parcial é explícita.

O formato PID250 tem resolução aproximada de0,473176473L por incremento. Consultar a cada10s não aumenta essa resolução; em marcha lenta várias leituras consecutivas podem ser iguais e a média só muda quando há diferença válida. A média também depende de odômetro recente e coerente. Não há comprovação física nova das escalas nesta entrega.

Configuração continua na NVS v216/v223 com marcador v225; trip3a/b,trip4a/b e saldo legado continuam sendo escritos na cadência limitada do checkpoint existente. Os novos trip5a/b guardam somente metadados da média e CRC de vínculo ao checkpoint, sem duplicar a conta de combustível. O limite de10s do checkpoint já existia; uma queda pode perder a parte ainda não persistida. A próxima âncora válida pode reconciliar o total entre contadores, mas não inventa distribuição interna por trecho.

Cloud atual: leitura somente confirmou `truckbox-ingest`v26, com sobrescrita de projeções de viagem pelo cálculo `SPN183_RATE_CLOUD_V1`. Nenhum deploy foi feito. Os novos campos são aditivos no JSON bruto; os totais financeiros da Cloud ainda exigem revisão/migração separada de `cloud/README_FUEL_V2.md`. Não foi recalculado histórico.

## Memória e reinicializações

Comparação dos ELFs com mesma IDF/configuração: RAM estática211.554→211.874B, **+320B**. Não se declara economia estática. O ganho é limitar alocação transitória do diagnóstico: no host cJSON nativo, árvore cheia56.545B contra12.854B compacta; resposta HTTP compacta com reserva simulada de upload/checkpoint teve pico13.187B,200repetições e zero vazamentos. A resposta mínima funciona com zero heap disponível. São medições de host/compilador, não heap medido no ESP em viagem.

GCC `-fstack-usage` na captura: `persist_trip`1.088B, `request_task`560B, `j1708_task`208B, `snapshot_task`368B e `capture_json_mode`5.488B, sem somar chamadas internas. As stacks configuradas não foram aumentadas; HTTP10.240B e checkpoint3.072B permanecem. High-water real em campo continua pendente. HEALTH mantém heap mínimo e acrescenta heap atual/maior bloco; BOOT_DIAGNOSTIC e endpoint de crash retido existentes foram preservados.

A consulta de BOOTs desde14/11 encontrou na0.2.24:27POWERON e1SW, sem PANIC/WDT nesse conjunto. Na0.2.22 houve1PANIC. ESP-IDF5.5.2:1=POWERON,3=SW,4=PANIC,6=TASK_WDT. POWERON sozinho não determina se a causa é pós-chave, oscilação de tensão, EN/reset externo ou outro fator. Não há dump elétrico para atribuir a causa física. O boot606 teve heap mínimo1.648B com polling sem transmitir; isso confirma pressão de RAM independente do TX, não sua origem exata. O defeito reproduzido de diagnóstico é corrigido, mas não se afirma que todos os reinícios estão resolvidos sem dump/medidas.

## Testes executados

Suíte `tests/run_all.sh` com ASan/UBSan e avisos como erro,30testes Python e Node de contrato de combustível: passou. Inclui transações/format/recovery/IO FAT/OPEN/remontagem/manutenção/backup, fila/fairness, perfil legado, J1939/TP, equivalência J1939 C/Python, J1587, diagnóstico Volvo, UART, captura, agendador, RXguard, consumo e Link. Regressões locais de navegador do painel e manutenção: passaram contra servidor mock, sem dispositivo.

|Cenário|Resultado verificável|
|---|---|
|Boot602 exato,eco parcial1..4/5 e fim tardio|Libera TX aos20ms; quadro exato excluído uma vez; sem jguard/stop nativo; candidato MID/PID correlacionado|
|Eco divergente,timeout repetido e suspeita MID|Abortos elétricos e contadores preservados; polling nativo segue nas cadências individuais|
|117 sempre silencioso|≥26timeouts em300s virtuais;118 e250 continuam respondendo; sem bloqueio global|
|Resposta pré-TX/fora450ms|Rejeitada como resposta atual; candidato temporal continua classificado candidato|
|Configuração antiga e MID188|Migra só três cadências, mantém MID/targets/outros períodos, sem repetição da migração|
|Mudança salva de MID|Reconfigura entre transações e usa o novo MID, após RX recente|
|RX cego/UART/clock/OTA/parada manual|Proteções continuam; recuperação controlada por medidas saudáveis, sem força de TX|
|PID250 e média|Reconciliação sem soma dupla; contador igual, negativo/salto, origem, limites, CRC e rollback; média com extremos compatíveis|
|Viagem virtual300km|299,98172160km;59,81502658L canônicos;3reinícios simulados POWERON/TASK_WDT/OTA;4boots retomando;MID188 mantido|
|Períodos117/118/250 na viagem|1.188envios para cada um; respostas117 propositalmente omitidas a cada17ºenvio não param os demais|
|Viagem,modo vazio e fronteiras|Duas operações distintas e fase vazia, referências NVS preservadas; cobertura da média da última operação parcial25,07835307L, distinta do total canônico|
|Offline da viagem300km|7.200s sem rede;1.207registros únicos,1.208entregas com ACK perdido;0duplicatas no servidor mock; fila final0;2reinícios com gravação interrompida|
|Volume integral offline2h|442.830quadros processados;9.870consultas/candidatos;zero bruto persistente;14quedas simuladas de gravação e sincronização sem duplicação|
|Offline com relógioUTC|4.341registros,1.802.978B físicos de1.818.624B de orçamento;0dropped_full|
|Offline sem UTC/GPS|3.909registros,1.630.651B físicos;0dropped_full; nenhum(0,0)fictício|
|Memória|Falha individual de cada alocação, paginação, serialização, upload, diagnóstico sob41.596B e mínimo sem heap; sem vazamentos detectados|
|Build real|Core ESP32-C6/8MB, ESP-IDF5.5.2, slots/bootloader/partições compatíveis; artefato e hashes no manifesto|

Dois ensaios se complementam: a viagem usa aquisição/scheduler/decoder/combustível/NVS reais com UART/relógio mock; suas linhas operacionais atravessam depois o writer/CRC/recovery/batch/ACK reais com endpoint mock. O ensaio integral2h usa os serializers reais de todos os sensores e eventos, com volume mais alto. Nenhum deles substitui o ESP real, flash física sob vibração ou prova de entrega à Cloud de produção.

## Limites e pendências

1. Não foi feito ensaio elétrico, OTA real, reset físico nem consulta ativa à ECU. Não se declara validação externa dos27PIDs ou do transmissor.
2. A margem do ensaio integral2h com UTC foi15.646B, aproximadamente0,86%. Arquivos isolados, backlog anterior e eventos extras reduzem a autonomia. O cenário medido não garante2h em qualquer volume existente ou padrão de falhas. Evidências isoladas permanecem preservadas mesmo após ACK e consomem espaço; nenhum arquivo foi apagado pela candidata.
3. Falhas repetidas de eco/colisão são diagnósticas para o polling nativo, mas o TX atual sempre aborta. Um conflito físico verdadeiro continua exigindo correção do MID/instalação, que esta versão não pode determinar sozinha. O resultado não é rotulado sucesso físico.
4. Resposta J1587 não contém identificador exclusivo de transação; um broadcast do mesmo MID/PID dentro da janela permanece `RESPONSE_CANDIDATE`. Não aceitar causalidade física só pela proximidade temporal.
5. O consumo técnico e odômetro seguem EM VALIDAÇÃO física. Quantização do250 e idade do odômetro limitam a frequência útil da média.
6. Cloud financeira v26 ainda precisa da migração revisável já existente; não está incluído deploy. Sem alteração de layout Android/multimídia, viagens, histórico, filas/ACK ou armazenamento físico nesta tarefa.
7. Causa dos POWERON e do PANIC antigo não comprovada. Símbolos ELF/map e diagnósticos preservados permitem decodificar um próximo dump real.

Arquivo de evidência da fonte/Cloud: `test_evidence/v0225_cloud_readonly_audit.json`. Logs completos e comparação de memória acompanham o pacote. Diff: PR de revisão/`git diff a9cd6bdd..HEAD`; procedimento em `ATUALIZACAO_v0.2.25.md`.
