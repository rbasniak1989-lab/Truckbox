# TruckBox Core Rev.B.1 0.2.14 — candidata

Acrescenta **Validação supervisionada em campo**, alternativa quando não se pode conectar NEXIQ e TruckBox simultaneamente. Mantém a validação externa independente.

O caminho novo exige autorização estacionado, velocidade zero recente, comparação passiva antes/depois, três rodadas dos 17 PIDs, janelas sem transmissão e confirmação separada do operador. Resultado satisfatório permite autorizar o agendamento de viagem; resultado inconclusivo ou falha bloqueia a liberação. É evidência **operacional indireta**, sem comprovação elétrica externa. Não preenche campos de captura externa nem valida automaticamente sensores ou contadores.

- **truckbox_core_0.2.14_ota.bin**: aplicação para OTA manual somente do Core.
- **Truckbox_Core_B1_0.2.14_CANDIDATA.zip**: pacote completo com fontes, aplicação, bootloader, partições, scripts Windows, símbolos e relatórios.
- **truckbox_firmware_source.zip**: fontes ESP-IDF e módulos irmãos completos.

[Guia e critérios](../../espidf/REV_B1_0.2.14_CANDIDATA.md) · [Testes e RAM](../../espidf/TESTES_v0.2.14.md) · [Changelog](../../espidf/CHANGELOG_v0.2.14.md). Hashes em `DOWNLOADS.json` e `SHA256SUMS.txt`.

Compilado com ESP-IDF 5.5.2 para ESP32-C6/8 MB. Suíte geral, controlador supervisionado, proteção contra coincidências passivas e 20 verificações no navegador passaram. Duas horas offline simuladas com metadados do modo indireto: 5.904 registros por cenário, máximo 1.732.149/1.818.624 B e sincronização sem duplicação no servidor simulado. RAM adicional conhecida: 1.552 B. Falta executar este ensaio na placa real e observar a operação prolongada.

O Link instalado permanece compatível; não há binário Link neste pacote. Cadências, NVS, partições, codec local e ACK preservados. A 0.2.13 permanece arquivada e permite rollback pela aplicação OTA. Nenhuma placa foi gravada ou atualizada automaticamente, nenhum merge e nenhuma alteração de viagens da Cloud.
