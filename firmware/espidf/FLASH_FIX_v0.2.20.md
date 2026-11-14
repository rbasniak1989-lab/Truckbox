# Core Rev.B.1 0.2.20 — OPEN/EACCES e recuperação durável

Data: 14/11/2026 UTC. Branch `fix/revb1-v0220-flash-open`.
Base real: release 0.2.19, commit `c1165259d279cb4188574445a0ba89059a87b164`,
fontes compilados da base `f13f0e7f422f048e29e8e00f036afb4063f17907`.
ESP-IDF 5.5.2/ESP32-C6/8 MB. Candidata de software; não instalada pelo agente.

## Conclusão e limite da evidência

Foram corrigidos defeitos comprovados de seleção do arquivo e de contabilização
da recuperação. O caso de arquivo FAT somente leitura foi reproduzido com o
FatFs real e resolvido em teste, preservando bytes, atributos, nome e IDs antigos.

**Ainda não é possível determinar a causa específica do EACCES na placa só
com os campos da 0.2.19 fornecidos.** Dois cenários diferentes reproduzem a
recusa com espaço livre e remontagem bem-sucedida: arquivo somente leitura e
diretório FAT sem entrada disponível. Não se afirma que o diretório ou o
atributo da placa foi medido neste trabalho. A 0.2.19 não registrava o FRESULT
original ou os atributos FAT; esta versão passa a medi-los.

O diagnóstico de entrada é o trecho enviado pelo operador: OPEN/errno 13,
segmento 1, sequência 462617, 0/326 bytes, 24 tentativas/remontagens/releituras,
zero registros confirmados no boot, 64 itens na fila, 15 descartes antes da
durabilidade e 1.761.280 bytes disponíveis. Não houve consulta/alteração da
Cloud nem acesso direto à placa. O trecho não inclui SHA ELF do equipamento.
As 38 requisições/38 respostas/zero colisões são informações do operador,
não um ensaio físico executado pelo agente.

## Auditoria solicitada

| Item | Constatação |
|---|---|
| Montagem/remontagem | A 0.2.19 remonta o mesmo FatFs usando o registro WL/VFS existente; sucesso de `f_mount` comprova montagem, não criação/escrita de arquivo; a releitura subsequente também não confirma escrita. |
| Modo antigo | `append_record` usava `fopen(path,"a+b")`, que solicita escrita e, no IDF, chega a `FA_READ|FA_WRITE|FA_OPEN_ALWAYS`. Um AM_RDO continua recusando escrita depois de qualquer remontagem. Trocar apenas para `ab` não resolve essa proteção. |
| Caminho/nome | Para segmento 1, o fonte constrói `/tbflash/00000001.LOG`, nome 8.3 válido. A 0.2.19 não o enviava no diagnóstico. |
| Permissões | `vfs_fat_fstat`/`get_stat_mode` do SDK sempre incluem bits Unix de escrita; não expõem AM_RDO. A conferência correta usa `f_stat`/`FILINFO.fattrib`. Não foi aplicado chmod ou alteração de atributo. |
| Descritores | `fclose` é tentado após erros; o VFS libera seu FIL mesmo quando `f_close` falha. FatFs pode deixar lock de compartilhamento até remontagem. O teste anterior dessa condição continua passando. A nova abertura fecha o descritor se `fdopen` falhar. |
| Concorrência | Writer, recuperação, batch, ACK e manutenção do outbox usam `state.mutex`. Não foi encontrado outro consumidor de `/tbflash` que contorne esse mutex. O snapshot do FRESULT é limitado à abertura e à tarefa que a iniciou. |
| Os 24 sucessos | `recover_after_failure` da 0.2.19 incrementava `recovery_successes` logo após `recover_records`, antes de qualquer `fwrite`/`fflush`/`fsync`/`fclose` bem-sucedido. Era sucesso de releitura, não de persistência. |
| Espaço livre | Espaço de clusters não garante entrada de diretório, permissão de escrita ou ausência de lock. O teste enche apenas as entradas da raiz, com arquivos vazios, e reproduz FR_DENIED mantendo os clusters livres. |
| Cloud | `tb_store_batch` recusa lote quando o índice não está pronto. O bridge só inicia HTTP para lotes válidos e não vazios; a falha persistente explica `bridge.attempts=0`. Não há bloqueio causado por GPS neste caminho. |

Referências na base 0.2.19: `store.c`/`append_record:847`,
`recover_after_failure:681–705`, `storage_task:989–1037`,
`tb_store_batch`; `core.c`/`bridge_task`.
Referências ESP-IDF: `components/fatfs/vfs/vfs_fat.c`, funções
`fat_mode_conv`, `fresult_to_errno`, `vfs_fat_fstat`, `get_stat_mode`,
`vfs_fat_open`/`close`; `components/fatfs/src/ff.c`, `f_open`,
`dir_alloc`/`dir_register`, `f_close`/`f_mount`.

## O que significa errno 13

O mapeamento do SDK transforma vários códigos em EACCES:

