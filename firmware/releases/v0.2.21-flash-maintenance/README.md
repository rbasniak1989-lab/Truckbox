# Core Rev.B.1 0.2.21 — CANDIDATA de manutenção Wi-Fi

**Preparada, não instalada pelo agente. Instalar a BIN não inicia a limpeza.**

- [Procedimento, perdas, testes, memória e rollback](../../espidf/MAINTENANCE_v0.2.21.md).
- [Changelog](../../espidf/CHANGELOG_v0.2.21.md).
- [Aplicação OTA Core](truckbox_core_0.2.21_ota.bin).
- [ZIP completo com fontes, símbolos, ferramentas e relatório](Truckbox_Core_B1_0.2.21_MANUTENCAO_CANDIDATA.zip).
- [Diff incremental](SOURCE_CHANGES.diff) e [verificação dos artefatos](PACKAGE_VERIFICATION.json).

Fontes compilados: `4c5996d2b8c529bc79364a67a87900c9be459b1f`.
ESP-IDF 5.5.2 / ESP32-C6 / 8 MB. Aplicação 1.570.960 bytes. RAM estática +80 bytes;
sem novas filas ou tarefas. Build, testes host/FatFs/navegador passaram.
Verificação de 188 hashes e 494 arquivos dos fontes. Teste físico pendente.

A manutenção recria somente a partição storage, com administrador, parada medida,
aceite da perda dos arquivos, frase exata, UID/boot e desafio de uso único.
Boot, OTA e recuperação de conexão não formatam automaticamente. Há backup
privado da partição antes da limpeza. NVS não é apagada; somente a marca de
proteção dos arquivos descartados é reiniciada durante a manutenção autorizada.
Viagens, combustível, identidade, vínculo e configurações persistentes são mantidos.
A política automática J1708 permanece; durante manutenção pode pausar pela
proteção de OTA e retomar após rede saudável. A recepção passiva é preservada.

Para OTA usar apenas o `.bin` de aplicação. Não enviar ZIP, bootloader,
partições, OTA data ou imagem USB pelo painel. Não atualizar o Link.
A OTA reinicia: registros ainda só em RAM na versão em campo não têm
preservação garantida. Backup do filesystem não inclui essa RAM.

SHA-256 OTA:
`8542804d815bfaba5a1cbfd512a3147730dc8c4c9e561a91a95b2e404acdf072`.
SHA-256 ZIP:
`326bb6b4281b6663eb2568738b1dc8fe03f0f937e7870194e6b2885e7b59102b`.

O SHA256SUMS completo refere-se aos arquivos extraídos do ZIP.
A prova local de escrita não confirma upload: verificar telemetria real persistida,
bridge e ACK após eventual operação autorizada. Falha de manutenção deixa pausa,
etapa e código; não repetir cegamente. Não foram executadas ações na placa.

Rollback de BIN não recupera arquivos eliminados pela manutenção.
