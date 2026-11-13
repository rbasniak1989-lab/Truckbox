# Rev.B.1 0.2.19 — recuperação da flash e investigação dos reinícios

Data: 13/11/2026. Dispositivo analisado: `tb-c6-d40592b9190c`.
Branch: `fix/revb1-v0219-storage-recovery`, derivada da 0.2.18 real,
commit `48aff2f05802cb79a24163877db6912b8df085c7`.
Estado: candidata compilada e testada em software; não instalada pelo agente.
Nenhuma OTA, reset, alteração de configuração, ativação de TX, formatação,
mudança de viagem, deploy Cloud ou merge foi executado.

## 1. O que os dados realmente comprovam

As consultas ao Supabase foram somente SELECT, restritas a este dispositivo.
Os 100 `BOOT_DIAGNOSTIC` da 0.2.18 recebidos pela Cloud, boots observados
310–515, contêm o SHA ELF
`a2f4dca0da66be3686b285715b51a195176bce479dc87b5f089def154ad42fe1`,
igual à imagem 0.2.18 entregue. O diagnóstico fornecido pelo usuário informa
0.2.18 no boot 553, mas não inclui SHA do aplicativo.

| Motivo registrado no ESP-IDF/C6 | Quantidade recebida na 0.2.18 |
|---|---:|
| 1 — POWERON, energização/reset de chip | 95 |
| 9 — BROWNOUT, queda de tensão detectada | 2 |
| 4 — PANIC, exceção/crash de software | 2 |
| 3 — SW, reinício por software | 1 |

O boot 310 registrou SW; isso é compatível com atualização/configuração, mas
não identifica qual comando foi usado. Os PANIC originaram os boots 322 e 323,
com registros de início às **13:12:44 e 13:18:51 de 12/11, Brasília**
(`time_valid=true`). Os BROWNOUT originaram os boots 395 e 399, cujos registros
chegaram à Cloud em 12/11, **16:57:58 e 16:58:01 Brasília**. Nesses dois casos
`time_valid=false`: são horários de recebimento, não prova do instante do reset.

O último registro recebido nesta consulta foi seq 108560 em
13/11/2026 17:41:15.953 Brasília. Existem lacunas nos boots recebidos; não
atribuímos um motivo aos boots ausentes. O avanço de 515 para 553 confirma que
o histórico da Cloud é incompleto. Não se somam DTCs históricos reemitidos
como se fossem novos boots ou telemetria atual.

O usuário confirmou que os reinícios aconteceram sozinhos, com a alimentação
ligada. POWERON não identifica sozinho conector, regulador, EN ou watchdog
externo. Os dois BROWNOUT comprovam episódios de baixa tensão; os dois PANIC
comprovam também um problema de software ainda sem backtrace disponível.
Não é correto atribuir todos os reinícios a RAM, alimentação ou TX.

No último HEALTH recebido antes dos dois PANIC, a mínima de heap reportada
era 20.488 B (boot 321) e 11.984 B (boot 322). Isso não mede a memória no instante
do crash nem prova sua causa. O histórico posterior ainda registra mínimos
baixos, inclusive 2.416 B no boot 509. O mínimo 40.140 B do boot 553 cobre apenas
55,6 s e não comprova estabilidade prolongada de RAM.

## 2. Por que o armazenamento não se recuperava

No boot 553: `ready=false`, onze erros de escrita, zero registros confirmados
neste boot, fila RAM 34/64, disponibilidade estimada 1.761.280 B.
`pending_records=null` significa desconhecido; não significa fila vazia.
`last_ack_seq=max_seq=108560` coincide com o último seq recebido na Cloud,
mas não comprova ausência de outros bytes pendentes na flash.

Na 0.2.18, `store.c:766–781` reúne `fopen`, `fwrite`, `fflush`, `fsync` e
`fclose` numa condição que só registra a mensagem genérica. O worker em
`store.c:900–934` aguarda 5 s e relê arquivos na mesma montagem. Não reinicializa
o estado do filesystem. Se o IO continuar falhando, a releitura não o conserta.
O estado `ready=false` impede a oferta de lotes, explicando `bridge.attempts=0`.

Foi reproduzido um defeito concreto de recuperação com **o FatFs original
do ESP-IDF 5.5.2**, configurações relevantes iguais às do Core, num disco RAM
descartável: falha em `f_sync` e `f_close` deixa a entrada de compartilhamento
presa. O VFS do IDF libera seu descritor mesmo se `f_close` falhar, mas o FatFs
só libera o bloqueio se a sincronização fechar com sucesso. Novo `f_open`
retorna `FR_LOCKED`; reler não limpa esse estado. Remontar o mesmo volume
liberou o bloqueio e preservou os bytes anteriormente confirmados e `.BAD`.

Referências da dependência: `components/fatfs/vfs/vfs_fat.c`,
`vfs_fat_close` e `file_cleanup`; `components/fatfs/src/ff.c`,
`f_close` e `f_mount`. O teste compila esses fontes originais, não uma
reimplementação de FatFs. Somente o disco e as fronteiras RTOS são mocks.

