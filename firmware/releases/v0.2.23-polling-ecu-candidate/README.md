# TruckBox Core Rev.B.1 0.2.23 — CANDIDATA

Somente Core. Nenhuma OTA, reset, ativação ou deploy Cloud executado.

- [Pacote completo](Truckbox_Core_B1_0.2.23_POLLING_ECU_CANDIDATA.zip) — 8,619,195 bytes; fontes, aplicação, bootloader/partições, ferramentas Windows e símbolos no ZIP.
- [Aplicação OTA](truckbox_core_0.2.23_ota.bin) — 1,583,360 bytes, ESP32-C6/8MB, IDF5.5.2.
- [Fonte completa](truckbox_firmware_source.zip).
- [Relatório](../../espidf/RELATORIO_v0.2.23.md), [atualização/rollback](../../espidf/ATUALIZACAO_v0.2.23.md) e [diff](SOURCE_CHANGES.patch).
- [Verificação](PACKAGE_VERIFICATION.json); origem limpa `377d1e4a9009658f1ccae4d09994accba38c5184`.

SHA256 OTA: `7fa1fdc07230d7e48a13bd42576d95ea06d02e9402f1523938a583f036e01df5`.

SHA256 ZIP: `13bb5ca4d2079d68299987939dc0479715971f8c27a2eb1fcd7fd347d60e67aa`.

O falso conflito de eco e a perda da política de polling foram corrigidos. O consumo é integrado por PID183 e reconciliado por PID250 sem soma duplicada. Proteções e bloqueios técnicos permanecem. Testes de software e duas horas offline passaram; validação em placa ainda não executada. O ajuste da Cloud está preparado para revisão e não foi implantado: as projeções atuais da Cloud ainda podem sobrescrever o combustível do Core. O PANIC do boot564 segue sem causa específica até análise do dump.

A aplicação OTA não altera NVS nem partições. Não enviar ZIP, bootloader ou partições pelo formulário OTA. O Link permanece na versão instalada. O pacote contém script de recuperação Windows, que não deve ser executado automaticamente.
