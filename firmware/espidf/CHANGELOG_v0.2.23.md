# 0.2.23 — candidata: polling permanente e combustível reconciliado

- Corrige o falso conflito causado pelo eco local que chegava junto da resposta após 30 ms.
- Uma proteção técnica não grava mais o polling como desativado pelo operador. Configuração desejada e bloqueio de segurança ficam separados na NVS.
- Migração única da política de produção, supervisão do scheduler e da tarefa TX, espera por rede saudável e recuperação com backoff. Mantém bloqueio por MID realmente observado, arbitragem, eco, colisões e limites existentes.
- Mantém os 17 PIDs e as 10 candidatas já existentes, com os mesmos MIDs e frequências persistidas. PID250 continua com referência inicial de 300 segundos.
- PID183 integra o consumo; PID250 substitui o provisório do intervalo pelo delta da ECU. Correções positivas e negativas não duplicam litros.
- Checkpoint A/B v4 com CRC, âncoras e referências de operação; conserva chaves legadas para rollback. Fronteiras e ajustes recebem registros auditáveis.
- Diagnóstico compacto, timestamps efetivos de envio UART/resposta candidata e razão do último bloqueio. Guarda informação compacta de dump retido após PANIC, sem atribuir sua origem a um boot sem evidência.
- Fontes e proposta de integração da Cloud prontas para revisão. Nenhum deploy, recálculo histórico ou OTA foi executado.

Relatório: [RELATORIO_v0.2.23.md](RELATORIO_v0.2.23.md). Atualização e retorno: [ATUALIZACAO_v0.2.23.md](ATUALIZACAO_v0.2.23.md).
