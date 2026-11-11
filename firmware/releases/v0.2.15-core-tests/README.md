# Core Rev.B.1 0.2.15 — CANDIDATA PARA REVISÃO

Não instalada. Não houve OTA, reset, alteração de configuração, ativação de TX ou deploy. Base 0.2.14 preservada; Link e viagens da Cloud não foram alterados. A aplicação candidata requer aprovação antes de qualquer instalação ou teste no veículo.

Fonte: commit `31f0b482e37be3f6bd945a03c66b0b78894c0ab4`, branch `fix/revb1-v0215-j1708-guard`. ESP32-C6, ESP-IDF 5.5.2, flash 8 MB. Não muda a tabela de partições. A versão funcional v0.1.0-b1 está preservada.

| Download | Conteúdo |
|---|---|
| [Pacote completo](https://raw.githubusercontent.com/rbasniak1989-lab/Truckbox/fix/revb1-v0215-j1708-guard/firmware/releases/v0.2.15-core-tests/Truckbox_Core_B1_0.2.15_CANDIDATA.zip) | Fontes, Core OTA, bootloader/partições para recuperação, ELF/map, scripts Windows e documentação; 7.756.057 bytes |
| [Aplicação OTA do Core](https://raw.githubusercontent.com/rbasniak1989-lab/Truckbox/fix/revb1-v0215-j1708-guard/firmware/releases/v0.2.15-core-tests/truckbox_core_0.2.15_ota.bin) | 1.500.416 bytes, somente Core, não gravada |
| [Fontes completos](truckbox_firmware_source.zip) | `firmware/espidf`, J1708 e J1939, testes, referências e proveniência |
| [Diff das alterações](changes.patch) | Base 94cd01a → fonte 31f0b48, sem binários |
| [SHA256](SHA256SUMS.txt) | Conferência dos arquivos publicados |

OTA SHA256: `3b0a00f707932bc5ab6a7e4ecdd1c40fb9083588a38875e4d717e12c845f4fcc`.

ZIP completo SHA256: `a8121de219592977ef247693b0e42bde0e943b7829e8ecdac6040c62d90816c5`.

Leia [a auditoria com a condição antiga e as evidências](../../espidf/REV_B1_0.2.15_AUDITORIA.md), [testes e limites](../../espidf/TESTES_v0.2.15.md) e [changelog](../../espidf/CHANGELOG_v0.2.15.md). As 17 cadências/MIDs não mudam. Respostas permanecem candidatas e a validação operacional permanece indireta.

Uma única janela com inválidos ou taxa diferente do ensaio antigo podia interromper a 0.2.14. O evento repetia dados históricos sem guardar a janela da decisão. O ramo real do incidente das 20:02:34 e a causa elétrica permanecem desconhecidos. A candidata usa medições recentes separadas, persistência e diagnóstico explícito, preservando as proteções de TX.

Todos os testes host passaram, incluindo A–J e duas horas offline simuladas; 21 cenários de navegador e ingestão Cloud local mock passaram; Core compilado no hardware alvo. Acréscimo conhecido de RAM: 2.992 B; heap/pilha reais ainda pendentes. Testes locais não validam eletricamente a placa.

Após aprovação, executar primeiro ensaio estacionado e explicitamente autorizado. Nenhuma configuração deste pacote inicia trabalho ativo automaticamente. Rollback manual para 0.2.14/0.2.13/0.2.12 preserva formato/partições/NVS; 0.2.14 restaura também a proteção antiga. Consultar o procedimento detalhado antes de versões anteriores. Não usar scripts USB que gravem partições como substituto de atualização OTA de aplicação.
