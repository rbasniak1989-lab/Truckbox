# Core Rev.B.1 0.2.25 — candidata

Base: aplicação0.2.24 da PR27, commit `a9cd6bddfa6c1d176a7f8f9a8b46855dcd7ab3aa`, identificada pelo SHA ELF registrado no boot602. Wi-Fi/compartilhamento Starlink da0.2.24 preservados. Não houve OTA, reset, reativação de TX ou deploy pelo desenvolvimento.

- Corrige a identificação do próprio quadro quando o último byte chega ao montador depois do timeout elétrico de eco. O GPIO TX continua sendo liberado no prazo original de20ms. Eco tardio não vira prova elétrica ou resposta confirmada.
- Polling nativo não grava nem utiliza a antiga trava persistente `jguard`. Suspeita de MID, eco incompleto e timeout de um PID ficam em diagnósticos/contadores. O PID falho volta à sua própria cadência; os demais continuam. Ensaios manuais mantêm suas regras próprias.
- Mantém um único transmissor,500ms entre consultas, arbitragem/idle mínimo5ms, checksum, correlação MID/PID/tempo, timeout450ms, parada manual, suspensão para OTA e recuperação após perda real de RX. Não força TX em barramento ocupado.
- Aplica uma migração NVS única para117/MID140,118/MID140 e250/MID128 a10.000ms. Primeiras fases1s/4s/7s; mantém MID local escolhido e outras cadências.
- Reconfigura o perfil ativo após uma alteração salva do MID, terminando a transação em curso e aguardando RX saudável. Não retorna silenciosamente ao172.
- Preserva a reconciliação PID183/PID250 e acrescenta média consolidada somente com combustível e odômetro dos mesmos extremos; não adiciona estimativa ao combustível confirmado. Mantém a média encerrada enquanto o modo vazio continua consumindo. Referências SAE permanecem EM VALIDAÇÃO física.
- Mantém checkpoints v3/v4 para rollback e acrescenta pequeno slot A/B com CRC para os extremos da média. Não grava a configuração a cada consulta.
- Em pressão de RAM, `/api/state` entrega diagnóstico mínimo sem alocar árvore JSON; `/api/health` mantém heap, maior bloco e contadores J1708. HEALTH publica também heap atual/maior bloco.
- Acrescenta replay do boot602, cadência crítica, falhas isoladas, migração preservando MID, média/reboot e viagem virtual300km com três reinícios e duas horas offline. Sem envio bruto contínuo.

Não altera drivers/pinos, Link, layout dos aplicativos, partições, volume flash/ACK, dados históricos ou viagens na Cloud. A ingestão Cloud v26 ainda usa cálculo legado; sua migração financeira permanece separada desta entrega.
