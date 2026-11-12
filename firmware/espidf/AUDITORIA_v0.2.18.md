# Core Rev.B1 0.2.18 — correção candidata de RAM e eco J1708

**Não instalada. Nenhuma OTA, reativação de requisições, reset, alteração de configuração em produção ou escrita na Cloud foi executada.** Branch `fix/revb1-v0218-memory-echo`, baseada na distribuição0.2.17, commit `822e49fd6167c1ab68757731c0758d40a922f620`. A 0.2.16 funcional e a 0.2.17 permanecem preservadas.

## Incidente confirmado e limites da evidência

O boot308 da Cloud identifica firmware **0.2.17**, com ELF SHA256 `473639353c1e7ce986fdc9e341319cabf46e33f20956750de48624449b82b38b`, igual ao pacote entregue. Não se usou uma implementação hipotética para investigar.

O operador informou `heap_free=41596`, `heap_minimum=12`, `heap_largest_block=14592` e erro `Sem memoria RAM para diagnostico completo`. Esses números foram fornecidos pelo operador; não são uma medição de heap da 0.2.18. O mínimo é histórico desde o boot e não sobe apenas por fechar uma página.

Consulta Cloud somente de leitura, horário de Brasília:

| Horário 11/11/2026 | Seq | Evidência |
|---|---:|---|
|21:32:11|59137|Boot308, firmware0.2.17, hash ELF correspondente|
|21:40:51,827|59347|AUTO_STARTED, política persistente OPERATIONAL_INDIRECT|
|21:41:12,024|59365|TIMEOUT PID153, destino128, janela450 ms|
|21:41:20,584|59366|STOPPED, LOCAL_MID_CONFLICT, último PID439, local172/destino128|
|21:42:11|59383|PID153:2 envios,1 candidata,1 timeout; candidata com 26 ms|
|21:43:11|59404|PID439:1 envio,0 candidatas,0 timeouts; parou antes de completar o timeout|
|Após a parada|59385 e seguintes|DISABLED / OPERATOR_OR_MAINTENANCE_HOLD; respostas passivas continuam registradas|

Os horários das linhas de evento sem UTC próprio são a referência reconstruída/persistida pela ingestão. O boot e uptime originais estão preservados em [v0218_boot308_readonly.json](test_evidence/v0218_boot308_readonly.json). Os eventos antigos **não contêm os bytes do quadro que provocou o conflito**. Portanto, há um defeito reproduzido compatível com o incidente, mas não prova de que não existia outro transmissor172 no barramento real.

## 1. Pressão de memória: o que foi comprovado

### RAM estática e filas

Medição dos ELF reais, contando as seções físicas de RAM:

| Medida |0.2.16|0.2.17|0.2.18|
|---|---:|---:|---:|
|RAM estática total|203.524 B|210.084 B|210.084 B|
|Agendador J1708|868 B|1.348 B|1.348 B|
|Controlador ativo|1.136 B|1.144 B|1.144 B|
|Item de evento|392 B|392 B|392 B|
|Fila de eventos|16 itens|16 itens|16 itens|

A 0.2.17 acrescentou **6.560 B estáticos**, principalmente histórico/contexto DTC, quatro sessões de transporte192, contadores e dez posições do agendador. A 0.2.18 mantém essas informações e **não alega recuperar esses6.560 B**. As capacidades das filas de aquisição, armazenamento e eventos não foram aumentadas pela correção.

A biblioteca detalhada de descrições Volvo é JSON da Cloud/aplicativo, **não uma biblioteca grande carregada no ESP32**. No Core ficam parser, histórico limitado e nomes/versão. O decoder passivo atualiza um cache estático; em J1708_ONLY/LEGACY ele retorna antes de montar JSON por quadro. Não se encontrou alocação contínua de biblioteca OEM a cada sensor.

### Diagnóstico completo e alocações transitórias

Comparação do mesmo teste nativo com cJSON real e fronteiras RTOS simuladas:

| Árvore JSON da captura |0.2.16|0.2.17|0.2.18|
|---|---:|---:|---:|
|Completa, sem histórico de ensaio|25.184 B|36.328 B|36.328 B|
|Completa, com histórico de ensaio|38.816 B|57.270 B e acesso fora dos limites|49.960 B|
|Compacta, sem/com ensaio|9.178/10.383 B|9.168/10.373 B|9.168/10.373 B|

**São bytes de um processo host64 bits, não heap físico do C6.** Mostram o aumento do custo dinâmico. A serialização HTTP já transmitia e liberava a árvore em blocos; o problema remanescente era montar a árvore completa antes de transmiti-la. A 0.2.17 ainda a escolhia por padrão em `/api/state` sem parâmetros.

