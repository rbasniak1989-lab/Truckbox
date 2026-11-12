# Core Rev.B1 0.2.17 — candidata J1708 e diagnóstico Volvo

Estado: **não instalada**. Não houve OTA, reset, ativação de TX, alteração de configuração em campo, deploy Cloud, apagamento de falhas ou modificação de viagens nesta entrega.

## Base verificada

- Branch exclusiva: `feature/revb1-v0217-volvo-diagnostics`; PR [#19](https://github.com/rbasniak1989-lab/Truckbox/pull/19), base `feature/revb1-v0216-auto-j1708`, commit `0bbc8539aee9d051ca79aa3ec9436b2a9f11846b`.
- A consulta Cloud somente de leitura identificou `tb-c6-d40592b9190c`, boot306, versão0.2.16. O ELF SHA256 informado pelo dispositivo coincide com a distribuição0.2.16: `6a5b47f59a2111f4ac5c9fd772105ebee8926a38f3903dc2f63add6a47ce365a`.
- ESP32-C6, flash8MB, ESP-IDF **5.5.2**. GPIOs, driver UART, transceiver, alimentação e tabela de partições preservados. A v0.1.0-b1 e as distribuições posteriores não foram sobrescritas.
- Perfil padrão continua **J1708_ONLY**. O código J1939 permanece no projeto e nos testes; esta alteração não investiga nem corrige os zero quadros CAN em campo.

## Polling: uma fila, 27 PIDs únicos

As 17 consultas existentes mantêm MID, codificação, autorização persistente, arbitragem, timeout e comportamento de produção. O teste manual continua separado do funcionamento automático. A expansão acrescenta dez candidatas à mesma fila, sem outro transmissor/agendador.

| PID | MID destino | Normal | Arrancada | Situação |
|---|---:|---:|---:|---|
|26|128|60s|60s|Preservada|
|31|130|10s|2s|Preservada; prioritária na arrancada|
|32|130|30s|10s|Preservada|
|33|130|5s|2s|Preservada; prioritária na arrancada|
|34|130|15s|5s|Preservada|
|37|130|30s|15s|Preservada|
|62|144|60s|60s|Preservada|
|63|130|15s|5s|Preservada|
|68|128|60s|60s|Preservada|
|108|128|60s|60s|Preservada; não altera o intervalo legado de ensaio|
|117|140|60s|60s|Preservada|
|118|140|60s|60s|Preservada|
|158|128|60s|60s|Preservada|
|160|130|30s|10s|Preservada; significado Volvo ainda em validação|
|235|128|900s|900s|Acumulado; capturas especiais adicionais|
|236|128|900s|900s|Acumulado; capturas especiais adicionais|
|250|128|300s|300s|Acumulado; capturas especiais adicionais|
|93|128|60s|60s|Nova candidata|
|94|128|30s|30s|Nova candidata prioritária|
|107|128|300s|300s|Nova candidata|
|153|128|30s|30s|Nova candidata prioritária|
|154|128|300s|300s|Nova candidata; MID precisa de confirmação pelo resultado|
|161|130|10s|10s|Nova candidata prioritária|
|182|128|300s|300s|Nova candidata; não pressupõe reset por viagem|
|188|128|900s|900s|Nova consulta, reutiliza o decoder passivo existente|
|244|128|300s|300s|Nova candidata; não pressupõe reset por viagem|
|439|128|300s|300s|Nova candidata, página2 / solicitação PID384|

As cadências são metas. Uma requisição fica pendente por vez, com espaçamento global mínimo500ms, máximo2inícios/s e timeout450ms. A proteção pode reduzir o ritmo para1/s, adiar consultas ou suspender TX. Mantém eco de bytes, barramento livre, conflitos de MID, falhas consecutivas, parada manual e recuperação saudável da0.2.16. As consultas novas não garantem resposta da ECU.

Exemplo **de teste de codificação**, não captura elétrica: localMID172 → MID128/PID439: `AC FF 80 B7 80 9E`. O `FF` troca a página; `80` da página2 é a solicitação componente-específica PID384. O quadro tem seis bytes e checksum válido. Os quadros das 17 consultas antigas continuam com cinco bytes.

Os dez MIDs novos são associações candidatas com base no levantamento fornecido. O ZIP NEXIQ original não está disponível para conferência independente. REQUEST, eco e resposta dentro da janela continuam **RESPONSE_CANDIDATE / OPERATIONAL_INDIRECT**, sem conversão para validação elétrica externa.

## Decodificação e qualidade

O [inventário](../j1708_17pid/INVENTARIO_v0.2.17.csv) contém75PIDs catalogadas e a PID192 de transporte, separadamente. Não corresponde aos76sinais do inventário J1939 histórico. As48PIDs passivas e as17anteriores foram preservadas;188 não é duplicada. Acrescenta as outras nove candidatas e a PID90 passiva, observada na Cloud.

Novas fórmulas:93=int8×27,1Nm;94=uint8×0,0345bar;107=uint8×0,0498kPa;153=int16LE×0,0078125kPa relativa;154=8estados de2bits;161=uint16LE×0,25rpm;182=uint16LE×0,473176473L;244=uint32LE×0,1609344km;439=uint16LE×0,00125bar relativa. PID90=uint8×2/3−160/9°C. O decoder188 existente usa0,25rpm/contagem.

Valores, origem, bytes, idade, unidade e qualidade acompanham os dados. Indisponíveis não viram zero; parâmetros desconhecidos continuam brutos. As conversões do veículo permanecem **EM_VALIDAÇÃO**, inclusive percentuais, estados e significados proprietários da transmissão. PID162 é faixa **selecionada**, PID163 faixa **atingida**. A faixa ASCII `A` não é convertida em marcha numérica. O painel mantém um único alias de marcha/pressão equivalente e não substitui um alias J1939 já presente.

## PID194 e catálogo Volvo

- O parser recebe somente os bytes de valor, depois de retirar MID/PID/comprimento/checksum. Interpreta0x80=contador,0x40=inativa,0x20=padrão,0x10=SID e FMI nos quatro bits inferiores.
- Aceita múltiplos códigos, contador0, contador ausente, página2, código desconhecido e mensagem vazia. Valida a lista inteira antes de alterar o histórico. Ausência ou lista vazia nunca resolve uma falha anterior.
- PID192 monta mensagens194 de até239bytes, com quatro sessões MID independentes, sequência/comprimento/timeout controlados. O parser comporta119entradas de dois bytes; o histórico local tem32chaves distintas e não sobrescreve outra falha ao lotar. Mudanças brutas completas podem seguir à Cloud em fragmentos de32bytes.
- Histórico: MID/tipo/identificador/FMI, primeiro/último momento, estado, contador, bytes e contexto recente de velocidade/RPM/acelerador/marchas. NVS alternada com CRC, checkpoint limitado, replay sem nova notificação. Novas ativações e mudanças de estado são diferenciadas de repetição.
- A biblioteca pequena e versionada está em [cloud/diagnostics](../../cloud/diagnostics); o aplicativo contém cache das mesmas três definições. O firmware não armazena uma base OEM grande.
- SID18/FMI5 não confirma água no diesel. A descrição de circuito do sensor é candidata. Foi encontrada divergência em manual Volvo **VN/VHD/VT2007**, que identifica uma válvula do separador: outra família, sem comprovação para o FH2013. SID2/ABS depende da configuração da central. Sem correspondência comprovada, usa módulo+código+FMI genérico, sem gravidade automática.

## Contadores por viagem

250,236e235 são solicitadas **sequencialmente** pelo mesmo agendador, no boot após rede saudável, parada estável≤0,9km/h por15s, a cada300s estacionado com indicação válida de ignição, e eventos START/END de viagem. Os dois intervalos de parada são configuráveis. Não há reset de contador ECU.

Cada resposta conserva seu próprio boot/tempo/MID/raw/valor/unidade/qualidade. O grupo não é apresentado como simultâneo. Ausência mantém a última leitura válida identificada, marca o novo grupo incompleto e permite até duas tentativas adicionais espaçadas60s. Um timeout não fabrica valor.

Baseline e estado usam NVS A/B com CRC, limite de escrita60s quando houver mudança e persistência imediata da fila e das extremidades de viagem. Valores periódicos sem mudança não produzem gravações repetidas. O ACK do evento de viagem só é devolvido após persistir sua fila em NVS. O aplicativo mantém uma fila local limitada de64eventos até esse ACK; ela não arma TX nem depende da Cloud para entregar ao Core.

Diferenças250/236/235 validam MID, ordem, tempo e saltos. Regressão de contador não é transformada automaticamente em rollover. Diesel fora da marcha lenta só é calculado quando as extremidades250/236 estão temporalmente próximas e a subtração é coerente.182e244 não são assumidas como zeradas por viagem TruckBox.

O Motorista usa a referência250 quando há baseline da mesma viagem, leitura recente e captura inicial até30s do evento. Caso contrário preserva a integração/estimativa existente e identifica o motivo. A consulta Cloud também informa limites tardios/incertos e não autoriza tratá-los como consumo exato da viagem. Não altera viagens encerradas, custos, fretes ou o algoritmo financeiro.

## Derivados e integração

- I-Roll **provável** exige velocidade, acelerador, RPM, automático e neutro recentes, com persistência3s. Referência de marcha lenta188 quando disponível; faixa provisória explicitada quando ausente. Tempo/distância/combustível são estimados, com lacuna de vazão identificada.
- VEB usa somente percentual122; não infere estágio de alavanca.
- Diferença190−161 exige dados recentes e defasagem≤1s. Mudança de marcha e persistência são verificadas antes de emitir candidato. Não existe diagnóstico de patinação confirmado. A cadência161 de10s limita os pares temporalmente utilizáveis.
- Novos eventos são transportados como `J1708_REQUEST_EVENT`, compatíveis com ingest v26/ACK atual. Sucessos de polling são coalescidos em sensores/HEALTH e resumos; não geram dois registros a cada requisição. PID194 só acrescenta eventos brutos quando o conteúdo muda.
- APIs locais: `GET /api/diagnostics?offset=0`, `GET /api/counters`, `GET /api/volvo`; consultas existentes de sensores/requisições mantidas. `POST /api/counters/capture` recebe evento de viagem, não comando ECU.
- Motorista0.6.2 acrescenta falhas ativas/inativas, horário, MID/código, bytes, descrições, verificações, filtro por viagem, paginação e cache limitado. A consulta remota tem autenticação e escopo por veículo. [Preparação Cloud](../../cloud/diagnostics/README.md) está entregue em fonte, **não publicada**. Consulta local funciona sem esse serviço.

## Limitações explícitas

1. Nenhum novo PID ou diagnóstico mecânico foi validado fisicamente nesta tarefa. Não se exige nova campanha NEXIQ para compilar/entregar; a resposta dos dez candidatos será verificável pelos contadores normais após uma atualização autorizada.
2. Os ZIPs completos NEXIQ/Logs Mãe não estão disponíveis. Os testes usam bytes preservados e amostras Cloud reais, além de cenários sintéticos identificados.
3. Sem UTC dos dois lados de uma reinicialização, a duração desligada é desconhecida. Contadores brutos são preservados, mas um delta que depende desse intervalo não vira referência validada. GPS não é necessário; relógio de sistema e boot monotônico são tratados separadamente.
4. Fontes detalhadas Volvo FH por chassi/ECU ainda faltam. Três descrições iniciais são candidatas; não há catálogo OEM completo ou suporte fictício a PPID/PSID.
5. O histórico local tem32códigos; o cache Android mantém64por consulta e até8consultas. Overflow e histórico incompleto são informados. Histórico remoto depende do deploy manual do serviço; filtros por viagem usam os dados e referências temporais disponíveis.
6. Eventos de viagem feitos por outro cliente precisam integrar a API de captura. Não foi criado um canal Cloud→Core para controlar TX, nem alteradas viagens cadastradas.
7. Flash e NVS continuam limitadas. O teste de duas horas parte de espaço livre; arquivos isolados antigos reduzem a autonomia futura. NVS cheia produz erro e preserva os dados, sem apagar configuração. Heap mínimo e resistência elétrica precisam de medição na placa.
8. microSD, FRAM, SIM/4G e J1939 nesta placa continuam com o estado/configuração anterior. Nenhuma alteração elétrica ou tentativa de mascarar a limitação do hardware.

## Atualização e rollback

Somente após aprovação do operador e com caminhão estacionado:

1. Salvar os diagnósticos atuais, configuração por meio seguro e pacote0.2.16. Não publicar tokens/senhas. Verificar o SHA256 do `.bin` contra o manifesto do pacote0.2.17.
2. Abrir o painel atual do **Core**, autenticar e selecionar apenas `truckbox_core_0.2.17_ota.bin` no fluxo OTA existente. Não usar bootloader, partição ou dados OTA como imagem de aplicação. O Link não precisa de atualização nesta entrega.
3. Após atualização manual, conferir versão, identidade, fila/ACK, recepção passiva, estado automático e `/api/j1708/requests?offset=0&limit=8`. As17consultas antigas continuam; dez novas são candidatas. Observar erros sem declarar sucesso elétrico por eco/contador.
4. Se necessário, usar o mesmo fluxo OTA para reinstalar `truckbox_core_0.2.16_ota.bin` do pacote preservado. O rollback automático do ESP-IDF continua habilitado para imagem que não confirma inicialização; não cobre toda falha funcional posterior.
5. O rollback ignora as novas chaves separadas `tb_cfg/v217`, `tb_diag`, `tb_ecuc`; elas não substituem credenciais, partições, NVS da viagem antiga ou ACK. Não apagar NVS/flash para voltar de versão.
6. Se Wi-Fi/OTA não responder, seguir o procedimento USB já existente [UPGRADE_RECOVERY.md](UPGRADE_RECOVERY.md), com os artefatos da versão escolhida e preservação de NVS/storage. Os scripts Windows são fornecidos para execução manual; nenhum foi executado contra o dispositivo.

Build e resultados verificáveis: [TESTES_v0.2.17.md](TESTES_v0.2.17.md). Mudanças: [CHANGELOG_v0.2.17.md](CHANGELOG_v0.2.17.md).
