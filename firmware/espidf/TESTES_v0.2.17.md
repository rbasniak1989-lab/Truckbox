# Verificação da candidata Core0.2.17

Nenhuma placa foi atualizada, reiniciada ou configurada por esta tarefa. Não houve transmissão física nova, deploy Cloud, escrita SQL em produção ou alteração de viagens. Resultados de software não comprovam eletricamente TX ou interpretação OEM.

## Evidência de origem e capturas

Fontes reais0.2.16, commit-base `0bbc8539aee9d051ca79aa3ec9436b2a9f11846b`; branch candidata e PR[#19](https://github.com/rbasniak1989-lab/Truckbox/pull/19). A amostra de boot306 na Cloud identifica a versão e o hash ELF correspondentes ao pacote0.2.16 preservado.

Bytes reais10/11: seq50193 `12B501`, MID128; seq50192 `0235`, MID136. Bytes reais11/11: seq58610/58611 e amostras próximas, com hexadecimal/idade/MID legados preservados. Arquivos sanitizados somente de leitura: [10/11](test_evidence/v0217_cloud_10nov_readonly.json) e [11/11](test_evidence/v0217_cloud_readonly.json).

Os valores de29/10 vêm das amostras preservadas em `firmware/j1708_17pid/PASSIVE.md` e testes existentes. Os casos MID130 `25A101`/`25E100` foram fornecidos nos requisitos. Quadros J1708 completos reconstruídos para teste, tráfego sintético e multisseção sintética estão identificados como tal. **Os ZIPs NEXIQ/Logs Mãe originais não estão disponíveis e não foram integralmente reprocessados.**

## Executado

| Verificação | Resultado e alcance |
|---|---|
|Build ESP-IDF5.5.2, ESP32-C6, Core|PASS, aplicação0.2.17, flash8MB e perfilJ1708_ONLY|
|Suíte `tests/run_all.sh`|PASS: armazenamento, ACK/recovery, UART, J1939, J1587, controlador ativo, modos automáticos/manuais, viagens, memória/OOM, GPS e testes Python|
|`run_store_transactions.sh`, `run_store_format.sh`, recuperação/worker/codec|PASS: interrupções, CRC, seq/ACK, registros válidos preservados, sem GPS e sem apagar evidências|
|`run_j1708_active.sh`, `run_j1708_field.sh`, `run_j1708_rx_guard.sh`, `run_j1708_uart.sh`|PASS: eco, arbitragem, conflitos, falhas, timeout, ruído isolado, mudança de tráfego, dados históricos, internet independente e continuação passiva|
|`run_j1708_production.sh`|PASS: boot/reboot automático,17anteriores+10novas, MID/PID, página2, cadências, prioridade,≤2inícios/s, uma pendência, pausa e recuperação saudável|
|`run_legacy_profile.sh`, `run_capture_integration.sh`, `run_telemetry.sh`, `run_trip.sh`, `run_worker_fairness.sh`|PASS: aquisição e funções antigas preservadas, sem RAW contínuo no perfil, qualidade/bytes/fontes e retomada|
|PID194 portátil|PASS: quatro casos obrigatórios, contador0/ausente, múltiplos códigos,119entradas, página2 PID/SID, desconhecidos, inválidos/vazios, estado por chave, ausência não resolve e capacidade sem sobrescrita|
|PID192|PASS: quatro MIDs concorrentes, sequência, comprimento, timeout, reinício e payload máximo239bytes; teste sintético|
|`run_volvo_diagnostics.sh`|PASS com módulos reais e mocks somente nas fronteiras: parser do fio retirando comprimento, dedup de1000repetições, NVS A/B/CRC/checkpoint interrompido, store indisponível, capturas/start/end/reboot, UID64bytes, erro de NVS sem ACK fictício, ausência, retry, reset/MID/tempo/saltos, I-Roll/VEB/diferença de rotação|
|Configuração NVS real com fronteira simulada|PASS Core, Link e perfil legado: migração única, opt-out, cadências antigas, intervalos de parada, falha/corrupção de extensão sem bloquear passiva ou apagar credenciais|
|Python `unittest discover`|24testes PASS, incluindo empacotamento, preservação de NVS no script Windows e regressões existentes|
|Cloud `node --test cloud/diagnostics/test_diagnostics.mjs`|9testes PASS: vetores reais, histórico, desconhecidos, grupos incompletos, catálogo idêntico ao Android, deltas/limites tardios e autenticação/escopo/paginação de extremos|
|Adaptador Edge TypeScript|Sintaxe PASS sem executá-lo; não é comprovação de deploy Deno ou índices/performance em produção|
|Mock da ingestão v26 atual|PASS: novos eventos preservados/ACK, sem alterar latest, viagens, cálculos de consumo ou dados financeiros|
|Painel embarcado Playwright|22cenários PASS na CI; fixtures HTTP locais, sem placa/OTA real|
|Motorista e Multimídia|Build debug e testes JVM de regressão PASS na CI; não instalados em telefone/central nesta tarefa|

Evidências: [host](test_evidence/v0217_host.txt), [build](test_evidence/v0217_build_core.txt), [offline](test_evidence/v0217_offline.txt), [módulos Volvo finais](test_evidence/v0217_volvo.txt), [Cloud unit](test_evidence/v0217_cloud_unit.txt), [ingest mock](test_evidence/v0217_cloud_ingest_mock.txt), [imagem](test_evidence/v0217_image.json), [memória](test_evidence/v0217_memory.json) e [stack](test_evidence/v0217_stack_frames.txt).

As primeiras execuções de CI verificáveis estão em [Core+browser+Cloud](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43123525253) e [Android](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43123525255), commit `b46a10b`. A persistência imediata das extremidades foi acrescentada depois e verificada localmente; a CI do commit de entrega consta no PR. O binário entregue é identificado pelo seu manifesto/hash, não por um APK de execução anterior.

## Duas horas offline com volume de caminhão

Teste de7.200segundos **virtuais**, executando decoder/cache/serializadores e módulos de diagnóstico/contadores reais. Injeta60quadros/s mais respostas, alterna janelas de arrancada, exerce27PIDs, eventos de início/fim, falha conhecida de motor/ABS/transmissão, armazenamento e reconexão simulada. As reinicializações são da camada de armazenamento; o controlador e a NVS têm testes de reboot próprios. Não é emulação integral do ESP32 nem ensaio elétrico.

| Medida | Sem UTC | Com UTC |
|---|---:|---:|
|Quadros processados|441.135|441.135|
|Consultas/respostas candidatas simuladas|8.175/8.175|8.175/8.175|
|Registros produzidos|3.789|3.789|
|JSON antes da compressão|2.473.390B|2.572.474B|
|Ocupação física dos registros compactados|1.477.083B|1.595.469B|
|Orçamento de segmentos do teste|1.818.624B|1.818.624B|
|Segmentos ocupados|31|33|
|Descartes por flash cheia / erro de escrita|0/0|0/0|
|Reinícios da camada store|14|14|
|Linhas únicas após sincronização|3.789|3.789|
|Entregas, incluindo repetição por ACK perdido|3.797|3.797|
|Duplicações Cloud / pendentes válidos finais|0/0|0/0|
|RAW contínuo gravado|0|0|

Margem no pior cenário: **223.155B**, cerca de12,27% do orçamento. A extrapolação linear desta carga é aproximadamente2h17min, mas **somente duas horas foram ensaiadas**. Variação de sensores, erros, falhas novas e tamanho de identificadores alteram a ocupação.

O teste também preserva arquivos de evidência após corrupção/ACK:684.772B semUTC e684.788B comUTC. Eles continuam no disco e reduzem o espaço de outra sessão offline; não foram somados à ocupação inicial anterior à recuperação nem apagados para alegar autonomia. Não se garante duas horas em uma partição já ocupada por arquivos preservados. A capacidade real deve ser lida no diagnóstico local.

## RAM, flash e tráfego

- RAM estática física ELF: **203.524B→210.084B**, acréscimo**6.560B**. Não é redução de RAM. IRAM e dados inicializados permanecem iguais; o aumento está em BSS.
- Agendador868→1.348B; controlador1.136→1.144B; evento de fila392→392B. Não aumenta a capacidade da fila de eventos/RX nem cria uma nova tarefa de polling.
- Histórico DTC2.576B, sessões1921.072B, contexto/estado e contadores limitados. Novos blobs NVS vivos A/B somam**8.736B** antes dos metadados/GC, em partição NVS24.576B também compartilhada com configurações anteriores. Espaço físico final da NVS e heap livre/mínimo na Rev.B1 **não medidos**. NVS cheia gera erro, sem erase; não é simulada como sucesso.
- Maiores frames de stack novos medidos pelo compilador: flush de contadores1.920B e consulta de contadores1.168B. Stack da snapshot6.144B e HTTP10.240B já existentes. Essas medidas são frames de funções, não high-water físico nem soma completa das chamadas.
- Em host64bits, consulta compacta usa árvore cJSON9.168B/10.373B conforme fixture; o teste injeta falha de cada alocação. Esses números não são heap do ESP32.
- Aplicação final: tamanho e SHA256 estão em [v0217_image.json](test_evidence/v0217_image.json); o manifesto do pacote é a referência de integridade. Partição OTA continua3.145.728B; aproximadamente51% livres.
- As17consultas originais têm demanda nominal0,756/s normal e1,789/s durante janela acelerada. Com10novas: aproximadamente**0,957/s** e**1,990/s**. São metas antes de adiamentos/retentativas/capturas; o teto físico do escalonador continua2inícios/s.
- No ensaio offline:1,135consulta/s média. A entrada sintética não comprova ocupação elétrica real. Apenas os bytes de duas requisições máximas de6bytes/s equivaleriam a120bit/s em9600baud, sem contar respostas/arbitragem/intervalos. Uma resposta máxima de21bytes para cada uma elevaria esse orçamento teórico a540bit/s. A disponibilidade real é decidida pelo driver/guardas, não por essa conta.
- Não há dump bruto contínuo nem dois registros de sucesso por requisição. Exportação operacional/fast10s, saúde/slow60s,5linhas de estatísticas de PID/minuto, Volvo1/minuto, DTC por alteração e capturas limitadas de contadores. Repetições de valores estacionados são suprimidas quando aplicável.

Reproduzir memória: ambiente IDF ativo e `python tools/report_v0217.py CAMINHO_ELF_0.2.16 build-core/truckbox_core.elf`. O script usa seções físicas e tamanhos DWARF; não conta padding fictício de flash como RAM.

## Reproduzir

```sh
# Ativar a instalação ESP-IDF v5.5.2 conforme BUILD_ENVIRONMENT.md
cd firmware/espidf
bash tools/build_v020.sh core
sh tests/run_all.sh
cd ../..
node --test cloud/diagnostics/test_diagnostics.mjs
node cloud/diagnostics/check_entry.mjs
```

Android: Java17, AndroidSDK36 e Gradle9.4.1, conforme `.github/workflows/android-build.yml`; `gradle :app:testDebugUnitTest :app:assembleDebug :multimedia:assembleDebug`, com os patches de compatibilidade Multimídia já existentes no workflow. Não depende de Cloud/deploy para compilar. O pacote fonte preserva módulos irmãos, scripts e entradas de build.

## Pendente e aprovação

Não medidos: TX/colisões elétricas das novas consultas, respostas OEM dos dez candidatos, interpretação mecânica da embreagem, comportamento da alimentação/NVS/flash na placa e interface Android em telefone real. Não se cria evidência externa fictícia nem nova exigência de campanha NEXIQ. Catálogo FH por chassi e publicação autorizada do serviço de consulta continuam pendentes.

Os detalhes, instruções manuais de atualização/rollback e demais limites estão em [REV_B1_0.2.17_CANDIDATA.md](REV_B1_0.2.17_CANDIDATA.md). Não houve merge nem atualização de produção.
