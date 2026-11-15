# TruckBox Core Rev.B.1 — candidata 0.2.26

Fontes congeladas em `27449ab33f84b2ac0b3414f811d3937a2e66a360`. ESP32-C6, flash 8 MB, ESP-IDF5.5.2. PR30: https://github.com/rbasniak1989-lab/Truckbox/pull/30. Sem instalação, merge ou deploy automático.

Arquivos:

- `truckbox_core_0.2.26_ota.bin`: aplicação OTA do Core, 1.601.488 B.
- `Truckbox_Core_B1_0.2.26_SHARING_LIVE_CANDIDATA.zip`: firmware completo, fontes (577 arquivos conferidos contra o commit), símbolos, configuração efetiva, bootloader, partições, ferramentas Windows, evidências e rollback auditado0.2.24. Não contém nova BIN Link.
- `TruckBox-Multimedia-0.3.1-core-live-debug.apk`: aplicação separada para leitura limitada e falhas PID194; versãoCode4, mesmo applicationId. A assinatura debug precisa coincidir com a instalação anterior para aceitar uma atualização. Não desinstale/apague dados se o Android recusar por assinatura; obtenha a chave original de compilação.
- `RELATORIO_v0.2.26.md`, `CHANGELOG_v0.2.26.md`, `ATUALIZACAO_v0.2.26.md`: causa, mudanças, testes, limites e atualização/rollback.
- `verification.json`, `android-build.json`, `android-unit-tests/`, `SHA256SUMS.txt`: identidade, integridade e resultados auditáveis.
- `changes-0.2.25-to-0.2.26.patch`: diff das fontes/evidências antes desta publicação.

BIN SHA256: `d396d4481645c2fe3752b5394960323264c2fc49cdfc7f2bb20a594c26a95375`.

A BIN 0.2.25 publicada estava com IP_FORWARD/NAPT desabilitados. Esta candidata corrige a configuração efetiva e verifica a marca do recurso dentro da BIN. Marcha PID163, torque PID93 e pressão PID94 são expostos via `/api/live`; percentual de torque é derivado da referência histórica de2643Nm, em validação. PID94 passa a10s. TCP bruto35000 fica desativado em J1708_ONLY; são removidos12.544B de reserva dinâmica mais2.032B de DRAM estática, sem reduzir os128 sensores. Ganho de heap em campo permanece não medido.

Builds Core/Android e testes passaram; 8 testes da multimídia mais2 do Motorista, duas horas offline e300km simulados estão documentados. Internet real atravessando a placa, heap em viagem e apresentação na multimídia instalada ainda dependem de observação física. A Cloud não foi alterada e mantém a projeção financeira legada; atualizar firmware não migra seu histórico.

Auditoria local dos bytes efetivamente entregues:

```sh
python3 firmware/espidf/tools/verify_shipped_core.py \
  firmware/releases/v0.2.26-sharing-live-candidate/Truckbox_Core_B1_0.2.26_SHARING_LIVE_CANDIDATA.zip \
  firmware/releases/v0.2.26-sharing-live-candidate/truckbox_core_0.2.26_ota.bin
```

CI das fontes congeladas: [Core](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43710526271) e [Android](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43710526273). A publicação dispara outra execução que confere a BIN/ZIP publicados separadamente do build limpo. O resultado final fica no PR30.
