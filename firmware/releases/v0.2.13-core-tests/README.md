# TruckBox Core Rev.B.1 0.2.13 — candidata

Corrige o uso de RAM ao abrir o diagnóstico/painel da 0.2.12 e protege a liberação de memória em erro. A coleta J1708, retenção dos registros e confirmação da Cloud permanecem compatíveis. Não é necessário atualizar o Link instalado.

- **truckbox_core_0.2.13_ota.bin**: aplicação para atualizar manualmente o **Core** pela página Wi-Fi autenticada.
- **Truckbox_Core_B1_0.2.13_CANDIDATA.zip**: pacote completo, aproximadamente 7,6 MB, com aplicação, fontes, bootloader, partições, scripts Windows, símbolos e relatórios.
- **truckbox_firmware_source.zip**: fontes completos ESP-IDF e módulos irmãos.

[Guia e rollback](../../espidf/REV_B1_0.2.13_CANDIDATA.md) · [Resultados dos testes e RAM](../../espidf/TESTES_v0.2.13.md) · [Changelog](../../espidf/CHANGELOG_v0.2.13.md). Confira os hashes em `DOWNLOADS.json` ou `SHA256SUMS.txt`.

Compilado para ESP32-C6, ESP-IDF 5.5.2, flash 8 MB e partições preservadas. Testes de falta de memória e navegador passaram; a suíte geral inclui dois cenários de duas horas offline simuladas, 5.904 registros por cenário e sincronização sem duplicação no servidor simulado. Falta observar esta imagem no Core real, inclusive RAM durante upload e validação elétrica das requisições.

A versão 0.2.12 permanece arquivada. Rollback para 0.2.12 usa sua aplicação OTA, com formato de armazenamento compatível. Para 0.2.11 ou anterior, é necessário pausar/sincronizar conforme o guia; não apagar pendências.

Nenhum dispositivo atualizado automaticamente. Nenhum merge nem alteração de viagens. Requisições continuam exigindo validação física e autorização explícita antes do modo de viagem.
