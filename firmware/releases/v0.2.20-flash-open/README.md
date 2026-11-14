# TruckBox Core Rev.B.1 — 0.2.20 CANDIDATA

Correção incremental de armazenamento sobre a 0.2.19. Não instalada pelo agente.

- [Relatório: achados, limites da evidência, testes e atualização](../../espidf/FLASH_FIX_v0.2.20.md).
- [Changelog](../../espidf/CHANGELOG_v0.2.20.md).
- [Diff dos fontes](SOURCE_CHANGES.diff).
- [Verificação do pacote](PACKAGE_VERIFICATION.json).
- [Binário OTA Core](truckbox_core_0.2.20_ota.bin).
- [Pacote completo com fontes, símbolos, ferramentas e relatório](Truckbox_Core_B1_0.2.20_FLASH_OPEN_CANDIDATA.zip).

A 0.2.19 contava releituras como recuperação antes de confirmar uma escrita.
Isso foi corrigido. O caso de arquivo AM_RDO foi reproduzido e tratado sem
mudar o arquivo original; diretório cheio também foi reproduzido, com espaço
em clusters ainda disponível. **Não foi determinado qual desses casos,
lock ou outra recusa originou o errno 13 da placa.** A candidata mede o
FRESULT e atributos FAT que faltavam no diagnóstico anterior.

Fontes compilados: `bb0872ec8e70a87d444d0f1ba1abdbfff2943153`.
ESP-IDF 5.5.2, ESP32-C6, 8 MB. Core somente; manter o Link instalado.

Build e suíte local passaram. Foram conferidos 178 hashes do pacote,
477 arquivos dos fontes e 39 arquivos inalterados de captura/J1708, trips,
rede, configuração, protocolos e partições. RAM estática: +184 bytes.
O ensaio offline de duas horas é simulado, não um teste na placa real.

## Uso somente após aprovação de atualização

Usar somente o `.bin` de aplicação para OTA. Não aplicar partições,
bootloader, OTA data, NVS ou filesystem durante a OTA. Não há formatação
automática, reset de viagem ou ativação de hardware indisponível.
As requisições J1708 e a configuração automática persistente não foram alteradas.

SHA-256 da aplicação:
`927b863c5d3fc4f333633f4cffac2bc3490a124c9423f49893f51d8897130fb4`.

SHA-256 do pacote ZIP:
`fe7b42f6712b9d791634396bfcbc89d98211226819a3a93b215e350eaba5fb3e`.

O SHA256SUMS original é relativo ao conteúdo **extraído do ZIP completo**.
O manifesto também identifica individualmente os arquivos de aplicação/fontes.

A candidata preserva arquivos anteriores e nunca envia a fila RAM como se
fosse durável. Os 64 itens RAM da versão em campo não são exportáveis por
ela e não têm preservação garantida durante reset/OTA; os descartes anteriores
não podem ser reconstruídos. Não foi executado nenhum comando na placa.

Conferir após atualização autorizada: `ready`, `read_ready`,
`durable_write_confirmed`, `committed_this_boot`, `recovery_scans`,
`recovery_successes`, ACK e bridge. Se a falha continuar, guardar o
`last_io_failure` completo antes de tentar novamente. Após três falhas,
o writer pausa. O retry administrativo é limitado e não foi invocado pelo agente.

O procedimento de rollback permanece no pacote. A 0.2.19 não conhece a nova
marca de proteção dos arquivos existentes e aplica sua antiga reciclagem de
segmentos já ACKados; revisar esse limite e preservar cópia antes de rollback.
