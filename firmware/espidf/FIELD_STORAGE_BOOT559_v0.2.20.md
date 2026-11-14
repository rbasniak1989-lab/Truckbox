# Armazenamento 0.2.20 — diagnóstico do boot 559

Análise de 14/11/2026 UTC. Fonte: JSON fornecido pelo operador; não é uma
consulta direta à Cloud ou à placa. Base de código: `521b1eccbe2864fe26fe1d143749686fd4fb557e`.

## Resultado em campo

A 0.2.20 **não restabeleceu a escrita nesta placa**. O rollover para arquivos
somente leitura não se aplica à falha agora medida. A pausa e a contabilização
da recuperação funcionaram como previsto: três erros, três releituras,
zero recuperações com escrita confirmada e nenhuma tentativa posterior infinita.

| Campo medido | Valor | Interpretação |
|---|---:|---|
| `fatfs_fresult` | 7 | `FR_DENIED` da abertura original |
| `fatfs_open_flags` | 7 | `FA_READ | FA_WRITE | FA_CREATE_NEW` |
| `file_stat_result` | 4 | `FR_NO_FILE`: o objeto consultado não existe |
| Caminho | `/tbflash/00000001.LOG` | Nome simples 8.3, diretamente na raiz |
| `partition_readonly` | false | Partição não marcada somente leitura |
| `filesystem_type` | 1 | FAT12 |
| `root_directory_slots` | 512 | Capacidade da raiz; não mede ocupação |
| `segments` / marca de preservação | 0 / 0 | Nenhum LOG/BAD próprio reconhecido; não prova raiz vazia |
| `space_available_bytes` | 1.761.280 | Clusters disponíveis; não informa entradas livres |
| `read_ready` / `durable_write_confirmed` | true / false | Releitura concluída; escrita não comprovada |
| Tentativas / scans / sucessos | 3 / 3 / 0 | Sem falso sucesso de recuperação |
| `queue_depth` / `dropped_before_durability` | 64 / 1 | RAM cheia e um registro descartado antes de persistir |
| `bridge.attempts` / ACKs no boot | 0 / 0 | O bridge não recebeu lote durável para iniciar envio |

Não é o ramo de arquivo AM_RDO, proteção de mídia ou lock: o código original
medido é FR_DENIED, não FR_WRITE_PROTECTED/FR_LOCKED, e o objeto não foi
encontrado. Também não é um erro de alocação de RAM reportado pelo FatFs.

## Caminho exato no SDK

ESP-IDF 5.5.2, `components/fatfs/src/ff.c`:

1. `f_open`, linhas 3730–3790: ao não encontrar o nome e receber CREATE_NEW,
   chama `dir_register`. O nome `00000001.LOG` é SFN 8.3; não exige geração
   de nomes curtos numerados para LFN.
2. `dir_register`, linhas 2470–2547: pede uma entrada a `dir_alloc`.
3. `dir_alloc`, linhas 1811–1840: considera livre entrada cujo primeiro byte
   seja 0 ou 0xE5; se chega ao fim sem encontrar entrada, converte
   FR_NO_FILE em **FR_DENIED**.
4. `dir_next`, linhas 1750–1770: a raiz FAT12/16 é uma tabela fixa. Não cresce
   para os clusters de dados que ainda estão disponíveis.

O mecanismo localizado é **recusa na reserva de entrada da raiz**. Ainda
falta medir por que suas entradas não estão disponíveis: ocupação por
outros objetos, entradas órfãs/inconsistentes ou inconsistência de geometria/
leitura da mídia. Não se afirma que foram contados 512 arquivos na placa.

No Core: `store.c`/`append_record` solicita a abertura; `recover_records`
ignora objetos que não tenham os nomes próprios LOG/BAD; `recover_after_failure`
remonta e relê; `core.c`/`bridge_task` só inicia HTTP para lote válido não vazio.
A ausência de tentativas de envio neste boot é consequência da ausência de
registros conhecidos duráveis, não uma comprovação de falha Wi-Fi/Link.

## Reprodução adicional executada

Ampliado `components/tb_core/tests/test_fatfs_rebind.c`, usando o FatFs real
do SDK sobre disco RAM descartável:

- FAT12, 512 entradas, 512 arquivos estrangeiros vazios: clusters livres
  inalterados, nenhum LOG próprio, OPEN=7, flags=7, stat=4.
- FAT12, 512 slots com entradas LFN órfãs: a listagem pública não mostra
  arquivos, mas nenhum slot é reutilizável. Mesmos códigos da falha.
- Três remontagens em cada cenário não resolvem a recusa. Foi verificada
  ausência de escritas de recuperação e igualdade de todos os bytes da raiz.
- Casos anteriores de AM_RDO, criação exclusiva, lock após fechamento
  interrompido, write protection e falha persistente continuam passando.

Comando: `sh firmware/espidf/components/tb_core/tests/run_fatfs_rebind.sh`,
com ESP-IDF 5.5.2 ativado. **PASS**, ASan/UBSan, saída em
`test_evidence/v0220_boot559_fat12.txt`.

Os dois estados foram construídos apenas em RAM para demonstrar que os
mesmos campos não distinguem as causas. Não são uma reprodução byte a byte
da partição do caminhão. A primeira compilação deste teste adicional detectou
o uso de uma macro interna não exportada; foi corrigida antes da execução PASS.
Não houve nova alteração do firmware ou recompilação de aplicação nesta etapa.

## Evidência necessária e limite da intervenção

Próxima etapa: obter **leitura privada da raiz FAT12 e da geometria do volume**,
idealmente cópia somente leitura da partição `storage`, incluindo a camada WL.
A partição da tabela existente começa em 0x620000 e mede 0x1e0000 bytes;
ela não deve ser tratada como FAT cru sem interpretar wear levelling.
É necessário conferir BPB, limites da raiz, os 512 slots de 32 bytes,
marcadores livres/apagados, cadeias LFN, entradas de arquivos/diretórios e FAT.

A versão instalada não fornece dump bruto ou listagem detalhada da raiz pelo
GET `/api/state`. Este ambiente não tem USB/VPN à placa; não foi feita leitura
nem mudança remota. Uma extração que exija modo bootloader/reset depende de
autorização específica e caminhão parado. Nenhum comando desses foi executado.

Não gerar outra versão alegando reparo sem essa evidência. Trocar somente o
nome do arquivo, tentar criar uma pasta na mesma raiz ou remontar novamente
não libera entradas. Não limpar slots, apagar arquivos, formatar ou executar
reparo automático antes de preservar cópia e identificar seu conteúdo.

`pending_records=0` se refere apenas aos LOGs reconhecidos; não garante que
objetos desconhecidos ou metadados danificados não contenham dados antigos.
Os 64 registros RAM continuam não duráveis e não podem ser reconstruídos
após reset/OTA. Não são enviados antecipadamente para contornar o protocolo.

## Sistemas preservados

No JSON: J1708 automática RUNNING, 71 requisições, 71 respostas **candidatas**,
zero colisões, timeouts e overruns. Validação permanece operacional indireta.
RAM livre 58.868 bytes, mínimo 27.260, maior bloco 19.456; não identifica a RAM
como origem deste OPEN. RSSI -83 dBm é baixo, mas o bridge não tentou HTTP;
não explica o FR_DENIED local. `reset_reason=3` indica reset de software,
não comprova brownout neste boot; sua origem específica não foi medida.

Somente teste e documentação foram alterados nesta investigação. Não houve
nova OTA, reset, mudança de configuração, formatação, exclusão, alteração da
J1708 ou dos contadores. A candidata 0.2.20 e seus artefatos permanecem intactos.
