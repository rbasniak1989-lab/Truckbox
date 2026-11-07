# TruckBox Rev.B.1 v0.2.0 — VERSÃO DE TESTES

[**Baixar pacote ZIP completo**](./Truckbox_B1_firmware_0.2.0_TESTES.zip?raw=true)

ZIP: **3,704,491 bytes**. SHA256:
`9ce0a63d0836f9edd6aa68f245ccd436161fa6bec3215b25c2e144cc511cbc4c`.

Contém fonte ESP-IDF5.5.2 completo, Core e Link0.2.0, bootloaders, partições,
dados OTA, scripts Windows, guia de atualização/recuperação, testes e inventários.
Extraia o ZIP antes de usar; a fonte mantém os módulos irmãos em `firmware/`.

**J1939:**55/76 itens com fórmula/enum/grupo,15parciais/brutos,6pendentes/indisponíveis.
**J1587:**48PIDs passivos e17solicitáveis; TX desarmado sem MIDs presumidos.
Coleta/armazenamento sem GPS implementados e testados no host. Placa, microSD,
forma de onda/arbitragem, OTA e comunicação veicular reais ainda requerem ensaio.

Leia [relatório](./TEST_RESULTS.md), [pendências](../../espidf/PENDING.md),
[atualização/recuperação](../../espidf/UPGRADE_RECOVERY.md) e
[testes ativos](../../espidf/ACTIVE_J1708.md).

A Cloud atual precisa da [correção local revisável](../../espidf/cloud/README.md)
antes de consumir RAW/DECODED pelo gateway antigo. Nenhuma implantação foi feita.
Os Logs Mãe completos não foram reprocessados: seus ZIPs originais ainda estão ausentes.

Fonte compilado: commit `936f9f91b539287852c2ebf85e94f14583143d74`.
Release funcional v0.1.0-b1 intacta. **Sem merge e sem gravação automática.**
