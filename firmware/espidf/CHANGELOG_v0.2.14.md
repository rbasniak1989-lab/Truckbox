# Core 0.2.14 — candidata

- Acrescenta **Validação supervisionada em campo**, com autorização estacionado, comparação passiva antes/depois, três rodadas dos 17 PIDs e janelas sem transmissão.
- Exige ecos e respostas candidatas com MID/PID corretos e temporização válida; coincidências sem contraste suficiente deixam o teste inconclusivo.
- Separa confirmação operacional indireta de comprovação externa; mantém a opção de analisador independente e nunca preenche seus campos automaticamente.
- Permite autorizar o agendamento de viagem após confirmação explícita do resultado indireto. Mantém cadências de 1/5/15 minutos, arbitragem, uma requisição por vez e intervalo mínimo de 1,2 s.
- Acrescenta supervisão de tráfego, silêncio/erros, MID e registros de auditoria; mantém recepção passiva ao parar TX.
- Mantém a correção de RAM da 0.2.13, consultando detalhes em páginas de quatro PIDs.

Somente Core. Autorização fica em RAM, sem início automático após OTA/reinício. NVS, partições, armazenamento/ACK, sensores e viagens preservados. Validação na placa ainda pendente. Guia: `REV_B1_0.2.14_CANDIDATA.md`.
