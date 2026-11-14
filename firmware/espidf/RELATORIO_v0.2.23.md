# TruckBox Core Rev.B.1 — candidata 0.2.23

Data: 14/11/2026. Dispositivo investigado: `tb-c6-d40592b9190c`, Volvo FH 540 2013. Nenhuma OTA, ativação de transmissão, reinicialização, alteração de configuração ou escrita na Cloud foi executada. A classificação continua **OPERATIONAL_INDIRECT / RESPONSE_CANDIDATE**, sem comprovação elétrica externa.

## Base exata e escopo

Base da branch: `6707efb6668e4cb3d817e6e93461c188fb235531` (`fix/revb1-v0222-simple-maintenance`); fonte 0.2.22: `f29526c10bf997d40e16ab94cea3dc83d499f8fc`. Desenvolvimento isolado em `fix/revb1-v0223-polling-fuel`. A Cloud confirmou, nos boots 562–566, o ELF SHA256 `c50ffe3d1fd59a26527d25641b65025b93064d4c2dc91d6d8ab3a5f33ded6f7b`, igual à aplicação do pacote 0.2.22. Não foi usado firmware substituto.

ESP-IDF **5.5.2**, ESP32-C6, GCC RISC-V 14.2, flash 8 MB, perfil existente `J1708_ONLY`. Partições, bootloader, Wi-Fi, credenciais, microSD/FRAM/4G desabilitados, gateway Android, OTA/rollback e armazenamento/ACK existentes foram preservados. A manutenção simples da 0.2.22 permanece, inclusive sua autorização de uso em movimento. J1939 e decodificadores passivos continuam no repositório; o problema de zero quadros CAN não foi alterado. Nenhum PID novo ou aumento de frequência foi introduzido.

## Causas comprovadas

### Falso conflito de MID

O replay usa estes bytes reais do evento de 04h04min47s, BRT:

```
TX:       AC 80 22 82 30
resposta: 82 22 00 5C
UART:     AC 80 22 82 30 82 22 00 5C
```

Os dois quadros, e também sua concatenação, têm checksum módulo 256 igual a zero. Na 0.2.22, `active_note_rx` reconhecia o eco completo, mas `active_is_tx_echo` só o aceitava até 30.000 us depois do início do TX. Com o atraso observado de 38.362 us, o prefixo não era separado. `j1708_task` entregava a concatenação a `finish_j1708`, que observava o MID local 172 e parava o polling.

Reprodução anterior à correção, usando as funções reais: `observed_local=1`, `safety_stopped=1`, `armed=0`, `responses=0`. Após a correção: `observed_local=0`, `safety_stopped=0`, `armed=1`, `responses=1`. Há 30 combinações automatizadas de atraso/fragmentação, além dos testes existentes da PID439.

Arquivos: `tbx_j1708_active.c`, funções `active_note_rx`, `active_is_tx_echo` e `active_consume_tx_echo`; `capture.c`, funções `j1708_task` e `finish_j1708`. A correção consome **uma única** cópia do eco que já foi comparado byte a byte. Não exclui quadros arbitrários apenas pelo MID ou por uma janela temporal. Uma segunda mensagem independente, até com bytes idênticos, continua provocando proteção. O prazo elétrico/lógico de eco de 20 ms não foi aumentado; foi corrigido o reconhecimento do quadro já recebido e posteriormente processado.

Isso comprova um caminho de falso positivo e reproduz exatamente o evento informado. Não comprova que nenhum outro equipamento jamais tenha usado o MID172: essa exclusão física continua inconclusiva sem observação independente. A evidência não foi convertida em validação elétrica externa.

O `LINK_ERROR expected=AC received=88` ocorreu às **04h04min23,837s, PID161**. O conflito ocorreu às **04h04min47,164s, PID34**, 23,327 segundos depois. São transmissões distintas. A causa elétrica/arbitragem da divergência anterior não foi determinada nem desprotegida.

### Configuração desativada por uma proteção

Em `capture.c:request_task`, a 0.2.22 transformava `env.local_conflict` em `operator_hold/disable_pending` e chamava `tb_config_set_j1708_auto(false)`. O falso conflito, portanto, alterava a política desejada na chave `v216`. A Cloud confirma `j1708_automatic=false` ainda no boot564 e nos boots565/566. A falha não era qualquer reinicialização: os boots563/564 retomaram normalmente antes desse bloqueio.

A candidata remove essa escrita automática de OFF. O desejo continua em `v216`; a marca de migração única fica em `v223`; um conflito independente fica em `tb_trip/jguard`, com CRC, bytes, MID, boot e uptime. Carregamento inválido bloqueia TX e informa o problema, sem apagar NVS. Falha de persistência não é anunciada como persistência confirmada.

