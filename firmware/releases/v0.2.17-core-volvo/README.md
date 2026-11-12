# TruckBox Core Rev.B1 0.2.17 — CANDIDATA

Implementação incremental sobre a 0.2.16 funcional. Não instalada automaticamente. O Link permanece na versão existente. Sem merge, deploy Cloud, reset, alteração de configuração em produção ou comandos de apagamento/atuação em ECU.

- [Aplicação OTA do Core](truckbox_core_0.2.17_ota.bin): **1.545.104 bytes**.
- [Pacote completo](Truckbox_Core_B1_0.2.17_FINAL_CANDIDATA.zip): **8.175.365 bytes**, com aplicação, bootloader/partições para recuperação manual, fontes, símbolos, testes, ferramentas Windows e relatórios.
- [Fontes completos](truckbox_firmware_source.zip): ESP-IDF e módulos irmãos, Android, catálogo/serviço Cloud, scripts e dependências fixadas.
- [Manifesto](manifest.json), [SHA256](SHA256SUMS.txt) e [proveniência](SOURCE_PROVENANCE.json).
- [Guia e rollback](../../espidf/REV_B1_0.2.17_CANDIDATA.md), [testes/memória/tráfego](../../espidf/TESTES_v0.2.17.md), [inventário](../../j1708_17pid/INVENTARIO_v0.2.17.csv).
- [Motorista 0.6.2 debug](../../../app/releases/v0.6.2-driver-diagnostics/README.md), com instrução de preservação dos dados e limitação da assinatura de CI.

Código-fonte embarcado no pacote: commit **`ed24abe8dd5a6464d0444b56e8fd0a9e84d953c8`**, branch `feature/revb1-v0217-volvo-diagnostics`, árvore limpa no empacotamento. As versões funcionais anteriores permanecem preservadas. [PR #19](https://github.com/rbasniak1989-lab/Truckbox/pull/19).

## Resultado

Preserva as 17 consultas automáticas e acrescenta 10 candidatas na mesma fila, com até 2 inícios/s e proteções anteriores. Implementa PID194 múltipla/estados/contadores/desconhecidos e transporte192; contadores250/236/235 por boot, parada e viagem; I-Roll provável, VEB percentual e diferença de rotação com limites de idade. Catálogo Volvo inicial em português, com correspondências candidatas e fontes identificadas.

As consultas novas não estão declaradas como respondidas ou eletricamente validadas. Eco e pareamento continuam evidência indireta. Há divergência de aplicação OEM do SID18 descrita no relatório. O catálogo não confirma presença de água no diesel nem patinação mecânica.

## Verificação da entrega

- [Core + suíte host + painel + Cloud](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43125419084): **PASS**, código `ed24abe`.
- [Android, testes JVM e ambos os APKs](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43125419072): **PASS**, código `ed24abe`.
- Fonte do ZIP final extraído em diretório novo e recompilado com ESP-IDF **5.5.2** para **ESP32-C6**: **PASS, zero warnings**, mesma versão e tamanho de aplicação. Não se afirma identidade binária entre diretórios/datas diferentes de build. [Evidência](../../espidf/test_evidence/v0217_source_zip_build.json).
- Duas horas virtuais offline: **441.135 quadros**, **8.175 consultas/respostas candidatas simuladas**, **3.789 registros**, sincronização sem duplicar linhas e sem descarte por flash cheia. Pior ocupação **1.595.469 de 1.818.624 bytes**. Testa software com espaço livre inicial; não comprova duas horas adicionais em uma partição já ocupada por evidências antigas.
- Falta de memória: **258 pontos de falha de alocação** no diagnóstico, sem aceitar registros parciais. RAM estática adicional **6.560 bytes**; heap físico mínimo não medido nesta tarefa.

SHA256 da aplicação OTA:

```text
05d94388c43ce95f2ba9421ec88e7a64a73939cb1e5eebd3793155042157a2e7
```

SHA256 do pacote completo:

```text
493856b14873566e31cebc771ce26a8d95b889fda6f082ef6385ba2a97ee102e
```

## Pendências e uso

O serviço Cloud de consulta está em fonte e **não foi publicado**. A ingestão/ACK existentes seguem compatíveis; o novo histórico remoto depende desse deploy autorizado. A consulta local do aplicativo usa o Core e cache limitado.

Os ZIPs completos NEXIQ/Logs Mãe não estão disponíveis. Os testes usam bytes preservados de 29/10 e amostras Cloud reais de 10–11/11, além de cenários sintéticos identificados. Não foram medidos hardware, alimentação, novos PIDs em campo ou interface do aplicativo em telefone real.

Sem UTC atravessando um desligamento, a duração desse intervalo pode permanecer desconhecida. Reset/regressão/MID/saltos são detectados; substituir uma ECU por outra com o mesmo MID e contadores numericamente coerentes não é identificável apenas por esses sinais. Rollover não comprovado não gera delta inventado. A resolução dos contadores de combustível é aproximadamente 0,473 L; resultados por viagem permanecem com qualidade explícita.

Há histórico local de 32 códigos e cache Android limitado; as amostras brutas por mudança preservam códigos adicionais para a Cloud. Não há biblioteca OEM completa nem serviço de push remoto publicado. Notificações de novas ativações/mudanças são identificadas nos eventos; o aplicativo exibe os estados disponíveis.

Após aprovação e com o caminhão estacionado, usar somente o `.bin` de aplicação no fluxo OTA existente do Core. Não usar tabela de partições ou bootloader em uma atualização OTA. Em caso de regressão, reinstalar a aplicação 0.2.16 pelo mesmo fluxo, sem apagar NVS, credenciais, flash ou viagens. O procedimento detalhado e a recuperação USB manual estão no guia.
