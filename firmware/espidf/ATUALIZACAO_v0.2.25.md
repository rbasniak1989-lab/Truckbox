# Atualização e rollback — Core0.2.25 candidata

Somente a aplicação **Core Rev.B.1 / ESP32-C6 /8MB**. Link permanece instalado. A compilação e os testes de software não comprovam comportamento elétrico real nem resolvem falhas da alimentação física. Não foi realizada nenhuma ação no caminhão.

## OTA quando autorizada

1. Em uma parada com alimentação estável, conecte ao Core e autentique como administrador.
2. Confira UID `tb-c6-d40592b9190c`, versão atual e SHA256 do pacote. Registre os diagnósticos atuais; a página de manutenção não é necessária para esta atualização.
3. Envie **somente `truckbox_core_0.2.25_ota.bin`** pelo formulário OTA. Nunca envie ZIP, bootloader, NVS, flash completa ou tabela de partições. Não formate o armazenamento.
4. Após o boot, confira `/api/health` e `/api/state`. RAM baixa pode resultar em `diagnostic_partial=true` com os contadores essenciais. Para estatísticas use `/api/j1708/requests?offset=0&limit=8`; sensores em `/api/sensors?offset=0&limit=8`.
5. `automatic.enabled/persisted` deve manter a política desejada. Uma configuração anteriormente habilitada retoma sozinha após interface e três janelas recentes de RX saudável, aproximadamente30s. Uma desativação explícita do proprietário permanece desativada; a candidata não a contorna.
6. Confira crescimento real de `sent` e `responses`, último TX UART, idades e contadores por PID. `RUNNING` sozinho não prova que o fio transmitiu. MID local deve coincidir com o salvo pelo proprietário.
7. Confira117/118/250 configurados a10.000ms, sem rajada; idades podem aumentar por prioridade, arbitragem ou ECU silenciosa. `TX_ECHO_TIMEOUT` libera o TX atual, registra erro e não exige reativação global. RX indisponível, relógio inválido, OTA ou parada manual continuam suspendendo TX com motivo explícito.
8. Verifique fila flash e progresso de ACK pelo fluxo Core→Link→Cloud. Falta de internet não autoriza descartar registros pendentes.

Não é necessária outra campanha NEXIQ para executar os testes de software apresentados. O firmware mantém a classificação operacional indireta; esta entrega não cria comprovação elétrica independente.

## Combustível e Cloud

O Core mantém o total canônico e `trip_accounting.ecu_km_l` para a média consolidada de extremos compatíveis, com `average_partial=true` quando a cobertura da operação não é integral. `pending_l` permanece provisório. Uma resposta inalterada do250 é válida: a resolução de aproximadamente0,473L impede uma nova medição a cada10s em baixo consumo.

A Cloud em produção ainda executa `truckbox-ingest` v26 com projeção financeira antiga. Os campos adicionais continuam acessíveis no JSON bruto; não considerar os totais financeiros da Cloud já migrados ao PID250. O patch/migração revisável já existente em `cloud/README_FUEL_V2.md` **não foi implantado** nesta tarefa. Não executar SQL ou alterar viagens como parte da OTA.

## Retorno à versão anterior

O pacote inclui `rollback/truckbox_core_0.2.24_ota.bin`, extraído do artefato CI da PR27 e correspondente ao ELF observado no boot602. SHA256: `5d3d3bc914d1383d90af386044d53a645c03f6c4f020f5ec57def6e197b2374d`.

Faça OTA manual dessa aplicação pelo mesmo painel, quando autorizada, sem gravar partições/NVS. A0.2.24 contém as falhas de polling descritas nesta auditoria; rollback recupera a versão anterior, não corrige essas falhas. As tabelas de configuração e checkpoints v3/v4 são preservados; os slots adicionais da média são ignorados pela versão antiga. Se ela avançar os saldos, o CRC impede ressuscitar referências antigas ao retornar à0.2.25.

O rollback automático dos dois slots e do bootloader existentes permanece habilitado, segundo os critérios já existentes. Sua atuação elétrica/boot real não foi ensaiada pelo agente. Se HTTP ficar indisponível, os arquivos de recuperação e scripts Windows do pacote continuam disponíveis para acesso físico autorizado; não executar `erase_flash`.