Instalações antigas não distinguiam OFF manual de OFF produzido por esse bug. Conforme a decisão explícita de polling nativo, a migração v223 reabilita uma vez o estado desejado, preservando MIDs/cadências. Depois, uma desativação explícita no firmware novo permanece desativada nos próximos boots. A migração não dispensa a escuta passiva inicial nem a proteção contra MID observado.

## Operação automática e supervisão

O polling independe de Android, internet e Cloud. Aguarda interface pronta, política salva e três janelas recentes saudáveis da proteção RX existente. Estados: aguardando rede, executando, backoff, bloqueado, desabilitado ou indisponível. Não renova uma autorização manual de ensaio para operar em produção.

As três falhas consecutivas, erros reais de eco, colisões, UART e proteção RX continuam ativos. A recuperação transitória mantém espera progressiva de 60/120/240/480 s e requer novas condições saudáveis; não transmite para tentar provar que a rede está saudável. Um MID independente observado continua bloqueado entre boots e não é liberado por Wi-Fi, pelo botão de habilitar política ou por um timeout. Alteração legítima de perfil/MID continua sujeita à observação de rede.

O supervisor compara contadores de envio e timestamps reais. Seu prazo deriva de três janelas RX e de uma rotação completa do scheduler, incluindo timeout e espaçamento. Tempo com barramento ocupado não conta como oportunidade perdida de TX. Há motivo distinto `BUS_BUSY_DEFERRED`. Se o scheduler não progride apesar de oportunidades saudáveis, entra em backoff e reinicializa controladamente o controlador existente. A tarefa de snapshots também supervisiona o heartbeat da tarefa TX e suspende o controlador sob o mesmo mutex; não cria outra tarefa concorrente nem reinicia o ESP32. Se a tarefa voltar, respeita o backoff; uma tarefa ou mutex realmente travado continua sendo uma indisponibilidade diagnosticada, não motivo para forçar TX.

`AUTOMATIC_STATUS` é emitido mesmo quando o desejo está desabilitado. Registra desejo, estado, motivo, última parada, última submissão completa à UART, resposta candidata e heartbeat. `POLLING_PID_STATS` mantém as sete colunas existentes e acrescenta timestamps de envio/resposta por PID. Contadores acumulados não são evidência de uma nova transmissão naquele instante. Submissão UART/eco não são denominados comprovação elétrica externa.

Cadências de produção padrão herdadas da 0.2.22; os valores já salvos na NVS prevalecem sem alteração:

| PID | MID destino | Normal | Arrancada |
|---|---|---|---|
|26|128|60 s|60 s|
|31|130|10 s|2 s|
|32|130|30 s|10 s|
|33|130|5 s|2 s|
|34|130|15 s|5 s|
|37|130|30 s|15 s|
|62|144|60 s|60 s|
|63|130|15 s|5 s|
|68|128|60 s|60 s|
|108|128|60 s|60 s|
|117|140|60 s|60 s|
|118|140|60 s|60 s|
|158|128|60 s|60 s|
|160|130|30 s|10 s|
|235|128|900 s|900 s|
|236|128|900 s|900 s|
|250|128|300 s|300 s|

As candidatas já presentes também permanecem: 93/128/60s, 94/128/30s, 107/128/300s, 153/128/30s, 154/128/300s, 161/130/10s, 182/128/300s, 188/128/900s, 244/128/300s e 439/128/300s. Uma única fila, um pendente, até2 requisições/s, timeout configurado existente inicialmente450ms, prioridade/arbitragem e guarda de barramento livre preservados. Estes são parâmetros do código/NVS, não novas comprovações físicas dos significados proprietários.

## Combustível, persistência e viagens

`tbx_fuel_account.c` mantém **um único acumulado canônico**. PID183 utiliza integração trapezoidal de amostras recentes, com corte pelo último valor recente na fronteira da âncora. Interpolação limitada é estimativa identificada; intervalos sem suporte permanecem desconhecidos. Não se assume vazão zero na ausência de dados.

PID250 usa MID configurado, payload de quatro bytes, checksum/layout verificados pelo receptor e escala SAE existente de **0,473176473 L/tick**. A comparação com o valor do decodificador confere unidade/escala. Isso valida a origem de protocolo e a consistência temporal, sem mudar a marca de validação física. O flag antigo `j1587_fuel250_validated=false` não impede mais a referência técnica solicitada; permanece falso como indicador de validação física pendente.

