# TruckBox Core Rev.B.1 — 0.2.16 produção

Firmware implementado para o ESP32-C6/8 MB do Core. **Não instalado automaticamente.** Base real 0.2.15 preservada; Link, partições e arquivos de armazenamento não foram alterados. Nenhum deploy, reset, transmissão real ou merge nesta entrega.

Fonte do pacote: commit `47c25a55bc2a40d3389007b0788af09f8d33bf1c`, branch `feature/revb1-v0216-auto-j1708`. A proveniência registra o fonte limpo usado na compilação; commits posteriores apenas acrescentam os artefatos e esta página.

## Downloads

| Arquivo | Conteúdo |
|---|---|
| [Truckbox_Core_B1_0.2.16_PRODUCAO.zip](Truckbox_Core_B1_0.2.16_PRODUCAO.zip) | Pacote completo: aplicação, fontes, símbolos, bootloader/partições para recuperação e ferramentas Windows |
| [truckbox_core_0.2.16_ota.bin](truckbox_core_0.2.16_ota.bin) | **Somente aplicação OTA do Core**, 1.517.168 bytes |
| [truckbox_firmware_source.zip](truckbox_firmware_source.zip) | Projeto ESP-IDF completo, módulos, documentação e testes |
| [changes.patch](changes.patch) | Diff de código/documentação/testes em relação à base 0.2.15 |
| [SHA256SUMS.txt](SHA256SUMS.txt) | Integridade dos arquivos desta pasta |
| [manifest.json](manifest.json) / [SOURCE_PROVENANCE.json](SOURCE_PROVENANCE.json) | Identidade dos binários, versões, base e fonte utilizado |

SHA256 da aplicação OTA: `14ceb8c41afdb42417001ad40fd09cbb86674dac67fc3b66c95266a400b02122`.

SHA256 do pacote completo: `3b0563a0119b5894f53c9bb8e697282698d97f794c004c76232ea820fb8e3d4e`.

## Funcionamento

A configuração automática migra uma vez para a chave NVS `tb_cfg/v216`. O dispositivo `tb-c6-d40592b9190c` conserva o perfil operacional conhecido, MID local 172 e MIDs de destino existentes. Após ligar, o polling inicia depois de três janelas recentes saudáveis (normalmente 30–40 s), sem aplicativo, Wi-Fi, internet ou GPS. Opt-out administrativo permanece salvo; uma chave inválida ou perfil ausente bloqueiam TX. Não há escrita NVS por ciclo.

Os 17 PIDs usam uma fila e um pedido por vez, máximo de dois inícios por segundo. Durante a janela de arrancada, 31/33 têm prioridade e meta de 2 s. Timeouts, eco, arbitragem e proteções de rede continuam ativos; falhas recuperáveis suspendem TX e exigem espera e novas janelas saudáveis. Conflito de MID bloqueia persistentemente. Recepção passiva continua.

Frequências, formato dos dados, diagnóstico, limitações e procedimentos completos: [PRODUCAO_v0.2.16.md](../../espidf/PRODUCAO_v0.2.16.md). Resultados verificáveis: [TESTES_v0.2.16.md](../../espidf/TESTES_v0.2.16.md). [Tabela dos 17 PIDs/MIDs](../../espidf/sensors/J1708_PRODUCAO_v0.2.16.csv). [Changelog](../../espidf/CHANGELOG_v0.2.16.md).

## Verificação e limites

Compilado com ESP-IDF **5.5.2**, alvo ESP32-C6, flash 8 MB; aplicação dentro da partição OTA de 3 MiB. Suíte de testes C/ASan/UBSan e 24 testes Python aprovada; 22 cenários de navegador e ingestão Cloud com mocks aprovados. Teste virtual de duas horas: 6.751 requisições/candidatas, 438.751 quadros processados e 3.256 registros sincronizados sem duplicação. Não é medição elétrica na placa nem comprovação causal de resposta. RAM estática: +944 B em relação à 0.2.15.

Uma segunda compilação limpa dos fontes extraídos deste ZIP também passou: [verificação de identidade e integridade](BUILD_FROM_SOURCE_ZIP.json) e [log completo](BUILD_FROM_SOURCE_ZIP.log). Os 133 arquivos com hash interno do pacote foram conferidos. Caminho e horário de build podem mudar o hash do ELF/aplicação; não se afirma reprodução binária idêntica.

Os dados proprietários permanecem EM VALIDAÇÃO. Eventos de embreagem são candidatos; não confirmam patinação ou trepidação mecânica. Perfil J1708_ONLY da base mantido; código J1939 preservado, sem resolver a ausência física de CAN. microSD, FRAM e SIM continuam desabilitados. A autonomia depende também do espaço ocupado por dados anteriores e arquivos isolados.

## Atualização e rollback

Atualização **manual**, com caminhão estacionado e alimentação estável, usando apenas o `.bin` OTA de aplicação no Core. Não apagar NVS/flash, não alterar partições e não atualizar o Link com este arquivo. As ferramentas USB do pacote são para recuperação consciente; não são necessárias para OTA normal. Após reiniciar, conferir versão e `j1708.requests.automatic` (habilitado, persistido, estado/motivo), recepção e contadores. O automático pode aguardar a rede saudável antes de `work_mode=true`.

Rollback manual para a aplicação 0.2.15 conserva NVS e armazenamento e retorna à política manual antiga. A 0.2.15 ignora a extensão `v216`; voltar à 0.2.16 recupera essa política. Bootloader mantém rollback habilitado. Ver os procedimentos de compatibilidade antes de retornar à 0.2.11 ou anterior.

Para compilar o ZIP de fontes: extrair, ativar ESP-IDF v5.5.2 e executar `bash firmware/espidf/tools/build_v020.sh core`. Para a suíte: `sh firmware/espidf/tests/run_all.sh`. Esses comandos não gravam o dispositivo.
