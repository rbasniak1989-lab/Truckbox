# TruckBox Rev.B.1 Lite para o Volvo FH 540 2013

A versão `0.2.27-lite` usa dois papéis compilados para ESP32-C6. O **Core Lite** mantém a recepção J1708/J1587, o decodificador, a proteção de TX, a multimídia local e a manutenção. O **Link Telemetry** coleta a leitura atual, grava na sua flash e confirma a entrega à Cloud. A publicação do Link exige ativação explícita. Os dois firmwares usam o mapa OTA existente de 8 MiB com dois slots de 3 MiB.

## Caminho dos dados

```text
Volvo J1708 → Core RX/TX, cache e /api/live → Multimídia local
                       │
                GET snapshot autenticado
                       ↓
Link ESP32-C6 → diário e sequência reservada → HTTPS truckbox-ingest v26
                       ↑                                  │
                       └──────── ACK correspondente ─────┘
```

- Core Lite não monta a partição antiga de telemetria e não inicia a fila de 64 posições, o escritor, `tb_bridge`, o TCP RAW 35000, o exportador de snapshots de flash ou o checkpoint periódico de viagem. `/api/live` continua local. O perfil de configuração, o coredump e a recuperação OTA permanecem disponíveis. A consulta do Link usa buffer fixo e o cache em RAM. O boot ID do Lite correlaciona apenas a sessão, sem escrever um contador de boot na NVS. O Core precisa conseguir ler uma configuração válida para iniciar.
- `GET /api/telemetry/snapshot` exige a chave de vínculo em `Authorization: Bearer`. A resposta V1 inclui `device_uid` físico, `boot_id`, `local_seq`, `uptime_ms`, `unix_ms` (`0` sem relógio UTC), `age_ms` da leitura mais recente e métricas de saúde. `valid`, `stale` e `estimated` são máscaras na ordem de `tb_lite_wire.h`. `values` só contém chaves válidas. Uma leitura ausente fica fora de ambas as máscaras. O torque percentual usa a referência histórica do Volvo e é marcado como estimado. Contadores de odômetro e diesel exigem MID e escala comprovados antes de uso em cálculos oficiais. [Contrato em C](components/tb_base/include/tb_lite_wire.h)
- O Link usa o host do Core na configuração ou o AP privado já suportado, com uma tentativa por ciclo de 10 s. Saúde sai em ciclos de 60 s e há uma linha adicional ao detectar perda/retorno do Core, mudança de boot ou conflito de MID. Eventos de conectividade seguem como HEALTH para aproveitar a deduplicação. Tarefas separadas limitam a interferência de TLS e da indisponibilidade da Internet na coleta, no GPS e no modem. Se o Core está sem UTC, a amostra recebe horário do relógio sincronizado do Link menos a idade. Sem horário confiável a coleta aguarda. Os reenvios mantêm a hora original.
- O diário de telemetria tem formato `TBLT1`, CRC, `fsync` por registro e confirmação local durável. Usa `/gps/telemetry.log` e temporários próprios, com a mesma montagem FAT/WL que o GPS quando disponível. `/gps/outbox.log` continua reservado ao GPS. Só um volume todo apagado de fábrica é candidato à inicialização automática. Na recuperação, um final parcial pode ser truncado e um arquivo de backup da compactação pode ser restaurado. Um registro com CRC inválido completo deixa o diário em falha para diagnóstico. Se faltar espaço, os pendentes são preservados e a entrada nova é contada como descarte. Sem arquivo conhecido ou com falha de NVS, a leitura e os diagnósticos do Link continuam disponíveis.
- A partição `storage` do mapa é `0x1e0000` bytes (1.966.080 bytes). O limite da telemetria é **256 KiB**. O relatório do Link mostra capacidade e espaço livres medidos na montagem, além de pendências e descartes. A ordem de grandeza conservadora é cerca de meia hora com registros máximos, seis amostras e uma saúde por minuto e espaço livre suficiente. A autonomia depende do tamanho real dos registros, dos eventos, dos dados de GPS e do espaço necessário à compactação. Meça na unidade antes de prometer uma retenção.
- Um lote contém um registro. O Link só avança o diário após HTTPS 2xx, `ok=true`, `ack_seq` igual ao único `seq` oferecido, contagem `accepted=1` ou `health=1` conforme o contrato e confirmação gravada localmente. Um corte entre ACK remoto e checkpoint reenvia o mesmo identificador. As reservas de 128 números são persistidas antes do uso em `tb_link_lite`. O reinício pode pular números ainda livres da reserva, evitando reutilizá-los. A identidade física do Core, o cadastro Cloud e o endereço HTTPS ficam vinculados ao diário. Uma divergência bloqueia a publicação até revisão.