| FRESULT FatFs | Valor | Possibilidade |
|---|---:|---|
| FR_DENIED | 7 | AM_RDO, caminho de diretório ou criação de entrada negada/sem espaço no diretório |
| FR_WRITE_PROTECTED | 10 | Meio protegido; o driver WL padrão da flash não anuncia STA_PROTECT |
| FR_LOCKED | 16 | Conflito na tabela de compartilhamento |

**errno 13 não é FRESULT 13**; este último significa FR_NO_FILESYSTEM.
Uma partição marcada somente leitura no VFS é recusada com EROFS, não deve
ser diagnosticada automaticamente como AM_RDO a partir do número 13.

Faltam para atribuir a recusa do equipamento: FRESULT da abertura original,
`f_stat` do caminho exato e atributos/tamanho desse objeto. Se o objeto não
existir e a criação continuar retornando FR_DENIED, a análise dos slots/entradas
do diretório ou uma cópia privada da partição será necessária para separar
diretório cheio de inconsistência FAT. Não há acesso USB/VPN à placa aqui.

## Correção implementada

1. `recovered_file_readonly` lê atributos FAT reais. O writer não insiste no
   último segmento AM_RDO: preserva-o e usa o próximo ID de segmento.
2. Novo arquivo é criado com `O_RDWR|O_APPEND|O_CREAT|O_EXCL`; colisão não abre
   um arquivo estrangeiro silenciosamente. Arquivos próprios existentes usam
   append, sem O_TRUNC. Nenhum prefixo anterior é reescrito. O IDF pode mapear
   O_APPEND a FA_OPEN_ALWAYS; o diagnóstico registra as flags FatFs reais,
   além das flags POSIX fornecidas, para não esconder essa diferença.
3. A migração persiste uma única marca `tb_store/keep_through` com o maior ID
   antigo. Segmentos anteriores à atualização não são apagados ou renomeados,
   mesmo com ACK; arquivos desconhecidos e `.BAD` também permanecem intocados.
   Um arquivo antigo gravável pode receber somente append, mantendo seu prefixo
   e seus IDs. A marca não altera ACK, reservas, identidade ou contadores de viagem.
4. Arquivos AM_RDO permanecem protegidos também após ACK. Apenas segmentos novos
   e graváveis continuam na reciclagem normal depois do ACK comprovado, já
   autorizada no sistema anterior. A proteção dos arquivos antigos consome
   espaço: não se promete autonomia adicional sobre uma flash já ocupada.
5. `recovery_scans` conta releituras; `recovery_successes` só avança depois de
   persistir um registro real com `fwrite` completo, `fflush`, `fsync` e `fclose`.
   Não existe registro fictício de teste enviado à Cloud.
6. `storage.ready` exige escrita confirmada. `read_ready` pode ser verdadeiro
   antes: somente registros CRC válidos recuperados da flash podem ser ofertados
   nesse estado. A fila RAM continua não durável e não é enviada diretamente.
   O ACK continua exigindo alvo efetivamente ofertado, verificação do intervalo
   e commit NVS; não há ACK inventado nem salto de pendências.
7. Após três falhas consecutivas de IO ou recuperação, pausa as escritas,
   preservando o item em voo e a fila. Quando possível, conclui a releitura
   para permitir sincronizar registros antigos já duráveis. Não faz loop
   infinito de aberturas/remontagens, reset, formatação ou mudança de backend.
8. O GET de diagnóstico mostra caminho, modo, flags, errno original/close,
   FRESULT e flags medidos via `--wrap=f_open`, resultado de `f_stat`, atributos,
   tamanho, tipo FAT, slots totais da raiz e estado da montagem/remontagem.
   Código não medido é nulo. O wrapper não modifica resultados, modos ou drivers.
   O disassembly do binário confirma que `vfs_fat_open` chama o wrapper.
9. `POST /api/storage/retry`, com Bearer administrativo e `{"authorized":true}`,
   libera uma nova tentativa limitada após revisão do erro. Não limpa arquivos,
   fila, IDs, ACK, viagens, combustível ou configurações J1708; não reinicia e
   não pausa/rearma a aquisição. Não foi invocado na placa pelo agente.
10. Formatação automática da flash é desabilitada inclusive em partição vazia.
    Provisionar mídia vazia exige manutenção separada e explicitamente autorizada.

## Testes executados

- `tests/run_all.sh` completo com ASan/UBSan e 24 testes Python: PASS.
- FatFs original ESP-IDF 5.5.2 sobre disco RAM descartável: 24 remontagens/
  leituras com OPEN/EACCES em AM_RDO; novo segmento exclusivo permite gravar;
  arquivo anterior mantém nome, atributos e bytes: PASS.
- Instrumentação original compilada: FRESULT/flags reais, tarefa correta,
  atributos FAT versus modo Unix e rastreio sem atribuir chamada alheia: PASS.