No ensaio com orçamento de **41.596 B**, o caminho completo atingiu **41.577 B** e produziu dados incompletos. O novo caminho compacto atingiu **22.894 B**, já incluindo 8.192 B de upload e 3.232 B de checkpoint simulados residentes. Duzentas consultas repetidas não deixaram vazamentos. Não se afirma que essa contagem reproduza exatamente os 12 B mínimos da placa; falta rastreamento do alocador no hardware para atribuir cada pico real.

Correções:

1. `/api/state` passa a ser compacto por padrão. `?compact=1` continua válido. `?compact=0` solicita o modo completo, mas o servidor o adia e retorna resumo válido quando não há orçamento, com `full_diagnostic_deferred=true`. Detalhes seguem pelas APIs paginadas.
2. O limite conservador para tentar a árvore completa é64 KiB de margem de construção +16 KiB de reserva de trabalho. É política de diagnóstico, não regra SAE nem garantia de heap físico. Nenhum ajuste de proteção da rede depende desse número.
3. `/api/health` continua sem alocação cJSON e agora inclui boot_id, além de heap livre, mínimo e maior bloco. Fallback de falta de memória conserva esses indicadores. No estado normal, as métricas são tomadas antes da construção da própria resposta.
4. Estatísticas e estado de captura são copiados sob mutex; a montagem/serialização JSON ocorre **depois de liberá-lo**. O próprio diagnóstico não pode prender o RX/TX enquanto percorre centenas de alocações.
5. Sensores usam página máxima de 8 entradas e snapshot pequeno na stack, em vez de `calloc` de uma página grande. `next_offset`, contagem, MID, bytes, unidades, qualidade e últimos valores válidos continuam disponíveis. Falha de alocação retorna página ausente/fallback, sem sucesso parcial com campos deslocados.
6. Corrigido acesso `field.pids[i]` para índices17–26: o ensaio supervisionado tem17 entradas; as dez novas não têm esse histórico. A suíte antiga emitia aviso do UBSan sem reprovar. Agora `UBSAN_OPTIONS=halt_on_error=1` faz a suíte falhar nesses erros.

O snapshot usa stack já reservada: frame `capture_json_mode` de 4.576 B e página de estatísticas de 2.528 B, medidos pelo compilador. O servidor HTTP mantém 10.240 B de stack. Não é alocação de heap, nem medição de high-water físico. Nenhuma tarefa nova foi criada.

## 2. LOCAL_MID_CONFLICT / PID439

A 0.2.16 transmitia apenas solicitações de 5 bytes. A 0.2.17 acrescentou a PID439 com o quadro correto de 6 bytes, `AC FF 80 B7 80 9E`, e ajustou o comparador de eco. Entretanto, `j1708_task` ainda fazia a separação antecipada somente com `length == 5`.

O teste adiciona um eco de 6 bytes seguido por resposta válida na mesma entrega UART, com seis fragmentações diferentes do eco. Na0.2.17, o coletor deixa os dois juntos. A soma de dois quadros com checksum válido também é zero; o coletor pode reconhecer a concatenação como um quadro checksum-válido começando em172. `finish_j1708` então aciona a proteção real de conflito local. **Esse falso conflito foi reproduzido no código original antes da correção.**

Correção mínima: a separação antecipada considera os comprimentos possíveis do quadro TX atual,5 e 6 bytes, e usa o comparador exato existente. Não aceita prefixo incompleto, não amplia a janela de 30 ms do reconhecimento, não muda a arbitragem, não troca MID e não ignora todos os quadros172.

Depois da correção, o mesmo replay produz1 eco e 1 resposta candidata, sem conflito. Em seguida, um quadro distinto e checksum-válido de 172 continua parando o TX. Outra resposta ECU continua sendo recebida passivamente depois da parada.

Nova evidência limitada somente em falha: `MID_CONFLICT_FRAME` registra RX em hexadecimal, comprimento, progresso do eco e idade do TX. O evento contém também quadro TX e referência temporal. TIMEOUT passa a conservar o TX correspondente. Usa a fila/evento já existente, sem aumentar seu tamanho e sem dump bruto contínuo. Se a fila falhar, o contador de descarte aumenta; a parada não depende do sucesso do log.

Não é possível afirmar a causa elétrica exata do incidente308 sem seu quadro gerador. A relação entre falta de RAM, atraso de escalonamento e esse incidente também permanece hipótese; o diagnóstico prendia o mesmo mutex, mas não havia medição de duração desse bloqueio na captura real.