**Limite da conclusão:** isso comprova uma falha recuperável que o código
antigo não tratava; não comprova que o boot 553 sofreu exatamente FR_LOCKED.
O firmware antigo não guardou etapa/errno. Corrupção FAT persistente, falha
elétrica, flash defeituosa, atributo/acesso, esgotamento de recursos ou pressão
transitória de RAM não podem ser descartados pelo JSON atual.

## 3. Correção incremental

- `store.c`, `failed_record_io`/`append_record`: registra OPEN, WRITE, FLUSH,
  SYNC, CLOSE e operações de cauda/separador, o primeiro errno e eventual
  errno do fechamento. Zero é explicitamente desconhecido; não inventa EIO.
  Guarda segmento, sequência, bytes solicitados/escritos e uptime/boot.
- `rebind_flash_volume`: após falha de escrita em flash, sob o mutex e com
  todos os FILEs/DIRs fechados, usa o registro WL/VFS existente e `f_mount`
  para remontar **o mesmo volume**. Não usa mkfs, não desmonta WL, não apaga
  arquivos, não troca de backend e não reinicia o ESP32. A leitura dos registros
  ocorre depois dessa remontagem, eliminando o cache anterior como evidência.
- `recover_after_failure`/`storage_task`: mantém o retry de 5 s e o registro
  retido. Só o libera se uma sequência CRC válida for recuperada; caso contrário
  repete o payload. Falha persistente continua bloqueada, com pendência
  desconhecida. Nenhuma leitura vira confirmação Cloud.
- No retry do mesmo boot, conserva a parte não usada da reserva de sequências
  já persistida, respeitando maior sequência recuperada e sequence_floor.
  Evita commit NVS/bloco novo a cada retry. Nunca reutiliza a sequência que
  falhou; após reboot, mantém o salto para depois da reserva persistida.
  A reserva recuperada em RAM agora só avança depois do commit NVS: uma falha
  nesse commit não permite que a próxima recuperação o pule como se estivesse
  confirmado. O teste injeta falha com sequência recuperada maior que a reserva
  e comprova o retry e `reserved >= ack` depois da retomada.
- Mantém `fflush` + `fsync` + `fclose` antes de ofertar novos registros e
  a verificação/commit NVS do ACK antes de reciclar segmentos. Evidências
  corrompidas permanecem preservadas pelo mecanismo anterior.
- `/api/state`: expõe `last_io_failure`, `recovery_attempts`,
  `recovery_successes`, `volume_rebinds`, `volume_rebind_pending` e
  `volume_rebind_fresult`. `historical` distingue o último erro após retomada.
- `tb_web/tb_crash.c`: adiciona GET `/api/crash` e `/api/crash.bin`,
  ambos administrativos, bloqueados durante OTA e independentes do FatFs.
  O primeiro informa validade/razão disponível; o segundo exporta a partição
  de crash inteira em blocos de 512 B, inclusive para preservação forense
  de um dump inválido. Não escreve/apaga a partição nem envia o dump à Cloud.
  `dump_boot_id=null` evita atribuir dump antigo ao boot atual sem evidência.

O código e a política de polling, os27 PIDs/MIDs/cadências, a recepção passiva,
Wi-Fi, descoberta do Link, Cloud, GPS opcional, J1939, NVS/identidade, viagens
e partições permanecem preservados. O estado persistente de TX desabilitado
do dispositivo não foi alterado. Link, microSD, FRAM e4G não foram ativados.

## 4. Testes executados

| Verificação | Resultado |
|---|---|
| Build real ESP-IDF 5.5.2, ESP32-C6, 8 MB | PASS, aplicação 1.551.472 B em slot 3.145.728 B |
| `tests/run_all.sh`, ASan/UBSan com falha obrigatória | PASS; inclui 24 testes Python e toda suíte anterior |
| FatFs real: sync/close interrompidos, lock, remontagem, bytes/arquivos preservados | PASS; remontagem sem escrita no disco RAM |
| IO persistente, volume inválido e falha de remontagem | PASS; não simula sucesso nem ACK |
| Nove etapas de escrita, primeiro erro + erro secundário de close | PASS |
| Onze falhas consecutivas, retomada, registro retido e CRC/ACK sem duplicação | PASS |
| Cortes em 90 posições de bytes, legado 0.2.11, corrupção e sufixos válidos | PASS |
| Recepção passiva, UART, arbitragem, PID153/439, 27 PIDs, trip/NVS e watchdog/fairness | PASS, mesmos limites/proteções |
| API de crash: autorização, OTA, CRC/limites, erro de leitura e falhas de alocação | PASS; stream 512 B, sem dump público/associação falsa de boot |
| Duas horas offline, sem UTC e com UTC, reconexão | PASS em tempo virtual; 441.135 frames/7.200s,3.789 registros |
| Offline após 14 reinícios simulados do armazenamento | PASS;1.477.083 B sem UTC /1.595.469 B com UTC; orçamento 1.818.624 B |
| Reconexão/ACK perdido | PASS;3.797 entregas,3.789 linhas únicas,0 duplicadas,0 registros válidos pendentes |

