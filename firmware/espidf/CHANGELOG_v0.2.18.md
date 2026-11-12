# 0.2.18 — candidata RAM/eco, não instalada

- Diagnóstico compacto por padrão; modo completo só é tentado com orçamento de RAM. Saúde e indicadores de memória permanecem acessíveis mesmo sem alocação JSON.
- Construção de JSON fora do mutex de captura/TX. Sensores em páginas pequenas, sem alocação contígua do snapshot.
- Corrige leitura fora dos limites do histórico de 17 PIDs ao exibir as 27 estatísticas.
- Corrige separação do eco de 6 bytes da PID439. Proteção contra MID local realmente conflitante permanece ativa.
- Acrescenta evidência bruta limitada em conflito/timeout, na fila existente.
- Mantém PID153 com 450 ms e retry limitado; não desabilita proteção para mascarar timeout.
- Sanitizador passa a reprovar a suíte ao encontrar comportamento indefinido.
- Mantém aquisição passiva,27 PIDs/cadências, armazenamento, ACK, Wi-Fi, identidade, NVS, partições, viagens, J1939 e rollback.

Comparação com 0.2.16/0.2.17, causas comprovadas, hipóteses, testes e limites: [AUDITORIA_v0.2.18.md](AUDITORIA_v0.2.18.md).
