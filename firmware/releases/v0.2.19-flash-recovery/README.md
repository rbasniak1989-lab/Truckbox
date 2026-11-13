# Core Rev.B.1 0.2.19 — CANDIDATA, aguardando aprovação

Fontes derivados da 0.2.18, sem merge ou gravação da placa. Somente Core.
Código compilado: `f13f0e7f422f048e29e8e00f036afb4063f17907`.

- [Relatório e pendências](../../espidf/AUDITORIA_v0.2.19.md).
- [Changelog](../../espidf/CHANGELOG_v0.2.19.md).
- ZIP completo: fontes, OTA, símbolos ELF/map, manifesto, ferramentas Windows e instruções.
- Para OTA autorizada usar exclusivamente `truckbox_core_0.2.19_ota.bin`.

A recuperação remonta o volume após falha de IO, registra etapa/errno e mantém
CRC, fsync, retenção do registro e ACK. Não formata, apaga pendências, muda
partições ou reativa TX. O aumento de RAM estática é de88 bytes, sem filas novas.

Build ESP-IDF5.5.2/ESP32-C6 PASS; suíte ASan/UBSan, FatFs real com disco RAM,
nove etapas de erro de escrita e duas horas offline simuladas PASS. Não são
validação elétrica ou teste da candidata instalada no caminhão.

A alimentação/EN ainda requer medição. Dois PANIC antigos ainda precisam de
dump/backtrace; o novo GET administrativo `/api/crash.bin` exporta a partição
sem apagá-la. Guardar o dump em privado. Para um dump da 0.2.18, os símbolos
correspondentes estão no [pacote anterior](../v0.2.18-ram-echo/).

Não atualizar em movimento ou com alimentação instável. OTA reinicia o Core;
os34 registros que o diagnóstico do boot553 mostrou somente em RAM não têm
preservação garantida durante reboot. AAPI antiga não exporta essa fila RAM.
Preservar flash/NVS e obter aprovação antes de qualquer intervenção.

Rollback e atualização estão descritos no relatório. A0.2.18 e sua release
permanecem intactas. Link, microSD, FRAM,4G, Cloud e viagens não foram alterados.