## 3. Timeout PID153

A Cloud registra uma falha de resposta e depois uma candidata de MID128/PID153 em26 ms:2 envios,1 candidata,1 timeout. Isso comprova que a ausência não foi permanente. Não comprova que o primeiro quadro chegou eletricamente, que a ECU sempre responde ou que a resposta foi causada exclusivamente pela requisição.

Foi mantido timeout450 ms, mesma codificação, destino128, arbitragem, taxa global≤2/s e retentativa limitada. Um teste específico confirma que um timeout transitório seguido de candidata não desliga indevidamente o modo ativo. Os testes de três falhas, eco divergente, erro UART e conflito verdadeiro continuam exigindo parada/bloqueio. Não se removeu nem alongou a proteção para obter27/27.

## Arquivos principais

- `components/tb_web/tb_web.c`: padrão compacto, orçamento do completo, métricas e saúde sem heap.
- `components/tb_core/capture.c`: snapshot fora do mutex, limite17 do histórico, separação do eco6 e evidência de conflito/timeout.
- `components/tb_core/telemetry.c`: paginação pequena sem heap contíguo de snapshot e falha atômica de página.
- `components/tb_core/tests/test_capture_integration.c`: reprodução do falso conflito e preservação da parada verdadeira/RX passiva.
- `components/tb_core/tests/test_diagnostic_memory.c`: orçamento41.596 B,200 consultas, uploader/checkpoint concorrentes simulados, ausência de alocação sob mutex, saúde com heap zero.
- `components/tb_core/tests/test_j1708_production.c`: PID153 transitória e proteções originais.
- `components/tb_core/tests/test_telemetry.c`, `components/tb_web/tests/test_json_http.c`: páginas, falha de cada alocação e contrato HTTP.
- `tests/run_all.sh`: sanitizador em modo de falha.

## Resultados e reprodução

ESP-IDF **5.5.2**, ESP32-C6, flash8 MB, tabela de partições inalterada. Build real aprovado. A suíte completa passa com ASan/UBSan, incluindo armazenamento/CRC/ACK/recovery, captura, J1939, J1587, PID194, requisições, viagens, GPS independente, fairness, JSON/HTTP,24 testes Python e o ensaio de duas horas offline. O CI da branch repete essas verificações; os resultados e o diff estão no PR de correção.

O ensaio offline mantém 441.135 quadros sintéticos,8.175 consultas/respostas candidatas e 3.789 registros; reconexão sem duplicação de linhas. Não é teste elétrico do caminhão nem replay integral de ZIPs NEXIQ indisponíveis.

```sh
# Ativar ESP-IDF v5.5.2
cd firmware/espidf
bash tools/build_v020.sh core
sh tests/run_all.sh
```

Evidências na pasta `test_evidence/`: `v0218_boot308_readonly.json`, `v0218_memory.json`, `v0218_image.json`, `v0218_stack_frames.txt` e logs de testes/build. Comparações dos ELF preservam a distinção entre RAM estática, alocação transitória host e heap físico não medido.

## Antes de qualquer OTA

Não reativar o dispositivo para testar esta branch automaticamente. A parada/política NVS do operador não é substituída por esta alteração; não foi enviado comando ao Core. O código não limpa MID observado nem promove eco a validação elétrica externa.

Na0.2.17 já é possível consultar somente `http://truckbox.local/api/state?compact=1` ou `/api/health`. Isso não arma o polling nem zera o mínimo histórico de RAM.

Depois de revisão e **autorização explícita separada**, uma OTA manual deve ocorrer estacionado, com requisições mantidas desabilitadas, conferindo memória, maior bloco, uptime, recepção passiva e ACK antes de discutir novo teste ativo. Nenhuma campanha NEXIQ é pré-condição para entregar o código; a comprovação elétrica não é inventada.

Rollback manual: aplicação0.2.16 preservada, sem apagar NVS, storage, credenciais ou viagens. Não usar tabela de partições/bootloader como OTA. A 0.2.17 permanece arquivada como referência do incidente, não como destino preferido de recuperação. O rollback automático do ESP-IDF permanece habilitado, mas não resolve sozinho uma regressão funcional após inicialização confirmada.

Pendente: medir heap mínimo/maior bloco e high-water no C6 após uma atualização autorizada; obter RX gerador caso haja novo conflito real. **Não se declara resolvida fisicamente a RAM ou a transmissão somente pelos testes de software.**