Os `.BAD` preservados continuam ocupando cerca de 684 kB depois do ACK e diminuem
a autonomia futura. Duas horas simuladas não são ensaio elétrico na placa.
Os testes não demonstram reparo físico da alimentação ou sucesso atual da
gravação do boot 553. Logs completos de testes estão em `test_evidence/v0219_host_tests.txt`.

RAM estática física no ELF: **210.084 B na 0.2.18 → 210.172 B na 0.2.19**, aumento
de 88 B para diagnóstico/contadores, sem aumentar filas. A exportação de crash
usa buffer de512 B na pilha apenas sob pedido administrativo. Heap real,
fragmentação e máximas de pilha da 0.2.19 ainda não foram medidos em campo.
O painel compacto e `/api/health` sem alocação JSON permanecem disponíveis.

## 5. O que fazer na placa antes de autorizar atualização

1. Com o caminhão estacionado, um técnico deve registrar simultaneamente a
   entrada/terra, `3V3_MAIN` em TP1 e `CORE_EN` em TP5, procurando queda de
   tensão ou pulso de reset durante a falha. Multímetro pode não capturar
   pulsos curtos; não identificar um componente apenas pelo motivo de reset.
2. Conferir chicote, conector e soldas, regulador e o caminho EN/U8/ACC conforme
   [HARDWARE.md](HARDWARE.md), projeto realmente fabricado e datasheets.
   A documentação registra uma medição histórica de EN 1,6 V, footprint U8
   espelhado e ausência de pulsos DONE no firmware. Não tratá-la como medição
   atual. U8 é um candidato a verificar, não a causa já demonstrada.
   Não criar pontes, gerar DONE, retirar watchdog ou mudar GPIO por suposição.
3. Preservar os arquivos/partição atuais e o crash. Não formatar ou apagar NVS.
   A 0.2.18 não possui os novos endpoints de crash/IO e não foi possível ler
   sua flash física neste ambiente. Obter dump existente exige manutenção
   autorizada; o backtrace dos PANIC ainda falta.
4. Os 34 itens que o boot 553 mostra na RAM **não são duráveis**. A OTA reinicia
   o Core e não garante preservar esses itens se o armazenamento continuar
   falhando. A API antiga não exporta a fila RAM. Não prometer recuperação
   desses itens após perda de energia ou atualização.

Não foi feita conexão direta à rede privada da placa. O histórico de Cloud
não substitui os registros ausentes nem medições da alimentação/flash.

## 6. Compilar, atualizar e retornar

Com o ESP-IDF 5.5.2 instalado, a partir de `firmware/espidf`:

```sh
. /caminho/esp-idf-v5.5.2/export.sh
bash tools/build_v020.sh core
sh tests/run_all.sh
python3 tools/build_release.py --core-only --output-dir release-v0219 --package Truckbox_Core_B1_0.2.19_FLASH_CANDIDATA.zip
```

Esses comandos apenas compilam, testam e empacotam. Não instalam firmware.


Somente após aprovação explícita, com veículo parado e alimentação estável:
usar `truckbox_core_0.2.19_ota.bin`, de aplicação, na página OTA já existente.
Conferir SHA256 no manifesto. Não gravar tabela de partições, NVS, storage,
bootloader ou imagem de Link durante OTA. A partição mantém o SHA
`5e25baf13060f871fb6e9103f6cec4000a6c0e36bdf4d554fa037a26f3f91832`.

Conferir versão, `storage.ready`, último erro/etapa, retomada de `max_seq`,
`bridge.successful_acks`, pendências e o bloqueio persistente de TX. Isso é
validação da recuperação instalada; os testes deste relatório são de software.

Para investigar os crashes, após atualização autorizada, consultar `/api/crash`
e baixar `/api/crash.bin` com o Bearer administrativo já usado pelo painel.
Manter o binário privado: dumps podem conter dados/configurações de RAM.
Não publicar o dump em PR ou chat. A imagem pode pertencer a firmware anterior;
analisar com o ELF correspondente ao SHA contido nela, não presumir o atual.

Rollback: a 0.2.18 anterior e seus artefatos não foram alterados. A tabela,
NVS e formato do outbox permanecem compatíveis. Preservar o rollback automático
do bootloader. Para retorno manual depois da validação, seguir `OTA.md` e
`UPGRADE_RECOVERY.md`, com a barreira `/api/storage/prepare-rollback` existente
e `rollback_ready=true`; não apagar pendências para forçar retorno. Se a fila
não puder drenar, a manutenção e o risco dos itens RAM precisam ser resolvidos
explicitamente antes de reiniciar.

## 7. Pendências

- Estabilizar alimentação/EN; localizar a causa elétrica com medições.
- Obter/analisar dump dos dois PANIC. Causa exata de software não demonstrada.
- Medir etapa/errno do IO na placa; 0.2.18 não registrou esses valores.
- Verificar a candidata na placa real antes de tratá-la como validada em campo.
- Não foram reprocessados os ZIPs completos NEXIQ/Logs Mãe, ainda indisponíveis.
