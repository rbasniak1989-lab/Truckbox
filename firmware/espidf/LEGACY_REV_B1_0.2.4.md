# TruckBox Rev B1 — CORE 0.2.4 (LEGACY por padrão)

**Branch de testes. Não instalar automaticamente; não apagar NVS, fila persistente, ACKs ou histórico.**

## Por que este perfil existe
O antigo TruckBox V0.4.4 separava RX CAN de decodificação, enviava
telemetria consolidada e mantinha internet/RAW fora do caminho crítico.
Nossa arquitetura aprovada em agosto/2026 também previa ler tudo, mas não
gravar cada quadro. A 0.2.3 passou a persistir RAW e DECODED continuamente,
sobrecarregando a flash na ausência de ACK do Link.

A Rev B1 de campo não tem microSD, GPS dedicado e SIM/4G operacionais.
A 0.2.4 não depende desses circuitos nem muda a pinagem da 0.2.3.

## Modo LEGACY (padrão)
- CAN TWAI listen-only e J1708 receive-only intactos.
- RPM, velocidade, combustível e distância permanecem processados em tempo
  real no caminho curto capture.c, mesmo sem internet ou flash disponível.
- O worker `tb_decode` e sua fila continuam funcionando para decodificar todos
  os PIDs/PGNs catalogados, inclusive respostas J1708 solicitadas.
  Os últimos valores ficam no cache e no endpoint `/api/sensors`, mas o modo
  LEGACY não gera registros persistentes RAW/DECODED por quadro.
- Mantém RAW CAN no TCP/35000 somente com cliente Android conectado;
  sem cliente não formata HEX por quadro.
  O último quadro J1708 segue disponível no status local.
- OPERATIONAL com dados atuais: a cada 15 segundos; HEALTH: a cada 60 segundos.
- Mantém checkpoints e flash interna limitada, com ACK/replay inalterados.
- Não usa microSD, SIM, LTE ou GPS físico como requisito.

## Modo COMPLETO (testes curtos, caminhão parado)
No painel do CORE, habilitar `full_decode_enabled` e reiniciar.
Restaura o comportamento persistente da 0.2.3: RAW/DECODED por quadro e
OPERATIONAL a 1 Hz. Decoder e cache continuam em ambos os modos. Não modifica os drivers CAN/J1708.
Estado de diagnóstico: `telemetry_profile` e `operational_interval_s`.
Desabilitar novamente e reiniciar para voltar ao LEGACY.

## Limpeza de registros locais depois da OTA
- Só o CORE com versão 0.2.4 e imagem OTA em estado VALID aceita o comando.
- No painel autenticado, seção **Manutenção do armazenamento**, solicitar
  **Descartar registros locais e reiniciar** e confirmar digitando DESCARTAR.
- Endpoint: POST /api/storage/discard, Bearer admin, JSON
  `{"confirm":"DESCARTAR_REGISTROS"}`. Nunca usar sem consentimento.
- O comando apenas grava a marca `discard_logs=1` em `tb_store` (NVS), depois
  reinicia. Antes de recuperar a fila no boot, apaga somente arquivos de segmento
  `XXXXXXXX.LOG` da partição FAT `storage`.
- Os namespaces NVS, senhas, histórico na nuvem, checkpoints da viagem,
  slots OTA e tabela de partições não são apagados.
- A marca NVS só é zerada ao concluir todos os arquivos; corte de energia
  deixa a marca e reinicia a limpeza no próximo boot.
- O gerador de sequências preserva `reserved` e `ack`; não reutiliza IDs.
- Confirmar em /api/state: `storage.discard_completed_this_boot=true`,
  `storage.discarded_files`, `storage.pending_records`.
- Não há ampliação dinâmica da partição Storage: apagar o slot OTA antigo
  não lhe transfere capacidade e destruiria a proteção de rollback.
- Limpeza irreversível dos registros locais não entregues; nenhuma gravação ou
  eliminação acontece ao simplesmente instalar o novo firmware.

## Segurança e limites
- A atualização OTA deve preservar NVS, filas existentes e partições.
  A mudança de perfil **não apaga nem compacta à força o backlog anterior**.
- Não ativar `sd_enabled`, `fram_enabled`, `modem_enabled` nem
  `repaired_power_gates` sem reparo elétrico comprovado. U8/DONE continua sem alteração.
- A flash pode continuar cheia até a conexão CORE–LINK receber ACK válido.
  A redução de volume não substitui o conserto do transporte.
