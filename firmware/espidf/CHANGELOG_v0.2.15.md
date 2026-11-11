# Core 0.2.15 — CANDIDATA, não instalada

- Corrige a parada do modo ativo por uma única janela com erros ou por comparação da taxa atual com a calibração antiga.
- Separa falhas de recepção, eco/TX, falta de resposta e conflito de MID. Erro de checksum não é prova de interferência do TruckBox.
- Exige persistência em janelas recentes para degradação RX; mantém parada por perda de recepção durante TX, silêncio, conflito, falhas repetidas, fim da autorização e comando manual.
- Registra os contadores, intervalos, limites, condição e estado do momento da decisão. Identifica números históricos e mantém respostas como candidatas.
- Não rearma após reboot, Wi-Fi ou falha. Falha real exige reconfiguração explícita e nova autorização/validação.
- Preserva os 17 PIDs, MIDs, cadências, leitura passiva, J1939, armazenamento, Link, Cloud, NVS e partições. Somente o Core recebe imagem candidata.
- Acrescenta identificação do ELF no diagnóstico de boot para futuras auditorias.
- Inclui testes de ruído isolado/persistente, mudanças de tráfego, UART real simulada, eco, timeout, reboot, conexão, resposta espontânea, referência antiga e regressões existentes.

Leia [a auditoria](REV_B1_0.2.15_AUDITORIA.md) e [os testes](TESTES_v0.2.15.md). A causa física e o ramo exato do incidente de 10/11 permanecem indeterminados. Ensaios locais não validam eletricamente a Rev.B.1. OTA e ativação em campo dependem de aprovação explícita.
