# 0.2.19 — candidata de recuperação da flash, não instalada

- Depois de falha de escrita na flash, remonta o mesmo volume FatFs antes de conferir os registros. Isso limpa bloqueios e estado de RAM que uma simples releitura não recupera. Não formata, reinicia a placa, troca o meio ou altera as partições.
- Identifica a primeira etapa da falha, errno realmente medido, erro de fechamento, segmento, sequência e quantidade de bytes. Mantém o último erro distinguido do estado atual e informa tentativas de recuperação.
- Preserva o registro em voo e a regra de durabilidade `fflush` + `fsync` + `close`. Recupera a sequência válida ou repete o registro retido; nunca inventa ACK. Falha persistente continua indicada como falha.
- Durante retry no mesmo boot, usa a parte ainda livre da reserva de sequências já confirmada em NVS. Evita novo commit e salto de 256 IDs a cada tentativa; após reboot, continua pulando a reserva persistida para não reutilizar IDs antigos.
- A recuperação só atualiza a reserva em RAM depois de confirmar seu commit em NVS. Se falhar, a próxima tentativa repete a persistência antes de liberar o armazenamento.
- Acrescenta `/api/crash` e `/api/crash.bin`, protegidos pela senha administrativa, para consultar/exportar a partição de crash em blocos de 512 bytes. Não apaga nem envia automaticamente o dump e não o atribui a um boot sem evidência.
- Preserva recepção J1708, agendador/cadências/MIDs, bloqueio persistente de TX, Wi-Fi, telemetria, Cloud, GPS opcional, J1939, identidade, NVS, fila e rollback. Link instalado não é atualizado.
- Acrescenta testes com FatFs real do ESP-IDF, nove etapas de falha de IO, erro persistente, CRC/ACK e exportação autenticada do crash.

As quedas de alimentação, o circuito EN/U8 e os dois crashes anteriores não foram fisicamente diagnosticados ou reparados por software. Consulte [a auditoria](AUDITORIA_v0.2.19.md).
