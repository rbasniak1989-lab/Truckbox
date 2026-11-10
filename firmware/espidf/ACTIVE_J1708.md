# J1708/J1587 ativo — Core 0.2.9, versão de testes

A especificação vigente e o relatório estão em [J1708_FIX_v0.2.9.md](J1708_FIX_v0.2.9.md).
TX sempre inicia desarmado. Recepção passiva, Wi-Fi, ACK e coleta sem GPS continuam
independentes do transmissor. Nenhuma alteração de GPIO ou alimentação foi feita.

## Fluxo pelo painel do Core

1. Caminhão parado, velocidade zero recebida há no máximo 5s. Entre com a chave
   administrativa, informe MID local comprovadamente livre e confirme a topologia
   elétrica. A lista de MIDs observados é uma ajuda, não prova que outro está livre.
2. Selecione PID 26 para ensaiar os 17, ou ponto de retomada. Clique em
   **Varrer um PID por vez (parado)**. O teste autorizado dura até 60 s.
3. Após três ecos completos e uma resposta candidata do teste atual, clique em
   **Ativar durante a viagem** ainda parado. Também é possível promover a varredura
   concluída dentro do prazo original de 60 s. O modo viagem consulta todos os 17
   PIDs, mesmo se o ensaio começou no117.
4. Em modo viagem, velocidade positiva não desarma o transmissor. Há uma pendência
   por vez, intervalo mínimo1.2s, timeout450ms e autorização de até 12 h neste boot.
   Ausência de Android, internet, horário ou GPS não altera a autorização.
5. **Parar requisições**, OTA, conflito de MID local, três erros consecutivos de TX
   ou expiração da autorização liberam DE21 e desarmam. Reboot não retoma TX.
   O latch de três falhas exige corrigir a causa e reiniciar; não basta clicar Armar.

## API existente

`POST /api/j1708/requests`, Bearer da chave administrativa existente.

Para `action=arm`, enviar authorized=true, stationary=true,
 topology_confirmed=true, priority=8, local_mid escolhido explicitamente,
 preset="fh2013_capture_20261029", sequential_scan=true e start_pid=26.
Também aceita os 17 alvos explícitos em vez do preset. MID deve ser inteiro128..255,
fora dos alvos e não observado na rede. Não copiar MID sintético dos testes.

Promoção: {"action":"enable_work","authorized":true,"stationary":true}.
Parada: {"action":"stop"}. Configuração/MIDs não são persistidos para ativar TX.

Destinos do preset preservado da referência NEXIQ/Validator29/10:

| PIDs | MID |
|---|---|
|26,68,108,158,235,236,250|128|
|31,32,33,34,37,63,160|130|
|117,118|140|
|62|144|

São referências históricas, não prova de causalidade de toda leitura.
235/236/250 continuam acumuladores: nunca são zerados pelo firmware.

## Topologia e acesso ao barramento

SN65HVD1781, RX20, DE21 com UART invertida, DI=0 e /RE=0, conforme a base WROOM.
Não ligar um TX convencional ao DI nem reutilizar pinagem MINI do gerador antigo.
A saída usa o mesmo pedido direcionado: MIDlocal,128,PID,MIDalvo,checksum.

Antes de transmitir: guarda de5ms, RXalto, nenhuma mensagem parcial/emprocessamento,
nenhum evento pendente, ring buffer e FIFO física vazios. Bordas reais de RX20 são
monitoradas por ISR IRAM somente quando armado. A verificação é repetida após
configurar o roteamento. Barramento ocupado é adiado, sem apagar RX nem contar erro.

Divergência de eco libera DE imediatamente. Há espera variável20..50ms mais a
frequência mínima e nova guarda de silêncio para tentar o mesmoPID. Três falhas
consecutivas desarmam; um eco completo reinicia esse contador. Um timeout de resposta
com eco correto não é falha elétrica: o agendador passa ao próximoPID.

## Diagnóstico

/api/state -> j1708.requests: sent, echo_ok, responses, timeouts, collisions,
echo_timeouts, bus_deferrals, consecutive_tx_errors, rx_edge_monitor_ready,
rx_edges, event_dropped, uart_start_faults, work_mode, lease_remaining_ms,
scan_sent (progresso), scan_attempts (inclui repetições).

hardware_tx_available apenas indica UART+monitor inicializados;
hardware_tx_validation permanece EM_VALIDACAO. Eventos REQUEST, TX_ECHO,
LINK_ERROR e TIMEOUT registram quadro TX, relógio monotônico e evidência de bordas.
Respostas candidatas individuais usam evento RESPONSE_VALUE com MID/PID/valor/unidade,
preservando compatibilidade do tipo J1708_REQUEST_EVENT no backend.
JSON e armazenamento de eventos ocorrem fora do mutex RX/TX, em fila limitada16.
Falhas de fila/persistência incrementam event_dropped.

## Limite da validação

Testes de host e compilação não demonstram o tempo elétrico de liberação do
SN65HVD1781. A primeira habilitação desta versão exige ensaio estacionado e
verificação da instalação. NEXIQ demonstrou consultas na rede do caminhão;
não mede automaticamente o transmissor desta placa. A compatibilidade física
com NEXIQ permanece dependente do hardware. Não há transmissão automática neste pacote.
