# Core 0.2.13 — candidata

- Corrige pico de uso de memória ao gerar o diagnóstico e preparar lotes para o Link.
- O painel pede um diagnóstico menor e consulta os 17 PIDs em páginas pequenas.
- Acrescenta consulta reduzida de RAM e do maior bloco livre, mesmo quando o diagnóstico completo não cabe.
- Corrige liberação de nós JSON em falhas de alocação e protege a ordem dos valores, idade e MID.
- Distingue armazenamento temporariamente ocupado de armazenamento com falha.
- Mantém os dados, configurações, coleta J1708, autorização de transmissão e compatibilidade com a Cloud da 0.2.12.

Somente aplicação Core. Nenhuma placa atualizada automaticamente. Teste na placa e validação elétrica das requisições ainda pendentes. Veja o guia `REV_B1_0.2.13_CANDIDATA.md`.
