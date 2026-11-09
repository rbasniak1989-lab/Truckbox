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


## Candidato CORE 0.2.6 — requisições com o caminhão trabalhando

O CORE pode entrar em **work mode** sem interromper as requisições quando a
velocidade ultrapassar zero, mas **NUNCA ativa TX por padrão ou após reboot**.
Este é um recurso em teste, não uma certificação de compatibilidade J1708.
O motor, a I-Shift e os freios continuam sob controle exclusivo das suas ECUs;
os pacotes são apenas PID128 solicitando leituras.

### Preparação a partir da captura real (29/10/2026)

O JSON do teste estacionado pode usar
`"preset":"fh2013_capture_20261029"` em lugar do array de 17 alvos, com
`topology_confirmed:true` e **um local_mid inteiro explicitamente escolhido,
confirmadamente livre no barramento**. O preset somente preenche o **MID de
destino**, nunca o MID do TruckBox:

| PIDs | MID de destino observado |
|---|---|
| 26,68,108,158,235,236,250 | 128 (motor) |
| 31,32,33,34,37,63,160 | 130 (transmissão) |
| 117,118 | 140 (sistema de freios) |
| 62 | 144 (retarder) |

Esses MIDs foram correlacionados à captura NEXIQ/Validator, não validados
como respostas causais em toda condição. Não assumir que o MID local 250
dos testes unitários está livre: é **sintético**. Verifique os MIDs na
instalação real. Erro de MID pode fazer o controlador parar após timeouts.

### Fluxo de habilitação

1. Antes do teste, conferir com instrumentação apropriada as formas de
   onda RX20/DE21, nível físico J1708, polaridade e liberação do transceptor.
   Não fazer a primeira transmissão em trânsito. Atestar explicitamente
   `topology_confirmed:true` **somente após essa validação**.
2. Com o veículo parado (velocidade J1587 recente = 0), enviar POST autenticado
   em `/api/j1708/requests` com
   `{"action":"arm","authorized":true,"stationary":true,"topology_confirmed":true,"preset":"fh2013_capture_20261029","priority":8,"local_mid":<MID_LIVRE_CONFIRMADO>}`.
   O local MID deve ser um número real 128–255 livre; o texto entre
   `<...>` é apenas um marcador explicativo, **não JSON válido**.
3. Aguardar pelo menos três ecos completos `echo_ok>=3` e uma resposta
   candidata de MID/PID `responses>=1`, sem colisões/falhas, mantendo a
   velocidade zero e observando tráfego normal. Uma resposta candidata
   continua não sendo prova de resposta causada pelo pedido.
4. Enviar POST autenticado com
   `{"action":"enable_work","authorized":true,"stationary":true}`.
   O CORE só confirma se o caminhão está comprovadamente parado na amostra
   de velocidade mais recente (até 5 s) e se passou nas verificações acima.
5. Depois disso, a operação suporta o caminhão **em movimento**: não há
   desarme automático apenas por velocidade positiva. O espaçamento passa
   a pelo menos **500 ms** (até 2 solicitações/s; menor em barramento ocupado).
   A permissão dura até **12 horas** e não sobrevive à reinicialização.
   Renovação explícita pode usar `action=arm`, `stationary=false`,
   preservando o preset/MID previamente configurados.
6. `{"action":"stop"}` desarma imediatamente. Também desarmam: OTA,
   colisão/eco incorreto, erros repetidos, oito timeouts consecutivos,
   12 h sem renovação e conflitos de MID local. A recepção passiva continua
   independente desses eventos.

### Observabilidade e limites

`/api/state`, em `j1708.requests`, traz `work_mode`,
`work_evidence_ready`, `lease_remaining_ms`, `echo_ok`,
`collisions`, `sent`, `responses` e `timeouts`.
Na cloud, os HEALTH minuto recebem
`context.j1708_work_mode` e os contadores por janela
`metrics.j1708_request_sent`, `j1708_request_answered` e
`j1708_request_timeouts`. Sensores J1587 permanecem provisórios até
validação de fórmulas; especialmente pressão de óleo, turbo e pressão
pneumática do câmbio (PID37). Não dirigir ajustando comandos no celular;
fazer habilitação inicial estacionado e supervisionada.

**Advertência de hardware**: o circuito Rev.B.1 liga DI=LOW e modula DE21
com UART invertida; sem osciloscópio ou verificação equivalente, nem um
build bem-sucedido nem testes unitários demonstram que o transceptor transmite
sem perturbar o barramento físico. Essa verificação é necessária antes de
ligar TX no veículo, mesmo para o modo de teste parado.
