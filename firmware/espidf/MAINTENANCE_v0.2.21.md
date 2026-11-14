# Core Rev.B.1 0.2.21 — manutenção da telemetria por Wi-Fi

Candidata de 14/11/2026 UTC, baseada na 0.2.20 (`521b1ec`) e investigação do
boot 559 (`db41c56`). ESP32-C6, 8 MB, ESP-IDF 5.5.2.
**Nenhuma OTA, limpeza, reset ou alteração foi executada na placa pelo agente.**

## Escopo e perda dos arquivos

Instalar esta BIN não chama a formatação. Boot, OTA, recuperação de Wi-Fi e
retry de armazenamento não autorizam limpeza. A operação só fica disponível
depois, mediante confirmação explícita no painel administrativo.

Recria somente o filesystem FAT de `storage`, offset 0x620000, tamanho 0x1e0000
(1.966.080 bytes). Recusa outro endereço/tamanho/tipo ou partição somente leitura.
É reinicialização lógica do filesystem; não é apagamento seguro de todos os bits.
São perdidos arquivos locais, inclusive não enviados, objetos desconhecidos e
arquivos danificados. Não se declara determinada a causa original da inconsistência.

Não apaga firmware, bootloader, partições OTA, NVS, identidade, credenciais,
vínculo, viagens e combustível. Altera somente a marca `tb_store/keep_through`
para 0, pois os arquivos anteriormente protegidos serão eliminados. ACK e reserva
de sequências não são zerados. Novas sequências continuam acima da reserva
persistente. O descarte autorizado não é contabilizado como ACK.

## Procedimento no painel

1. Estacione, mantenha alimentação estável e ignição ligada. A velocidade medida
   deve estar entre 0 e 0,1 km/h, com idade máxima de 5 segundos. Dados ausentes,
   antigos, futuros, negativos ou inválidos bloqueiam a operação. GPS não é exigido.
2. Abra `http://truckbox.local/` ou o IP atual (último informado: `192.168.1.251`).
   Entre como administrador e envie **`truckbox_core_0.2.21_ota.bin`** na atualização
   por Wi-Fi. Não envie ZIP, bootloader, partições ou imagem USB. Não atualize o Link.
3. Aguarde o reinício e pelo menos 20 segundos; a imagem precisa estar confirmada
   como VALID. A OTA não formata o armazenamento nem troca as credenciais.
4. Abra **Coleta e armazenamento → Manutenção da memória de telemetria** e marque
   a declaração de caminhão estacionado. A placa também confere a velocidade real.
5. Baixe a cópia da partição antes de limpar. Deve ter exatamente **1.966.080 bytes**;
   o painel recusa cópia incompleta. Guarde em particular: contém dados locais e
   wear levelling, não é uma imagem FAT diretamente montável.
6. Clique **Preparar confirmação de limpeza**. Gera desafio efêmero válido por
   2 minutos neste boot; nenhum arquivo é apagado nessa etapa.
7. Somente se aceitar a perda, marque o aceite, digite **`FORMATAR TBFLASH`**,
   clique **Apagar arquivos e recriar armazenamento** e confirme o diálogo final.
8. Mantenha alimentação e conexão. Não há reset automático após a manutenção.
   Se perder a resposta, consulte o diagnóstico antes de preparar outra autorização.

A OTA reinicia o Core. Os 64 itens atualmente apenas em RAM na 0.2.20 não têm
preservação garantida durante a atualização. Backup da partição não inclui RAM.
A nova manutenção não limpa explicitamente a fila RAM; contadores já salvos
na NVS são preservados. Não confundir essas garantias com recuperação dos descartes.

## Implementação e proteções

- `POST /api/storage/maintenance`: Bearer administrativo, ações `prepare`/`format`.
  Exige autorização, parada medida e declarada, aceite da perda, frase exata,
  UID/boot corretos e desafio aleatório de 128 bits. O desafio não sai no diagnóstico
  público e é consumido antes da primeira operação destrutiva. Não existe intenção
  de formatação persistida que possa repetir após reboot ou resposta HTTP perdida.
- Função nova `tb_capture_storage_stationary` somente lê velocidade/idade sob lock.
  Não muda aquisição, NVS ou controle J1708. Os parâmetros e controladores originais
  de recepção, PIDs, MIDs, cadências, arbitragem e proteções permanecem iguais.
- Compartilha a exclusão atômica da OTA e o mutex de writer/recuperação/batch/ACK.
  Não mantém FILE/DIR do outbox aberto durante recriação. Ofertas antigas são
  invalidadas; ACK continua exigindo um alvo real. Nenhuma confirmação é inventada.
