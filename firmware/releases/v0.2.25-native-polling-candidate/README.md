# TruckBox Core Rev.B.1 0.2.25 — CANDIDATA

Aplicação do Core ESP32-C6/8MB. **Não instalada pelo desenvolvimento.** Link, NVS, partições, arquivos flash e viagens Cloud não foram alterados.

- [BIN OTA do Core](truckbox_core_0.2.25_ota.bin)
- [ZIP completo: fontes, binários, símbolos e ferramentas](Truckbox_Core_B1_0.2.25_POLLING_NATIVO_CANDIDATA.zip)
- [Relatório da auditoria/testes](RELATORIO_v0.2.25.md)
- [Instruções OTA e rollback](ATUALIZACAO_v0.2.25.md)
- [Changelog](CHANGELOG_v0.2.25.md)
- [Diff completo desde a fonte0.2.24](changes-0.2.24-to-0.2.25.patch)
- [Verificação independente do pacote](verification.json)

Fonte compilada limpa: `7e089d6739529ae6f96b4192cc5f5228c70a6952`, baseada na PR27 `a9cd6bddfa6c1d176a7f8f9a8b46855dcd7ab3aa`, cujo ELF coincide com o boot602. ESP-IDF5.5.2. A imagem tem1.591.584bytes, checksum/hash internos válidos e cabe no slot OTA de3MiB. Não gravar bootloader/partições na atualização OTA.

SHA256 OTA: `38b0dbded81d0656cb5de3427d18435f1054e112fe6bfba436f6656d92d8e77d`.

SHA256 ZIP: `c79cb6a88f577fafdf27421f4a1830ad2432134cce54335fcb250eb798df31d6` (9.608.298bytes).

Rollback incluído: [Core0.2.24 verificado](rollback/truckbox_core_0.2.24_ota.bin), SHA256 `5d3d3bc914d1383d90af386044d53a645c03f6c4f020f5ec57def6e197b2374d`. Contém o defeito antigo e deve ser usado somente como retorno deliberado autorizado.

Testes de software: suíte completa,30testes Python, ASan/UBSan, navegador com mocks, replay real do boot602 e viagem virtual300km com3reinícios/2h offline. Não comprovam transmissão elétrica ou funcionamento comercial em campo. A margem do teste integral2h foi15.646B; evidências/backlog/eventos extras reduzem a autonomia. A Cloud ainda usa ingestão financeira v26, que precisa de revisão separada para adotar os totais ECU nas projeções; não houve deploy ou recálculo histórico.
