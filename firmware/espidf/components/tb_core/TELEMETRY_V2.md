# Registros de aquisição v0.2.0

O TCP `TB1` e os endpoints legados permanecem separados desta fila. As funções
de RX só copiam o frame para uma fila de 128 posições. Um worker de prioridade 3
decodifica e oferece registros ao armazenamento existente, de prioridade 4.
Nem RX nem decoder esperam GPS, Android ou internet.

Cada captura recebe `boot_id`, `capture_id` crescente dentro do boot e
`uptime_us` monotônico. O armazenamento acrescenta `seq` persistente ao confirmar
o registro. `store_queue_accepted` significa entrada na fila, **não** confirmação
no microSD; a confirmação persistente e erros pertencem a `storage`.

`schema_version=2`, `decoder_version=0.2.0`. `sample_kind=RAW` identifica a parte 0
com frame original. O campo `signals` contém as leituras que cabem no mesmo
registro. Havendo mais leituras, registros `DECODED` conservam a mesma identidade
de captura e aumentam `part`. Cada JSON tem menos de 768 bytes. Um consumidor pode
agrupar por `(boot_id,capture_id)`; `seq` continua sendo a identidade de ACK.

`time_valid=false` e `time_source=MONOTONIC_BOOT` significam ausência de horário
absoluto. Quando o relógio do sistema já está válido **na entrada do RX**, grava-se
`unix_ms` e `time_source=SYSTEM_CLOCK`. O worker não reaplica um relógio ajustado
depois da captura aos frames antigos.

GPS real fica na parte 0. Sem fix: `gps=null`, `gps_status=NO_FIX`, `has_fix=false`;
latitude e longitude ficam ausentes. Continuações usam `gps_ref_part=0` para
referir a posição daquela captura, sem consultar GPS novamente. Um fix recebido
depois do timestamp da captura nunca é retroatribuído a esse frame.

Em CAN: ID real de 29 bits, PGN reconstruída e SA; não há tradução dos aliases do
logger. J1939 usa `sid` do inventário, `pgn`, `sa`, `unit`, `value`, `quality`,
`validation` e versão do decoder. `value=null` preserva indisponibilidade,
comprimento/estado inválido ou falta de fórmula. Derivações vêm identificadas.

Grupos documentados no Documento Mestre, como curvas EC1/RCP5 e calendário,
emitem a leitura principal e entradas adicionais com o mesmo `sid`,
`component_index` e `component` nomeado. Cada componente guarda unidade, valor e
qualidade próprios e pode ocupar outro `part`, respeitando o limite de 768 bytes.
Um componente experimental permanece `EM_VALIDACAO` mesmo quando o agrupamento
de origem tem histórico validado nos logs. Não é necessário reunir a curva toda
em um único JSON nem suprimir seus pontos para caber no registro.

O worker também recebe TP.BAM passivo: quatro sessões, até 64 bytes cada, apenas
para PGNs 65251/65249. Todos os frames físicos TP.CM/TP.DT continuam gravados,
inclusive transferências rejeitadas/incompletas. Uma transferência completa emite
registros separados com `source=j1939_tp`, PGN/SA da mensagem reconstruída e
`tp_start_capture_id`/`tp_end_capture_id` exatos. A primeira parte reconstruída,
`part=1`, preserva `raw_payload` completo, comprimento, quantidade de pacotes e
timestamp inicial. Continuações usam `payload_ref_part=1`. Sua referência GPS
continua apontando para a parte 0 do último frame físico, sem criar localização
retroativa para os anteriores. Nunca se atribui DLC maior que 8 a um frame CAN
clássico nem se inventa um ID físico para a mensagem reconstruída.

Sessões incompletas expiram após 1250 ms sem pacote. O worker verifica expiração
também quando a fila está vazia. Diagnósticos `j1939_tp_bam` distinguem conclusão,
timeout, sequência/ordem inválida, pacote órfão, comprimento, controle/destino/PGN
não aceitos, reinício e limite de sessões. O módulo não transmite CTS nem ACK.

J1587 usa `pid`, `mid`, `raw`, `unit`, `value`, `quality` e
`validation=EM_VALIDACAO`. A origem é sempre recepção passiva, inclusive para um
PID que também pertença ao catálogo de 17 pedidos. Nenhuma coincidência temporal
é anunciada como resposta causal. Além do valor escalar:

- `states`: pares `[estado,largura_em_bits]`, na ordem de campos definida no
  switch correspondente em `tbx_j1587_passive_full.c`; os nomes constantes desse
  decoder são a tabela de interpretação. Estados de 2 bits mantêm 0/1/2/3 separados.
- `state_pairs_lsb_first`: quatro pares de 2 bits do decoder dos 17 PIDs, da menor
  para a maior significância. PID 62 só interpreta o primeiro par; os demais são
  reservados, conforme o módulo existente.
- `dtcs`: entradas `[identificador,FMI,eh_SID,pagina2,ativo,tem_ocorrencias,
  ocorrencias]`. Os campos booleanos são 0/1. Não se infere causa do fabricante.
- `clock_hms`, `date_ymd`, `text` e, para controle, `requested_pid`/`target_mid`.

Checksum, formato inválido, MID não J1587 e frames truncados continuam gravados
como bruto, com diagnóstico explícito. `raw_truncated_or_oversize=true` nunca
representa um frame completo validado.

