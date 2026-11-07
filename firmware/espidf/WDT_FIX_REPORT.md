# Correção 0.2.0-wdt1 — watchdog sob tráfego contínuo

## Evidência de campo recebida em 07/11/2026

A placa com Core0.2.0 exibiu `task_wdt: Task watchdog got triggered`,
`IDLE (CPU 0)` sem atendimento e `CPU 0: tb_decode`. O console USB não respondia
normalmente e havia falha de conexão do celular ao AP. Os endereços MEPC0x4201a212
/RA0x4201a224, confrontados com o ELF exato da imagem0.2.0 entregue, apontam para
`tbx_j1939_tp_expire`, linha18, chamado no caminho de processamento de frames.
O endereço da interrupção não prova que essa pequena função tenha um loop infinito.

O watchdog monitorava IDLE com timeout15s; panic/reboot do taskWDT estava desativado.
Portanto, esse log mostra starvation/ausência de IDLE, não demonstra por si só um
reinício, uma senha alterada ou falha elétrica. A causa exata da associação Wi-Fi
continua a confirmar na placa depois da correção de escalonamento.

## Defeito encontrado e reproduzido

O timeout de `xQueueReceive` só bloqueia quando a fila está vazia. Com recebimento
contínuo, o decoder prioridade3 permanece pronto indefinidamente; o gravador de
prioridade4 tem o mesmo padrão. IDLE0 e o consoleUSB2 podem ficar sem oportunidade
de executar. A fase final de startup/main também pode ser atrasada.

O teste de regressão extrai os laços REAIS do código0.2.0 original e força uma
fila permanentemente não vazia. A ausência de oportunidades para tarefas inferiores
é reproduzida. Não se afirma que esse modelo no host reproduz temporização elétrica
ou Wi-Fi/FreeRTOS reais.

## Alteração

- Cada worker verifica o orçamento após cada item: no máximo16itens ou4ms de tempo
  decorrido entre verificações antes de bloquear por2ticks. Um item individual
  pode ultrapassar4ms; a verificação ocorre quando ele termina.
- Com tick100Hz da configuração real,2ticks garantem ao menos um tick inteiro
  bloqueado independentemente da fase. Isso permite executar IDLE, console e startup.
- O gravador libera o mutex da mídia ANTES da pausa, inclusive na rejeição rápida
  de registros quando a mídia está cheia.
- Filas e limites não foram aumentados; brutos, decoders, timestamps de captura,
  fsync por registro, CRC, reserva de sequência, recuperação e ACK permanecem iguais.
  Sob carga acima da vazão suportada, os contadores de overflow/rejeição continuam
  explícitos; a correção não promete captura sem perdas a qualquer taxa.
- Novos diagnósticos: `telemetry_v2.scheduling_yields`, `burst_max_frames`,
  `burst_budget_us`, `rest_ticks` e `storage.scheduling_yields`.
- Watchdog permanece habilitado e com15s. Não há alimentação artificial do watchdog,
  aumento de timeout, desativação de IDLE monitorada ou reinício forçado para mascarar o erro.
- Wi-Fi, senhas/NVS, GPIOs, partições, protocolos e configuração do Link não mudam.
  O pacote recompila Link apenas para identificação da mesma versão; corrigir este
  defeito exige atualizar o Core, não o Link.

## Verificação de software

`run_worker_fairness.sh` executa os laços de produção com fronteiras RTOS/clock
simuladas, filas sempre cheias, dez mil itens por cenário, custos0/100/2000/30000us,
ticks100/1000Hz e callbacks que representam oportunidade de IDLE/console. O laço
original reproduz ausência de pausas; os dois laços corrigidos passam, com pausa
fora do mutex. ASan/UBSan e C11 estrito habilitados. Isso é teste de escalonamento
modelado, não validação física de latência nem medida de throughput do ESP32.

```sh
sh components/tb_core/tests/run_worker_fairness.sh
# Em um checkout Git contendo a versão original, contraste negativo:
sh components/tb_core/tests/run_worker_fairness.sh c52c4d5f09482fcc5c56f2c1933e68316ab59b68
```

O relatório final de build e regressão desta correção fica em `test_evidence/`.
TEST_RESULTS.md preserva os resultados e hashes da entrega0.2.0 original; os hashes
válidos da correção são os do novo manifesto/SHA256SUMS.

## Aceite pendente na placa

Atualização manual do Core, sem apagar NVS ou registros. Depois de manter o mesmo
tráfego por mais de15s, conferir: console `setup/status` responsivo, IDLE sem novos
avisos taskWDT, Wi-Fi/AP e página local, filas/perdas/heap e gravação persistente.
Testar primeiro passivamente; solicitações J1708 permanecem desarmadas por padrão.
Se o watchdog persistir, coletar o novo log e a versão exata; não presumir a causa.
