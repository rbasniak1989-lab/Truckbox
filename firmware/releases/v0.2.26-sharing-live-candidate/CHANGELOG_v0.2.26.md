# TruckBox Core Rev.B.1 — 0.2.26 candidata

- Corrige o compartilhamento de internet do Core: a BIN 0.2.25 publicada tinha encaminhamento IPv4/NAPT desabilitados na configuração efetiva. Agora Kconfig, compilador, empacotador e CI verificam também os bytes entregues.
- Adiciona `/api/live`, leitura limitada e sem árvore JSON no heap para instrumentos, saúde e estado do roteamento. O diagnóstico parcial por falta de RAM também oferece as leituras disponíveis.
- Disponibiliza marcha atual PID163, torque PID93 em Nm, percentual derivado da referência histórica de 2643 Nm do perfil conhecido e pressão de combustível PID94. Mantém origem, bytes, idade e qualidade; dados ausentes/desatualizados continuam nulos.
- PID94/MID128 passa a cada 10 segundos na fila de produção existente. PID117/118/250 continuam a cada 10 segundos. Não amplia a lista de requisições nem o limite de duas por segundo.
- Desativa TCP bruto 35000 e sua fila/tarefa no perfil J1708_ONLY. O cache continua com 128 leituras; seus bytes brutos passam a comportar o máximo integral de um parâmetro de um quadro J1708 válido.
- Multimídia 0.3.1-core-live usa `/api/live`, combina instrumentos locais com contexto de viagem da Cloud e exibe os códigos PID194 já decodificados pelo Core. Requisições Cloud/diagnóstico ficam independentes da atualização dos instrumentos.
- Preserva recepção passiva, polling automático/NVS/MID configurado, limites do barramento, armazenamento/ACK, contabilidade de combustível/viagem, OTA/rollback e interfaces existentes. Não modifica o firmware Link.

Sem OTA automática, formatação, limpeza de arquivos, alteração de viagens ou deploy de Cloud. É candidata de software; internet através da placa e leituras exibidas na multimídia ainda precisam de observação após uma atualização autorizada.
