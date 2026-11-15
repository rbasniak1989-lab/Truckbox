# Atualização manual — Core Rev.B.1 0.2.26 candidata

Nada é instalado automaticamente. O arquivo `truckbox_core_0.2.26_ota.bin` é exclusivamente do Core ESP32-C6 Rev.B.1. O firmware Link continua instalado como está.

1. Compare o SHA256 da BIN com `SHA256SUMS.txt` da release.
2. Quando decidir realizar a atualização, use o painel atual do Core (`http://truckbox.local` ou `http://192.168.4.1` conectado ao AP). Entre com a chave administrativa já existente e abra a atualização OTA do Core.
3. Selecione somente `truckbox_core_0.2.26_ota.bin`, envie e mantenha alimentação estável até o reboot e a confirmação da aplicação. Não use o ZIP, APK, bootloader ou tabela de partições nesse campo.
4. Depois consulte `/api/live`: versão 0.2.26, RAM, contadores J1708 e `network.internet_sharing`. `/api/state?compact=1` e os endpoints paginados continuam disponíveis. O polling nativo preserva a política/MID salvos e espera condições operacionais; atualizar não apaga uma desativação explícita do operador.
5. A multimídia pode permanecer conectada a `Truckbox-Core-190C`. Quando o Core estiver conectado à Starlink e o diagnóstico indicar NAPT ativo, a internet poderá atravessar o AP. `internet_verified=false` lembra que a BIN não executa um teste externo. Atualize a aplicação separadamente para `TruckBox-Multimedia-0.3.1-core-live-debug.apk` para usar a leitura limitada e a exibição de códigos PID194.

A aplicação Android mantém `br.com.truckbox.multimedia` e versãoCode4. O Android só aceita atualização sobre a instalação anterior se a assinatura coincidir. APK de CI é debug e não comprova que tem a chave da instalação em campo. Se o Android recusar por assinatura, não desinstale nem apague dados: será necessário compilar com a chave da instalação anterior. A candidata não altera automaticamente nenhum aplicativo.

Credenciais, identidade, NVS, partições OTA, histórico, fila e contadores de viagem/combustível não são apagados. Os arquivos USB existentes no ZIP são para recuperação manual; não use erase_flash nem restauração de fábrica para esta atualização. Não habilite microSD/FRAM/SIM por causa desta release.

## Rollback

O mecanismo de rollback da aplicação permanece habilitado e as partições não foram alteradas. Se a nova aplicação não confirmar a inicialização, o bootloader pode retornar à aplicação anterior conforme seu estado OTA. Isso não recupera dados que um operador apague separadamente.

O ZIP inclui `rollback/truckbox_core_0.2.24_ota.bin`, obtido do artefato auditado do PR27. SHA256: `5d3d3bc914d1383d90af386044d53a645c03f6c4f020f5ec57def6e197b2374d`. É compatível com a tabela de partições e pode ser enviado manualmente pelo mesmo campo OTA; mantém dados/NVS, mas reintroduz o detector antigo de MID/eco e suas limitações. A 0.2.25 também foi preservada no repositório, porém sua BIN publicada tem o defeito de compartilhamento de internet documentado.

Os símbolos exatos da nova BIN ficam no pacote para investigação de panic/watchdog. Em caso de erro, consulte o diagnóstico compacto em vez de limpar a flash. Esta entrega não executa OTA, rollback, reset ou comando na placa.
