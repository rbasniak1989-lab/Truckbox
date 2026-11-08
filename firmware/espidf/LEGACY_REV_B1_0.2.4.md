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
- Não inicia o worker tb_decode nem aloca a fila e a stack; o cache estático
  continua reservado no binário, porém sem atualização em LEGACY.
  A telemetria RAW/DECODED não é gerada para gravação ou upload.
- Mantém RAW CAN no TCP/35000 somente com cliente Android conectado;
  sem cliente não formata HEX por quadro.
  O último quadro J1708 segue disponível no status local.
- OPERATIONAL com dados atuais: a cada 15 segundos; HEALTH: a cada 60 segundos.
- Mantém checkpoints e flash interna limitada, com ACK/replay inalterados.
- Não usa microSD, SIM, LTE ou GPS físico como requisito.

## Modo COMPLETO (testes curtos, caminhão parado)
No painel do CORE, habilitar `full_decode_enabled` e reiniciar.
Restaura o comportamento integral da 0.2.3: tb_decode, RAW/DECODED,
cache de sensores e OPERATIONAL a 1 Hz. Não modifica os drivers CAN/J1708.
Estado de diagnóstico: `telemetry_profile` e `operational_interval_s`.
Desabilitar novamente e reiniciar para voltar ao LEGACY.

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
- No LEGACY, leituras adicionais do endpoint `/api/sensors` não serão
  produzidas pelo decoder completo; dados essenciais do painel permanecem.
- Não marcar firmware como validado em caminhão sem build ESP-IDF e teste real.

## Testes necessários
1. `sh components/tb_core/tests/run_legacy_profile.sh` testa a cadência.
2. `sh tests/run_all.sh` requer ESP-IDF 5.5.2 e ferramentas do host.
3. `bash tools/build_v020.sh core` compila CORE 0.2.4; não grava a placa.
4. Conferir tamanho de RAM, boots, CAN/J1708, uptime, frequência de amostras,
   ACK/flash e persistência de viagem em bancada antes de OTA.
5. Link 0.2.3 permanece compatível; não é necessária atualização do Link.
