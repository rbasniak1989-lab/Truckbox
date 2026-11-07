# Ensaio ativo J1708/J1587 — todos17PIDs

Catálogo inicial:26,33,34,37,62,68,117,118,235,236,250,31,32,63,108,158,160.
Todos estão habilitados no agendador. TX começa desarmado, sem endereços locais ou
alvos inventados. RX20 continua independente do agendador.

## Evidência da ligação

A release funcional documenta RX20 e DE21 no Core WROOM Rev.B.1, /RE=0 e DI=0.
A branch `hardware-revb-j1708-4g`, arquivo `hardware/reva/apply_revb_j1708.py`,
explicita a topologia dominante/recessiva: UART TX invertido em DE, receptor ativo.
Esse script antigo cita módulo MINI; sua numeração de GPIO não foi reutilizada.
A implementação usa exclusivamente DE21/RX20 do fonte WROOM da release funcional.
DI fixo não constitui, por si, impossibilidade física de TX. A forma de onda real
continua pendente de medição na Rev.B.1.

## API de controle

`POST /api/j1708/requests`, autenticado com a chave admin já existente.
O arquivo `j1708_request_config.template.json` inclui os17PIDs e MIDs nulos para
preenchimento por evidência. Nulos são recusados pela API, não viram endereço0.
Defina MID local livre confirmado e MID-alvo para cada PID; não copie endereços dos
testes unitários como se fossem da sua ECU. O firmware recusa MID local já observado.

Para armar, são obrigatórios `action="arm"`, MIDs inteiros128..255,
`authorized=true`, `stationary=true` e `topology_confirmed=true`. Priority8 é a
política conservadora inicial. Autorização/declaração de estacionamento expiram
em60s; renovar o mesmo corpo renova o lease sem perder espaçamento/pedido pendente.
Alterar endereços requer parar antes. A configuração não é persistida e reboot
sempre retorna ao estado desarmado.

Parada manual imediata: enviar `{"action":"stop"}` ao mesmo endpoint. O pino DE é
liberado antes de aguardar o mutex. OTA, movimento veicular detectado e lease vencido
interrompem o teste. Após uma parada automática de segurança, inspecione os erros;
o latch não é apagado por uma nova autorização. Reinicialização controlada após
corrigir a causa restaura o estado inicial desarmado.

## Proteções e limitações

- Pedido direcionado documentado: MIDlocal,PID128,PIDpedido,MIDalvo,checksum.
- Uma pendência;250ms de espaçamento mínimo; timeout450ms; máximo teórico4pedidos/s, reduzido por tráfego/latência.
- Guarda de barramento ocioso de pelo menos5ms, RX pendente/FIFO e nívelRX verificados. É política conservadora de ensaio; não se afirma arbitragem real certificada ou ausência de colisões.
- Eco byte a byte pelo RX sempre ativo; divergência libera DE. Deadline20ms do eco,3erros de link ou8timeouts consecutivos desarmam. Saída UART é desconectada da matriz e DE retorna LOW no aborto; TX FIFO anterior precisa terminar antes de reconectar.
- Eco próprio é persistido como TX_ECHO, sem alimentar sensores. MID+PID+janela só gera RESPONSE_CANDIDATE; tráfego passivo coincidente não comprova causalidade. Resposta indisponível é resposta, sem valor numérico válido.
- Eventos de configuração/armar/parar/pedido/eco/resposta candidata/timeout/erro são oferecidos ao armazenamento e expostos por contadores; falhas de armazenamento não são anunciadas como gravação bem-sucedida.
- Estatísticas totais/porPID em `/api/state`, incluindo colisões, eco, timeouts e lease. RX passiva continua mesmo com o transmissor desarmado ou em falha.
-235/236/250 são contadores acumulados. O firmware não emite comando para zerá-los; utilitário de delta exige leituras válidas da mesma origem e reestabelece marco após redução/troca. Persistir checkpoints por MID/PID fica a cargo do consumidor de viagens; brutos/leituras são persistidos.

Teste físico somente com autorização, caminhão estacionado e monitoramento da
recepção/erros. Esta implementação experimental não atua em motor, freios ou câmbio.