Para cada intervalo válido: `correção = delta_ECU - provisório_do_intervalo`, e `canônico = canônico_da_âncora_anterior + delta_ECU`. A estimativa não é somada de novo. Taxas/estimativas históricas são diagnósticos, não acumuladores independentes para somar. `pending_l` representa o saldo após a âncora; `direct_l`, os deltas ECU confirmados; `unanchored_l`, quando presente, preserva valores anteriores à primeira âncora sem promovê-los a confirmados. Lacunas históricas permanecem identificadas.

Os testes exigidos resultaram em **9→10 L (+1), 5→4 L (−1), total14 L confirmado** e **12→13 L (+1), total13 L, não25**. Esses exemplos exatos são testados no domínio SI. Há testes separados de bytes/escala: valores inteiros arbitrários de litros não são apresentados como quadros SAE reais, pois a quantização é 0,473176473 L.

Duplicatas, regressão temporal, MID diferente, redução do contador e saltos incompatíveis são recusados sem trocar a âncora válida. Contador inalterado com timestamp posterior é válido. Mantém-se o envelope conservador já existente de 1.000 L/h e tolerância de duas unidades de quantização, sem nova calibração de motor. Não há rebase automático de rollover/troca de ECU.

Checkpoint v4 A/B com CRC preserva âncoras, boot/uptime/UTC, odômetro quando validado, provisório, correções e referências de operação. Escreve no período já existente de 10 s; uma nova âncora notifica a tarefa de checkpoint, fora do RX. Fronteiras de operação são persistidas antes de oferecer seu registro. Mantém `trip3a/b` e a chave legada `trip` para rollback. Se uma versão antiga avançar seu checkpoint, a candidata importa o saldo legado, sem ressuscitar âncoras v4 incompatíveis.

Após reboot, o próximo delta250 pode cobrir o intervalo sem amostras183 quando ambas as âncoras têm tempo confiável. Sem UTC suficiente entre boots, conserva a âncora e o saldo, sinaliza `TIME_REFERENCE_REQUIRED` e continua integrando localmente; não usa um limite arbitrário de milhares de litros para fingir plausibilidade. Um checkpoint v3 não tem esses timestamps: seu saldo é importado e recebe uma nova âncora, sem reconstrução fictícia do intervalo anterior.

Início/fim de viagem não zeram contadores. `FUEL_BOUNDARY` guarda referências imutáveis, inclusive último250, provisório, timestamp e odômetro disponível. `FUEL_RECONCILIATION` registra ajustes com identidade de intervalo; repetições são idempotentes para consumidores. Se uma âncora atravessa uma fronteira, a distribuição pelo provisório/tempo é explicitamente estimada. Combustível no modo vazio após END não continua entrando na operação encerrada. START atrasado não reabre a operação já encerrada; referência entregue com atraso/idade desconhecida não é apresentada como exata. O detalhamento do contrato e das consultas por trecho está em [cloud/README_FUEL_V2.md](cloud/README_FUEL_V2.md).

## Cloud: dependência concreta e proposta não aplicada

A consulta somente leitura encontrou `truckbox-ingest` v26, os triggers de calibração e os reconstrutores de consumo. Hoje essas rotinas podem reintegrar a vazão e sobrescrever o total reconciliado do Core. **Somente atualizar o Core não corrige esses agregados da Cloud.**

Entregues: normalizador puro testado, preparador que exige a fonte v26 exata, diff de ingest, SQL de revisão e rollback SQL. Nenhum deles foi aplicado. A proposta evita segunda calibração/reintegração nas linhas v2 e exige UID de operação correspondente para normalizar combustível por viagem. Os agregados financeiros mistos ficam preservados, aguardando uma migração revisada; não foram recalculados. O SQL precisa de execução em uma base isolada antes da aprovação. Não se declara a integração de produção concluída sem essa etapa.

## Reinicializações

Interpretação pelo `esp_system.h` do IDF5.5.2:

| Boot | Horário BRT | reset_reason | Evidência |
|---|---|---|---|
|562|02:39:30|3, SW|Primeiro boot observado após software/atualização; não é um dos quatro seguintes|
|563|02:43:22|1, POWERON|Origem elétrica/EN/pós-chave não determinada pelo código|
|564|03:37:13|4, PANIC|Exceção comprovada; causa específica ainda sem dump|
|565|04:07:54|1, POWERON|Posterior ao bloqueio; proximidade não prova causalidade|
|566|06:07:06|1, POWERON|Mesma limitação de evidência|

