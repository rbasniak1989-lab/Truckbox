# Core Rev.B1 0.2.18 — CANDIDATA, aguardando revisão

Correção incremental da 0.2.17, comparada com a 0.2.16 funcional. **Nenhuma OTA, reativação de requisições, reset ou alteração de configuração em produção foi realizada.** Link, NVS, credenciais, partições, armazenamento/ACK, leitura passiva e viagens são preservados.

- [Relatório de causas, comparação de RAM, testes e limites](../../espidf/AUDITORIA_v0.2.18.md).
- [PR #20 — código e diff](https://github.com/rbasniak1989-lab/Truckbox/pull/20).
- [ZIP completo](Truckbox_Core_B1_0.2.18_RAM_ECHO_CANDIDATA.zip), com fontes, símbolos, aplicação, recuperação manual e relatórios.
- [Aplicação OTA do Core](truckbox_core_0.2.18_ota.bin), **1.547.200 bytes**, fornecida para revisão; não é instrução de instalação.
- [Fontes](truckbox_firmware_source.zip), [manifesto](manifest.json), [proveniência](SOURCE_PROVENANCE.json) e [SHA256](SHA256SUMS.txt).

Fonte embarcado: `91727f64f4cc86191b63b8fae84087625574e140`, árvore limpa no empacotamento. Build real ESP-IDF **5.5.2 / ESP32-C6** e verificações de hash aprovados. A tabela de partições é a mesma; não atualizar partições por OTA.

## Resultados

- [CI Core, suíte host, painel e testes Cloud](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43133873975): **PASS**. UBSan em modo de falha, incluindo a correção do acesso ao histórico de 17 entradas a partir das 27 estatísticas.
- [CI Android](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43133873807): **PASS**; aplicativo sem alteração nesta correção.
- Diagnóstico compacto por padrão; saúde sem alocação JSON com boot e indicadores de memória. Páginas de sensores pequenas e construção JSON fora do mutex RX/TX.
- Sob orçamento host de 41.596 bytes, o diagnóstico completo atingiu 41.577 bytes e ficou incompleto; o compacto atingiu 22.894 bytes, **incluindo** buffers simulados de upload de 8.192 e checkpoint de 3.232 bytes. Duzentas consultas sem vazamento nem alocação dentro do mutex. Não são medições do heap físico do C6.
- RAM estática: **210.084 bytes**, igual à 0.2.17 e 6.560 acima da 0.2.16. O objetivo desta correção é reduzir picos transitórios, mantendo as informações implementadas.
- PID439: defeito de separação do eco de seis bytes reproduzido no código anterior; corrigido em seis fragmentações com resposta adjacente. Conflito verdadeiro do MID172 ainda bloqueia TX e a recepção passiva continua.
- PID153: preservados timeout de 450 ms e retry limitado. Cloud registrou duas requisições, uma resposta candidata e um timeout; a candidata teve 26 ms. Não há prova da causa elétrica da primeira ausência.
- Duas horas offline, recuperação e ACK/sincronização continuam passando, sem duplicar linhas. Não houve replay integral de ZIPs NEXIQ/Logs Mãe indisponíveis.

O quadro RX gerador do conflito no boot308 não foi gravado no firmware antigo. O defeito de software está comprovado em replay; a atribuição elétrica exata do incidente continua indeterminada. Novos conflitos passam a registrar evidência limitada de RX/TX, sem tráfego bruto contínuo.

## Antes de qualquer atualização

Na 0.2.17 atual, `/api/state?compact=1` e `/api/health` já podem ser consultados sem reativar o polling. O mínimo histórico de RAM não é zerado por essa consulta.

Esta candidata precisa de revisão e autorização explícita separada antes de uma OTA manual, estacionado e com requisições mantidas desabilitadas. O heap mínimo/maior bloco e a stack ainda precisam de medição no hardware após uma atualização autorizada. Não se declarou sucesso elétrico por eco ou por testes de software.

Para rollback, a aplicação 0.2.16 está preservada. Não apagar NVS, storage, credenciais ou viagens. O procedimento detalhado está no relatório; nenhum procedimento foi executado no dispositivo.
