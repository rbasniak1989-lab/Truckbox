# Core 0.2.16 — produção, aguardando instalação

- Polling permanente no ESP32 com política NVS e início condicionado à rede saudável, sem aplicativo/Wi-Fi/GPS ou renovação por boot.
- Migração única do perfil conhecido do Core informado; opt-out, credenciais e os 17 alvos preservados. NVS/perfil inválido ou conflito bloqueia TX.
- Prioridade 31/33 em arrancada, duas tabelas persistentes, um pedido pendente e limite global de 2 pedidos/s.
- Pausa e retomada por saúde/espera, preservando arbitragem, echo/RX, colisões, timeouts e parada manual.
- Monitor de arrancada e snapshots com MID, bytes, valor, unidade, idade e qualidade. Eventos mecânicos são candidatos, sem afirmar patinação real ou relação de eixos.
- Sucessos agregados para proteger a flash; contadores completos, erros, sensores, fila offline e ACK preservados.
- Painel distingue operação normal dos ensaios. Parar salva opt-out; OTA não apaga a política.
- Boot registra hash ELF completo, evitando a limitação anterior de prefixo da API.
- Somente Core atualizado; Link, partições, código J1939 e versões anteriores mantidos. Nenhuma OTA/deploy realizada.