Há **um PANIC e três POWERON** nos quatro reinícios. A última janela antes do PANIC tinha mínimo de heap 7.824 B, mas esse número sozinho não prova falta de memória ou a função que falhou. Não há backtrace/dump do boot564 nesta entrega. Acesso de leitura já existente: `/api/crash` e `/api/crash.bin`, com autenticação administrativa. Não foi lida a placa pela rede nesta tarefa. A candidata acrescenta ao BOOT_DIAGNOSTIC informação limitada do dump retido em caso de PANIC, marcada como associação de boot desconhecida. Não apaga o dump nem o envia integralmente à Cloud.

## Validação executada e limites

Logs integrais em `test_evidence/v0223_*.txt`; memória/preservação em JSON no mesmo diretório. Build real do Core/ESP32-C6 e suíte completa ASan/UBSan, testes Python, navegador e teste offline passaram. A matriz abaixo indica o tipo de evidência, não validação elétrica:

| Aceitação | Evidência executada |
|---|---|
|A/B — boot e reinício|Controlador real com UART mock, escuta saudável, submissão TX e resposta candidata; sem aplicativo/internet|
|C — três falhas|Timeouts reais do scheduler, parada, backoff e retomada após janelas novas|
|D — falso conflito|Bytes do boot564, 30 atrasos/fragmentações, PID34 e PID439|
|E — conflito verdadeiro|Quadro independente MID172, inclusive cópia idêntica após consumir o eco; bloqueio preservado na NVS e passiva continua|
|F — reboot após parada|Migração desejada, guard com CRC, checkpoint A/B e referência de boot nova; não grava OFF por falha técnica|
|G — PID250|Enquadramento/escala e nova submissão/resposta candidata simuladas após boot; sem prova física nova|
|H — reconciliação|Exemplos14 L/13 L, correções negativas, contador inalterado e rejeições|
|I — reinício no intervalo|Checkpoint, âncora preservada, sem duplicação, relógio ausente explicitado|
|J — offline/ACK|Duas variantes de 7.200 s, com/sem UTC, 441.135 quadros cada, 8.175 requisições/candidatas e 14 reboots da recuperação de armazenamento|
|K — operação/vazio|START/END, fronteira dentro de intervalo, alocação estimada, END congelado, duplicatas e entrega atrasada|
|L — rede|Limite global existente, um pendente, arbitragem, colisões, erro de eco, timeout, guard RX, barramento ocupado e parada manual|

No pior dos dois ensaios offline: **1.671.151 B físicos de 1.818.624 B de orçamento**, sem overflow de capacidade. Foram preservados 3.909 registros, com reentregas e zero duplicatas no mock da Cloud; fila válida zerada após reconexão. Arquivos isolados de evidência continuaram no disco depois do ACK e reduzem espaço futuro até revisão explícita. Isso não equivale a afirmar duas horas disponíveis em toda flash que já contenha outros dados. Não houve replay completo dos ZIPs dos Logs Mãe, que continuam indisponíveis.

Ainda pendentes: observação do binário candidato na placa, causa exata do PANIC, exclusão elétrica de outros transmissores MID172, calibração física das fontes marcadas EM_VALIDACAO e aprovação/teste isolado da integração Cloud. Não se requer uma nova campanha NEXIQ para simplesmente compilar esta correção.

## Memória e preservação

RAM estática do ELF: **210.436→211.506 B**, aumento de **1.070 B** incluindo IRAM e estado novo. Não há economia fictícia: os checkpoints e o supervisor têm custo. Nenhuma tarefa ou fila nova foi criada; capacidades das filas permanecem iguais. Estado de combustível host584B e checkpoint v4 host736B; a persistência exclui explicitamente ponteiros e o build RV32 verifica o layout escalar.

Diagnóstico compacto passou por todas as falhas individuais de alocação testadas e 200 repetições, sem vazamento. No cenário host de 41.596 B de orçamento, o pico compacto, incluindo upload concorrente e checkpoint existente, foi **25.077 B**. O resumo conserva indicadores de memória e a paginação dos sensores. A serialização dos registros usa buffer limitado ao formato existente. Campos diagnósticos compactos podem usar precisão de mililitro; o acumulado canônico e os contadores brutos não são arredondados no cálculo.

Frames de stack compilados: persistência992B, captura JSON5152B, snapshot de intervalo848B e fronteira976B. São frames do compilador, não medição de watermark em execução. **Heap, fragmentação e stack reais do candidato no caminhão não foram medidos.** Evidências de preservação conferem118 arquivos de runtime/Link/aplicativos/partições/armazenamento e a release0.1.0 sem mudança de bytes.