- O LEGACY não é o binário V0.4.4 antigo; usa pinagem e periféricos da Rev B1.
- Se a CAN continuar zerada no LEGACY, investigar driver, transceptor U3,
  GPIO2 e alimentação sem atribuir automaticamente ao parser.
- No LEGACY, `/api/sensors` é alimentado pela decodificação completa.
  Itens sem fórmula validada continuam identificados como indisponíveis, e
  um PID recebido não é automaticamente uma resposta causada pelo pedido.
- Não marcar firmware como validado em caminhão sem build ESP-IDF e teste real.

## Testes necessários
1. `sh components/tb_core/tests/run_legacy_profile.sh` testa a cadência.
2. `sh tests/run_all.sh` requer ESP-IDF 5.5.2 e ferramentas do host.
3. `bash tools/build_v020.sh core` compila CORE 0.2.4; não grava a placa.
4. Conferir tamanho de RAM, boots, CAN/J1708, uptime, frequência de amostras,
   ACK/flash e persistência de viagem em bancada antes de OTA.
5. Link 0.2.3 permanece compatível; não é necessária atualização do Link.

## Capacidade e reparticionamento futuro (USB; não é OTA)
- O layout original 8 MiB usa dois slots OTA de 3 MiB e FAT storage 1.875 MiB.
- O Core 0.2.3 .bin de testes ocupa 1.437.408 bytes. Uma alternativa para
  avaliação é **dois slots OTA de 2 MiB** e **storage 3.875 MiB**.
- Modelo somente de referência: `partitions_usb_2x2mb.csv`. NÃO trocar a
  tabela de partições por OTA; precisa backup de NVS/credenciais, gravador USB,
  imagem completa compatível e procedimento de recuperação/teste de energia.
- O limite anterior de 24 segmentos x 48 KiB comportava apenas 1.125 MiB de
  registros, mesmo com a partição 1.875 MiB. No layout atual continua 24;
  em uma partição maior lida em runtime o limite sobe, reservando 512 KiB para
  FAT e operações temporárias.
- Como 3.875 MiB ainda é limitado sem internet, planejar perfil OFFLINE:
  acumular consumo e distância em tempo real; guardar resumos menores e menos
  frequentes quando backlog crescer, sem apagar histórico já confirmado.
- Mantemos os dois slots OTA e rollback. Nenhuma gravação ou reparticionamento
  foi realizado no hardware.

## Requisições ativas J1708 — os 17 PIDs selecionados
- A recepção/decodificação J1708 e as requisições ativas são independentes do
  destino dos dados. LEGACY desativa RAW por quadro, **não** a decodificação.
- O agendador atual contém: 26, 33, 34, 37, 62, 68, 117, 118, 235, 236,
  250, 31, 32, 63, 108, 158 e 160. Todos são *alvos de validação*, não
  respostas causais homologadas pela captura NEXIQ.
- O controlador TX `tbx_j1708_active.c` permanece desarmado no boot.
  Não há MID local ou MIDs-alvo inventados. Habilitar transmissão requer
  comprovar o circuito DE21 invertido, MID local livre, destino de cada PID,
  `topology_confirmed`, `authorized` e `stationary`. A proteção já existente
  impede envio inadvertido ao ligar ou enquanto a placa não estiver validada.
- A autorização vale 60 s e é interrompida por movimento observado, OTA,
  colisão/eco inválido e falhas repetidas; o teste é estacionado e deliberado.
- Por padrão, o agendador mantém 1 pedido em aberto, espaçamento mínimo
  250 ms e timeout 450 ms. Isso **não** garante 500 ms por PID (17 no total).
  Precisamos medir a carga real e ajustar prioridades/cadência antes de produção.
- Durante ensaio no LEGACY, REQUEST/TX_ECHO/RESPONSE_CANDIDATE/TIMEOUT são
  contabilizados em `/api/state`, mas não gravados repetidamente na flash;
  eventos de segurança/armar/parar continuam persistidos.
- O PID 250 é um total de combustível da vida da ECU. O consumo da viagem
  é calculado por diferença entre leituras confiáveis, nunca zerando a ECU.
- **Pendente para a próxima etapa:** ampliar `HEALTH` para agregar os sensores
  selecionados (mínimo/máximo/média), respeitando limite de 768 bytes por registro.
  Nesta etapa, o Supabase ainda recebe resumos operacionais básicos e contadores
  de CAN/J1708, não todos os sensores catalogados.
