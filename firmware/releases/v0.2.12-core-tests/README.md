# TruckBox Core Rev.B.1 0.2.12 — candidata

Base0.2.11 preservada. Pacote completo: **Truckbox_Core_B1_0.2.12_CANDIDATA.zip** (aproximadamente7,5 MB). Contém fontes, OTA Core, bootloader, partições, scripts Windows, símbolos ELF/mapa, documentação e evidências de testes. O Link instalado é mantido; não há binário Link neste pacote.

O arquivo **truckbox_core_0.2.12_ota.bin** é somente a aplicação para OTA do Core. **truckbox_firmware_source.zip** também está disponível separadamente. Verificar os hashes em DOWNLOADS.json/SHA256SUMS.txt.

[Guia, procedimentos e pendências](../../espidf/REV_B1_0.2.12_CANDIDATA.md) · [Memória](../../espidf/MEMORIA_v0.2.12.md) · [Changelog](../../espidf/CHANGELOG_v0.2.12.md).

Testes locais: suíte existente e adicionais passaram, incluindo dois cenários de2 h offline simuladas (5.904 registros cada, sem perdas ou duplicações no servidor simulado). O pior ocupou1.663.617 de1.818.624 bytes de orçamento. Economia conhecida de RAM:27.472 bytes. A autonomia depende do espaço inicial; evidências de corrupção preservadas também ocupam flash.

Validação elétrica da TX, duas horas na placa real e envio Core → Link → Supabase desta imagem estão pendentes. Requisições não começam automaticamente: teste único estacionado, confirmação externa e autorização explícita antes do modo de viagem. Não apagar pendências para fazer rollback; seguir o guia para pausar gravação e sincronizar primeiro.

Nenhum dispositivo foi atualizado ou gravado automaticamente. Não houve merge nem alteração de viagens da Cloud.