- Volume registrado: `f_mount`/`f_mkfs`/`f_mount` no mesmo VFS/WL. Volume indisponível:
  API explícita de formatação do SDK, somente dentro dessa autorização. O boot normal
  continua com `format_if_mount_failed=false`. Formatação não apaga a NVS.
- Confere volume vazio e cria exclusivamente `TBPROBE.CHK`: escrita completa,
  `fflush`, `fsync`, fechamento, reabertura e comparação. Fica como evidência,
  ocupa um cluster, não é amostra e nunca vai para a Cloud.
- Recria o índice e libera o writer. Prova de volume não é telemetria confirmada:
  `durable_write_confirmed` só muda após gravar telemetria real. Só então os novos
  registros entram na oferta/ACK. Erros deixam etapa/código/errno e writer pausado;
  não há repetição automática da limpeza.
- `GET /api/storage/backup`: administrador, parada, exclusão OTA e mutex; lê somente
  a partição fixa, em blocos de 1 KiB. Libera recursos em OOM, erro de leitura/envio.

Durante backup/manutenção, TX pode pausar por `OTA_BUSY`/`OTA_PAUSED`, a proteção
existente usada para exclusão de escrita. A recepção passiva continua, a política
automática permanece persistida e aguarda rede saudável para retomar. Não requer
comando de rearmar. Não ativa microSD, FRAM ou 4G e não muda J1939 ou viagens na Cloud.

## Resultado esperado

- `storage.maintenance.phase`: `AWAITING_TELEMETRY`, depois `COMPLETE`.
- `maintenance.probe_durable=true`, `result=0`; em falha, guardar `stage`/`errno`.
- `storage.ready=true` e `durable_write_confirmed=true` após primeira gravação real.
- `committed_this_boot` aumentando; não inclui o arquivo de prova.
- `bridge.attempts`/`successful_acks` aumentando quando Link/Cloud responderem.
- `last_ack_seq` avançando somente com ACK real, sem ser zerado.

`maintenance.successes` confirma filesystem/prova/índice; não comprova upload.
Sem internet, os registros duráveis aguardam envio normalmente.

## Testes e memória

Suíte completa ASan/UBSan + 24 testes Python: PASS. FatFs original: FAT12 com
512 slots cheios ou LFN órfãs, preservação até confirmação, formatter real,
novo arquivo/sync/readback/remontagem e falha de escrita sem falso sucesso: PASS.
Handler/store reais: autenticação, perda/frase/UID/boot/desafio/expiração,
velocidade/OTA/mutex/alvo, falhas NVS/format/probe, replay, ACK/IDs monotônicos,
fila RAM retida e reboot sem repetir autorização: PASS.

Leitor real de velocidade: ausente/antiga/futura/movimento/NaN/negativa, sem mudar
NVS ou flags J1708: PASS. Backup real com fronteiras simuladas: bytes/limites,
alvo errado, OOM, movimento, leitura/envio e liberação do mutex: PASS.
Chromium/página real contra servidor local: ausência de limpeza automática,
backup, preparo, cancelamento, confirmação, erro sem retry, expiração/logout e
isolamento Core/Link: PASS; regressão completa do painel também PASS.

Duas horas offline simuladas com/sem UTC: 3.789 registros, 14 reinícios, zero
descartes por capacidade; 3.797 entregas com retries, 3.789 únicos e zero pendências
válidas após reconexão. Não é ensaio físico no caminhão.

Build ESP-IDF 5.5.2/C6/8 MB: PASS. Aplicação **1.570.960 bytes** no slot de
3.145.728. RAM estática **+80 bytes** sobre 0.2.20; scratch de formatação 4 KiB
e buffer de backup 1 KiB temporários. Sem aumento de filas/tarefas. Conferidos
39 arquivos: 38 byte a byte; `capture.c` só recebeu o leitor de parada. Evidências
em `test_evidence/v0221_*`.

Heap, duração, cortes reais de alimentação durante formatação e gravação/upload
no caminhão não foram medidos. Falha elétrica ou de mídia não é corrigida por
recriar FAT. Simulações não são validação elétrica de transmissão J1708.

## Reprodução e rollback

ESP-IDF 5.5.2: `bash tools/build_v020.sh core` e `sh tests/run_all.sh`.
Com Playwright/Chromium: `node tests/web/test_flow.cjs` e
`node tests/web/test_flash_maintenance.cjs`.

Rollback usa o processo OTA existente e mantém partições. Voltar à BIN anterior
**não restaura arquivos eliminados**. Antes de rollback manual, respeitar escoamento
da fila e instruções existentes; não apagar pendências para liberar sua barreira.