- Diretório cheio com clusters livres, write protection, colisão exclusiva,
  sync/close interrompidos, lock e erro persistente: PASS. Diretório cheio
  permanece bloqueado; não se apaga nada para simular correção.
- Store real: rollover do arquivo somente leitura, migração da marca de
  preservação, falha do commit, append mantendo prefixo, IDs/ACK/reboot,
  arquivos antigos/read-only preservados após ACK e colisão sem sobrescrita: PASS.
- Dez etapas de IO, pausa após três falhas, 24 scans sem falso sucesso,
  retomada limitada e erro de recuperação: PASS.
- Handler administrativo real com guards de autorização, JSON, OTA e mutex;
  retomada preserva 64 itens RAM, NVS, erro histórico e pendência: PASS.
- Worker real com erros repetidos, retenção, espera fora do mutex, retry
  supervisionado e fairness/IDLE; suíte passiva/ativa J1708 e demais regressões: PASS.
- Duas horas offline em tempo virtual, sem UTC e com UTC: 441.135 frames,
  3.789 registros, 14 reinícios simulados, 0 descartes por capacidade;
  1.477.083 / 1.595.469 bytes físicos dentro do orçamento 1.818.624: PASS.
- Reconexão com ACK perdido: 3.797 entregas, 3.789 registros únicos,
  zero duplicação e zero registros válidos pendentes: PASS.
- Build real ESP-IDF 5.5.2, ESP32-C6/8 MB: PASS; aplicação 1.556.512 bytes em
  slot 3.145.728 bytes; partições iguais à base.

Os testes usam falhas controladas/bytes sintéticos. Não são ensaio elétrico,
campanha NEXIQ ou medição de sucesso no caminhão. Os testes só formatam um
disco RAM temporário, nunca a flash de um dispositivo.

RAM estática ELF: 210.172 B na 0.2.19 → 210.356 B na 0.2.20, aumento de 184 B,
sem novas filas ou aumento de capacidades. Heap e latência reais da candidata
não foram medidos na placa. Evidências: `test_evidence/v0220_*`.
Foi conferida identidade byte a byte de 39 arquivos de aquisição, requisições,
decodificação, viagens, rede, configuração e partições contra a base 0.2.19.

## Arquivos e reprodução

- `components/tb_core/store.c`: abertura, proteção, recuperação e diagnóstico.
- `components/tb_core/CMakeLists.txt`: observação do FRESULT original pelo linker.
- `components/tb_core/core.c`/`core_internal.h`: um endpoint de retry do armazenamento.
- `components/tb_core/tests/*`, `tests/run_all.sh`: regressões e mocks de fronteira.
- CMake/build/package e workflow: versão 0.2.20; docs e evidências.

Não foram alterados captura/transmissão, PIDs, MIDs, cadências, arbitragem,
proteções ou persistência J1708. O modo automático já habilitado na NVS
permanece habilitado; não depende do endpoint de retry. Viagens/combustível,
J1939, Wi-Fi, Link, OTA e protocolos Cloud permanecem preservados.
Link instalado, microSD, FRAM e 4G não foram atualizados ou ativados.

No projeto `firmware/espidf`, com ESP-IDF 5.5.2:

```sh
bash tools/build_v020.sh core
sh tests/run_all.sh
python3 tools/build_release.py --core-only --output-dir release-v0220 --package Truckbox_Core_B1_0.2.20_FLASH_OPEN_CANDIDATA.zip
```

## Atualização, verificação e pendências

Não foi feita OTA, reset, mudança de configuração em produção ou transmissão
de teste. Instalação exige aprovação explícita, caminhão parado e alimentação
estável. Usar apenas `truckbox_core_0.2.20_ota.bin`, conferindo hash do manifesto;
não gravar NVS, storage, partições, bootloader ou firmware do Link durante OTA.

A versão antiga não exporta a fila RAM. Os 64 itens RAM e descartes anteriores
não têm recuperação garantida ao reiniciar/atualizar; não foram reconstruídos
ou enviados como valores fictícios. Arquivos e registros duráveis são preservados.

Depois de atualização autorizada, conferir `readonly_segment_count` e
`first_readonly_segment`, `ready`, `durable_write_confirmed`, `committed_this_boot`,
`recovery_result`, fila, ACK e bridge. Se a falha persistir, são necessários
`last_io_failure` completo e resultados do filesystem; não repetir indefinidamente
o retry sem analisar esses dados. FR_DENIED com objeto ausente/diretório cheio
exigirá evidência do diretório/backup e uma decisão de manutenção que respeite
a preservação. Esta entrega não declara a escrita em campo restabelecida.

Formato de registros, partições, ACK e rollback OTA continuam compatíveis.
Para rollback manual, seguir `OTA.md`/`UPGRADE_RECOVERY.md`; não apagar dados
para liberar a barreira de rollback. A 0.2.19 não conhece a nova marca de
preservação e pode reciclar arquivos ACKados segundo sua política anterior;
preservar cópia e avaliar esse limite antes de escolher retornar à versão antiga.