`GET /api/sensors?offset=0&limit=16` expõe leituras observadas, paginadas até 32 por
resposta, com unidade, validação, idade, `stale` após 5 s e bytes de origem. A tabela
tem 192 posições e distingue `(J1939,sid,componente,SA)` de `(J1587,PID,MID)`. Esgotada a
capacidade, substitui a leitura mais antiga e incrementa `latest_evictions`;
isso não remove registros persistentes. Catálogo e fórmulas faltantes são
consultados separadamente, sem inventar leituras de sensores nunca recebidos.

O cache mantém no máximo 32 bytes brutos por entrada. `raw_length` informa o
comprimento real, e `raw_truncated_in_latest=true` identifica uma mensagem maior;
o `raw_payload` persistente conserva seus 64 bytes quando presentes. Grupos e
componentes são entradas distintas na paginação e partilham a mesma captura.

Uma nova observação inválida/indisponível continua aparecendo com sua qualidade
e `value=null`, preservando separadamente `last_valid_value`,
`last_valid_age_ms`, `last_valid_stale`, `last_valid_quality` e
`last_valid_validation` quando houver uma leitura numérica anterior utilizável.
Campos provisórios mantêm a validação provisória também nesse histórico; nenhum
valor suspeito de perfil é promovido a leitura válida. Esse cache é de RAM;
o histórico persistente e seu replay pertencem ao armazenamento.

Os limites da fila e a duração de `fsync` no cartão real impedem prometer captura
sem perda a qualquer taxa. `rx_queue_dropped`, `store_rejected`,
`serialization_errors` e contadores do armazenamento tornam perdas e rejeições
visíveis. `capture_id` também avança em overflow, deixando lacunas identificáveis.
O teste de capacidade e duração no microSD físico continua obrigatório.

Teste portátil do serializer real, com ASan/UBSan:

```sh
IDF_PATH=/caminho/esp-idf-v5.5.2 sh components/tb_core/tests/run_telemetry.sh
```

Verifica ausência de GPS/horário absoluto, fix posterior sem retroatribuição,
GPS com todos os campos opcionais, 8 estados, 6 DTCs num frame máximo, 7 linhas da
PGN 65263 divididas sem perdas, checksum inválido preservado, rejeição pelo store,
fila limitada e cache de leituras. RTOS, GPS e escrita física são mocks dessas
fronteiras; os decoders e a serialização/divisão exercitados são os de produção.

Também verifica BAM EC1 de 30 bytes, RCP5 especial de 19 bytes e o limite de
64 bytes, preservando todos os pontos das curvas e a correlação dos frames
físicos. Esses payloads foram **construídos a partir dos offsets e exemplos do
Documento Mestre fornecido**; não representam replay dos logs-mãe completos,
que não foram disponibilizados. O teste inclui valor indisponível após RPM
válida, verificando que o histórico anterior permanece separado.


## Snapshot J1587 enviado no HEALTH (candidato 0.2.5)

No perfil LEGACY ou COMPLETO, o CORE continua emitindo OPERATIONAL a cada
15 s (ou 1 s no COMPLETO) e HEALTH a cada 60 s. O HEALTH agora pode incluir
`metrics.j1587_sensors` quando o cache contém observações numéricas J1587.
A cloud grava a estrutura completa em `health_minute_samples.metrics`; não há
envio de RAW/DECODED adicional, alteração de ACK ou solicitações ativas no barramento.

Formato compacto: chaves são **PIDs J1587 em decimal**, valores são
`[valor, idade_segundos, MID]`. Por exemplo, `"110":[46.1,2,128]`
representa temperatura do arrefecimento de 46,1 °C observada há 2 s.
`context.j1587_sensor_quality="EM_VALIDACAO"` aplica-se a **todos** os
valores deste bloco: são decodificações provisórias, não medições validadas
para alarmes ou manutenção. As unidades vêm do catálogo, por PID.

Prioridade de inclusão: 110 água (°C), 175 óleo do motor (°C),
100 pressão do óleo (bar; **escala a validar**), 102 turbo (bar relativo;
**escala a validar**), 177 óleo da transmissão (°C), 105 admissão (°C),
106 pressão do ar de admissão (kPa absoluto), 92 carga do motor (%),
168 bateria (V), 98 nível do óleo (%), 96 nível do combustível (%),
171 temperatura ambiente (°C), 174 temperatura do combustível (°C).
O campo é **omitido** quando o PID não foi observado, não possui leitura
numérica/qualidade admissível, ou sua última observação tem mais de 300 s.
A idade nunca é escondida: a interface deve sinalizar como DESATUALIZADO
qualquer sensor com idade acima da tolerância de seu sistema (5 s para
leituras operacionais rápidas; valores lentos podem aceitar intervalo maior).
Não reutilizar valores `last_valid` de observações indisponíveis.

O serializador aceita cada leitura somente se o **HEALTH completo** couber
no limite existente `TB_RECORD_MAX=768` bytes. A ordem privilegia sensores
de motor e caixa, descartando entradas de menor prioridade se faltar espaço,
sem descartar o HEALTH básico. Pode haver leituras ausentes por volume,
não apenas por indisponibilidade física.

**Ainda não há identificação confirmada da pressão hidráulica da I-Shift
na J1708.** Não rotular PID 37 (catálogo inicial de testes) nem PID 46
(pressão do reservatório de ar) como pressão de transmissão.
Dados J1939 permanecem independentes; a falta de CAN não impede os
snapshots J1587.

Critérios antes de instalar no caminhão: compilar ESP-IDF 5.5.2,
passar testes host de serialização/stale/no-data, validar que o ingest
preserva `metrics.j1587_sensors`, depois testar OTA e rollback **com
o caminhão parado**. Nenhuma instalação ou alteração em produção é
executada apenas por este commit.
