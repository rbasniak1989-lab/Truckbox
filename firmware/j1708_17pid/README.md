# TruckBox Rev.B.1 — 17 PIDs J1708/J1587 desde o início

**Solicitados pelo proprietário:** 26, 33, 34, 37, 62, 68, 117, 118, 235, 236, 250, 31, 32, 63, 108, 158, 160.

Os **17 ficam habilitados no catálogo**. Eles usam uma fila global, com uma requisição pendente por vez, espaçamento inicial de 250 ms (até 4 pedidos/s), timeout de 200 ms, contadores individuais, desarme após 8 timeouts consecutivos ou 3 erros de link. O transmissor mestre exige autorização explícita de teste com veículo parado; não autorizar durante condução.

## Estado
- [x] Catálogo completo de 17 PIDs.
- [x] Agendador portátil C11 e testes de fila/limites.
- [ ] Integrar ao firmware real Core ESP-IDF, localizado no pacote ZIP de `firmware/releases/v0.1.0-b1/truckbox-b1.zip` na branch `firmware-revb1-ota-v0.1.0-b1`.
- [ ] Ligar callback `send_request(pid, context)` ao transmissor J1587 existente, com MID/endereço, enquadramento, checksum e acesso ao barramento corretos. **Não inventar bytes nem modificar os drivers que já funcionam.**
- [ ] Ligar parser de respostas ao `note_response(pid, timestamp)`, guardar MID e payload bruto. Correspondência temporal sozinha NÃO confirma resposta causal.
- [ ] Implementar os decodificadores dos 17 PIDs usando nossas capturas e inventário; validar comprimento, escala, unidades, valores especiais, qualidade e validade temporal.
- [ ] Expor valores para multimídia, motorista e nuvem e preservar logs brutos no microSD.
- [ ] Build ESP-IDF, replay das capturas e validação física supervisionada em caminhão estacionado.

## Testes locais
`cc -std=c11 -Wall -Wextra -Werror -pedantic src/tbx_j1708_requests.c tests/test_tbx_j1708_requests.c -o /tmp/test_j1708 && /tmp/test_j1708`

## Teste de carga
Iniciar medindo tráfego passivo, taxa de respostas e erros. Ativar os 17 na fila, comparar tráfego e latência. Aumentar carga somente de maneira controlada em bancada/barramento isolado; **não saturar intencionalmente a rede operacional do veículo**. O objetivo é obter os 17 PIDs com segurança, não transmitir o máximo possível.

O pacote v0.1.0-b1 deve permanecer intacto. Este módulo é apenas preparação para integração; **não há novo .bin compilado nem driver ligado**.