## Evidência da Cloud e sequência

A inspeção foi somente leitura, em 21/11/2026. A Edge Function `truckbox-ingest` ativa estava na versão 26. Ela resolve `device_uid` na tabela `devices`, confere o token individual, usa `unix_ms` quando `time_valid=true` e faz upsert idempotente em `telemetry_samples` e `health_minute_samples` por `(device_id,seq)`. `telemetry_samples.seq` é `bigint`, limitado a inteiros seguros pela função. O ACK é o maior número oferecido após a escrita. A rota `events` usa INSERT, por isso os eventos de transporte Lite usam a tabela de saúde.

Para `tb-c6-d40592b9190c` o maior `seq` operacional foi **557255**, saúde **557243**, `telemetry_latest.seq` **557254** e o último timestamp operacional **2026-11-17 13:16:21.847 UTC**. Havia uma viagem ativa. O maior número local informado era **557568**. **1.000.000** é uma faixa inicial proposta para esse corte e deve ser revalidada contra os máximos de ambos os lados antes de ativar. A publicação usa `sequence_floor`, cadastro e token informados na configuração. O Link usa o mesmo cadastro e token individual já existentes para a operação oficial, com o UID de hardware esperado do Core em campo separado. [Edge Functions](https://supabase.com/docs/guides/functions)

```sql
select d.device_uid,
       (select max(seq) from telemetry_samples where device_id = d.id) as operacional,
       (select max(seq) from health_minute_samples where device_id = d.id) as saude,
       (select max(seq) from events where device_id = d.id) as eventos,
       (select max(seq) from telemetry_latest where truck_id = d.truck_id) as painel
from devices d where d.device_uid = 'tb-c6-d40592b9190c';
```

## Instalação e ponto de corte

1. Estacione o caminhão, use alimentação estável e confirme as duas imagens existentes, as partições e o acesso local de administração de Core e Link. Guarde as imagens anteriores, configuração, calibrações, identificação e credenciais em local privado. Compare SHA-256 e papel do arquivo antes de abrir a página de OTA. Use o acesso local de serviço de cada módulo, com antenas e alimentação conhecidas. Na Rev.B.1 J3 é o USB de serviço do Link. Para primeiro acesso USB ao Core, use a ficha FIX-013 do pacote de hardware. O conjunto desta versão fornece aplicativos OTA para o mapa existente.
2. Grave o **Link Telemetry** com o coletor em observação (`lite_upload_enabled=false`). Guarde `wifi_ssid`, chave de vínculo e token já cadastrados. Configure **host do Core** (`192.168.1.251` na instalação observada, conferir DHCP), `telemetry_core_uid`, HTTPS de ingestão e UID Cloud. Separe o nome/SSID do AP privado do SSID Starlink. O Link continua aceitando o relay anterior nesta etapa. A versão 0.2.24 normalmente não oferece o novo endpoint, então 404 é esperado na observação com o Core antigo.
3. Valide os dois builds em bancada isolada, com tráfego J1708 de teste. Use um Core Lite de teste e um **Link de teste ou ambiente segregado** vinculado a cadastro e endpoint de homologação, ou um receptor HTTPS de teste que reproduza o contrato de ACK. O diário associa URL e UID de forma durável, então mantenha a fila de homologação em unidade segregada. Confirme que o receptor realmente gravou o lote e restituiu o ACK correto, incluindo Internet interrompida e reinício do Link. Use a `telemetry_collector` no `/api/state` do Link para conferir `received`, `durable`, `pending`, `last_durable_ack`, `invalid_acks` e `discarded`. A opção de observação pode provar o recebimento do snapshot sem filas ou POSTs oficiais.
4. Após o aceite de homologação, aplique o **Core Lite** na unidade do caminhão. O upload legado deixa de existir nesse build e os arquivos antigos ficam disponíveis para recuperação. Confirme /api/live na multimídia, aquisição J1708 passiva, resposta de `/api/telemetry/snapshot` no Link, EN e estabilidade. Se o Link reconhecer o UID errado, aponte o host correto antes de prosseguir.
5. Confirme a faixa de sequências e a data UTC. Registre o odômetro, o estado da viagem e, se disponível, o PID 250 do Volvo no momento do corte. Se o proprietário optar por começar uma nova viagem, encerre a atual e abra a nova pelo fluxo já autorizado no sistema, usando o odômetro real. Aplique a faixa de sequência no Link e ative **Ativar envio do coletor do Link**. O Link passa a rejeitar os lotes do bridge antigo. Valide linhas novas de `device_id` e `seq` na Cloud, a hora de captura, a atualização na aba de viagem e o retorno após perder o Wi‑Fi. Sem aceite, desative o coletor e use o rollback.
6. Atestação de TX é uma operação separada. Confirme o par e DE, a validação elétrica, a recepção, a colisão verdadeira e o MID de origem com bancada isolada. No Core Lite marque **Transmissão J1708 validada em bancada** após essa prova e preserve alvo, cadência, configuração e proteções. A política existente mantém PID 117, 118 e 250 com alvo de 10 s, espaçamento e guarda do barramento. PID 194 e demais PIDs seguem as políticas e a expansão já disponíveis. Registre a política efetiva. Se houver `LOCAL_MID_CONFLICT` histórico, use o botão de recuperação com caminhão parado, RX passiva limpa e MID ausente no boot atual. O procedimento arquiva o bloqueio técnico e mantém TX desligado até habilitação explícita. Um MID concorrente observado agora mantém TX bloqueado. [Correção de eco e recuperação](https://github.com/rbasniak1989-lab/Truckbox/tree/fix/revb1-v0224-mid172-echo-recovery)

## Viagem, combustível e dados antigos

O Link usa hora de início e primeiras leituras válidas no novo período. A distância usa diferença do odômetro absoluto validado. O combustível confirmado usa diferença do acumulador PID 250 validado, sem somar à integração já calculada pela Cloud. Se os contadores faltarem, regredirem, ou a base não apoiar o delta, os respectivos deltas ficam ausentes. Sem PID 250 válido a estimativa usa a vazão PID 183 quando disponível e recebe `fuel_quality=ESTIMATE_FROM_RATE_PID183`. A preferência por PID 250 no APK exige que a referência corresponda à mesma viagem nova (hora inicial e odômetro coerentes). A Edge Function em produção ainda integra a taxa na tabela de viagens. A preferência por PID 250 acima é para a leitura da viagem na Multimídia e o registro bruto, e não recalcula totais históricos no servidor.

Para uma nova referência local posterior, com o caminhão parado e o Link publicando, envie `POST /api/telemetry/reference` no **Link**, autenticado com a senha de administração e corpo `{"authorized":true,"stationary":true}`. O Link também exige velocidade zero recente do Core, hora válida e NVS gravável. O primeiro snapshot novo após a operação fixa a base. Guarde a referência antiga na ordem de serviço se for útil. A operação preserva fila, sequências, credenciais e histórico. A abertura ou encerramento oficial de viagem permanece no fluxo existente da Cloud.

A Multimídia `0.3.2-revb1-lite` usa `latest.ts`, a hora da amostra, porque `latest.updated_at` varia com reenvio. Leitura Cloud com mais de 30 s ou fora da janela de relógio perde a indicação de atual. A leitura local e os avisos de falha do Core continuam independentes da Internet. O layout da tela inicial permanece e a unidade da média sinaliza estimativa. A assinatura debug do novo build difere da cópia 0.3.1 publicada. Por isso o APK de oficina instala **TruckBox Multimídia Lite** lado a lado, com ID `br.com.truckbox.multimedia.revb1lite`. Mantenha o app existente e configure o endereço local do Core no Lite.

## Rollback e diagnóstico

- Desative a publicação Lite no Link e preserve o diário pendente. Faça backup de configuração/logs de maneira autorizada. Restaure o aplicativo do Link que estava instalado ou o slot anterior conhecido. O Link legado reconstruído incluído no pacote é uma alternativa, com a versão instalada a conferir. Restaure o **Core 0.2.24** pelo arquivo anterior do pacote ou pelo slot conhecido, através da gravação OTA manual. Mantenha os papéis e o mapa de partições. A configuração Lite usa chave adicional para os firmwares anteriores continuarem lendo a configuração antiga. Registre o resultado com todas as funções em bancada antes de mover o caminhão.
- Para voltar a publicar pelo Core legado, calcule antes o maior número aceito na Cloud e também a reserva do Link. Em storage recuperado e com autorização de corte, configure uma faixa no Core maior que todos esses números. Evite reabrir o emissor antigo com sequências abaixo da faixa usada. O Link volta a publicar somente depois de desligar o emissor concorrente e validar faixa, configuração e backlog. O rollback recupera a versão de aquisição anterior, que pode continuar com a mesma falha de armazenamento observada em campo.
- A instrumentação da reserva no perfil legado informa `nvs_set_u64` ou `nvs_commit`, `esp_err_t`, heap disponível e estatísticas de entradas NVS. Após três falhas consecutivas de reserva a tarefa aguarda retry autorizado, evitando o ciclo observado de 451 tentativas. Estatística ou ausência de espaço sem código de retorno do equipamento não identifica corrupção. A causa da falha da NVS na unidade de campo continua aberta. A topologia Lite remove esta dependência da telemetria do Core, mantendo as configurações que devem ser preservadas.

## Reprodução e aceite

Use ESP-IDF 5.5.2, dependências no lockfile e diretórios de build novos. Confira a versão, chip, papel e os dois perfis gravados no `sdkconfig` gerado.

```sh
cd firmware/espidf
idf.py -B build-core-lite -DTRUCKBOX_ROLE=core -DTRUCKBOX_REVB1_LITE=ON -DPROJECT_VER=0.2.27-lite build
idf.py -B build-link-lite -DTRUCKBOX_ROLE=link -DTRUCKBOX_REVB1_LITE=ON -DPROJECT_VER=0.2.27-lite build
sh tests/run_all.sh
```

Para aceite físico, execute ao menos 2 h com coleta, carga local e perda/restauração de Wi‑Fi. Registre heap livre, mínimo e maior bloco dos dois ESPs, o motivo de reset, as cadências por captura, latência de chegada ≤30 s em conexão saudável e número de linhas/seq antes e depois do corte. Faça ciclos de desligamento durante fila pendente, ACK inválido em homologação, fila próxima da cota, partida do Volvo e nova requisição ativa só após prova elétrica. A ausência de resets, a margem de RAM em execução, o TPS real de TLS e a latência de ponta a ponta dependem desse ensaio. O firmware mantém as limitações físicas de J4, alimentação do Link, modem e watchdog da Rev.B.1.

O GPIO1 continua como entrada no Core. Se uma unidade já recebeu o retrofit de U8 com RSTn ligado e precisa de DONE, valide a compatibilidade desse retrabalho antes de trocar a imagem em campo.
