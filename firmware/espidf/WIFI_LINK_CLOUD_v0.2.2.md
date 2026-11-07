# TruckBox Rev.B.1 0.2.2 — Wi-Fi do Link e ingestão v25 — TESTES

O Core recebe J1939/J1708, decodifica e mantém a fila persistente. O Link recebe
os lotes do Core e faz HTTPS para a nuvem pela internet do roteador/Starlink.
GPS, antena GNSS, SIM, APN e modem LTE não são necessários nesse modo.
Os dois firmwares precisam ser atualizados para 0.2.2. Nenhum dispositivo foi
gravado automaticamente e a versão funcional 0.1.0-b1 permanece arquivada.

## Topologia e compatibilidade

- Modo Wi-Fi de internet: Core e Link conectam suas interfaces STA à mesma LAN
  de 2,4 GHz. O roteador deve permitir comunicação entre clientes.
- O Link usa `wifi_ssid`/`wifi_password` para essa rede e DHCP, em vez do IP
  estático 192.168.4.2. Campo `core_host`: hostname ou IPv4 do Core nessa LAN.
- Core aprende o IPv4 do Link pelo socket de uma mensagem autenticada com a
  chave de vínculo, aceita somente endereços de suas sub-redes locais e expira
  a descoberta após 60 s. O mesmo endereço é usado para envio de lotes e OTA.
  Um endereço fornecido no JSON não escolhe o destino.
- Sem SSID externo no Link, o modo anterior continua: STA na rede privada do
  Core, IP 192.168.4.2, envio pela interface PPP quando LTE está habilitado.
- APs de configuração permanecem: Core 192.168.4.1; Link 192.168.5.1. Eles não
  oferecem roteamento de internet aos clientes. Não há NAT novo no Core.
- No modo externo, a comunicação Core–Link depende da LAN do roteador. Com
  roteador ausente, Core continua capturando e usando sua fila finita em flash.
- O HTTPS é executado somente no Link, com certificado verificado e interface
  de rede selecionada explicitamente. SNTP sincroniza o relógio pela internet;
  não é necessário fix GPS. O modo privado não é confundido com Wi-Fi de internet.

## O que foi corrigido

- Link não exige mais modem/APN/PPP para aceitar um lote quando possui Wi-Fi
  externo conectado. GPS utiliza a mesma seleção de transporte quando existir.
- Core incorpora a redução de retenção de JSON da 0.2.1 e conserva os intervalos
  de bloqueio do decoder/gravador introduzidos em 0.2.0-wdt1.
- Link libera as árvores JSON antes da espera HTTPS; conserva apenas o payload
  e os IDs oferecidos. Rejeita ACK de ID não presente no lote, inclusive lacunas.
- Identificadores, configuração NVS, partições, GPIOs, UARTs, senhas do AP e
  protocolo Android existente são preservados. Os controles defeituosos da B1
  não são habilitados automaticamente.

## Atualização e configuração

1. Atualize o Core pela página dele com `truckbox_core_0.2.2_ota.bin`.
2. Antes de mudar a rede do Link, encaminhe pela página do Core o arquivo
   `truckbox_link_0.2.2_ota.bin`. Se necessário, use a página própria do Link.
3. No Core, mantenha o Wi-Fi do roteador já configurado e defina o identificador
   de ingestão como `tb-c6-d40592b9190c`. Preserve a sequência/NVS existentes.
4. No Link, em **Wi-Fi do celular ou roteador**, cadastre a mesma rede de 2,4 GHz
   do Core. Em **Endereço do Core na rede**, use `truckbox.local` ou o IP LAN
   mostrado no painel do Core. Para IP numérico, faça uma reserva no DHCP do
   roteador. O IP observado no diagnóstico de teste foi 192.168.68.105; ele não
   é um endereço fixo garantido.
5. Preserve a chave de vínculo já cadastrada. O painel do Core mostra
   `core_host` junto dos dados de vínculo. As antigas credenciais da rede
   privada podem continuar salvas para uso do modo anterior.
6. No Link, configure o endpoint
   `https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-ingest`
   e o token individual do Core entregue por canal privado. O token fica no
   Link; o UID enviado é o UID dos lotes do Core. Não use chave administrativa.
7. Salve e reinicie. Mantenha LTE desativado para este teste e microSD/acionamento
   de alimentação corrigido desativados enquanto a placa continuar original.

Não publique os tokens nem o resultado do comando USB `setup`.

## Nuvem já preparada

- `truckbox-ingest` foi atualizada da revisão 24 para 25 no Supabase, preservando
  autenticação por token individual e compatibilidade de entrada legada.
- RAW, DECODED e J1708_REQUEST_EVENT guardam o payload original em `raw` e sua
  classificação em `sample_kind`; colunas operacionais ficam nulas. Não alteram
  estado operacional, viagem ou consumo, inclusive nos triggers SQL existentes.
- HEALTH conserva também o registro original em `context.firmware_record`.
- Coordenadas ausentes continuam nulas. UTC válido, monotônico/boot e horário
  de recebimento mantêm a distinção de origem; não há posição GPS inventada.
- ACK só é retornado após persistência. Reenvios usam a unicidade device/seq.
  Lotes com registros/sequências inválidos são rejeitados integralmente.
- O UID físico deste Core foi cadastrado separadamente do UID legado, com token
  próprio, sem alterar os dispositivos antigos nem misturar suas sequências.
- Fonte integral do backend e credenciais ficam fora do Git/ZIP públicos.
  `cloud/patch_ingest_v25.py` e o diff mínimo permitem reproduzir a alteração.

## Verificação executada e critérios na placa

Compilação Core e Link: ESP-IDF 5.5.2, ESP32-C6, flash 8 MB, aplicações em slots
de 3 MiB. Suites nativas J1939/J1708, persistência/ACK, ausência de GPS e equidade
do decoder/gravador foram executadas; consulte os arquivos de evidência.

Testes adicionais executam o código C real de seleção/upload do Link e do cache
de endereço do Core com RTOS/rede simulados: Wi-Fi sem LTE/APN, interface TLS,
liberação das árvores JSON durante espera, ACK exato, rejeição de resposta inválida,
erro HTTP, modo LTE antigo e expiração/validação de endereços locais.

Teste HTTPS real da ingestão usou dispositivo temporário, sem sensores fictícios
no dispositivo real: recebeu três registros de teste sem GPS, confirmou seq 8,
reenviou sem duplicar, rejeitou token inválido (401) e sequência inválida (400).
Consulta ao banco confirmou apenas três linhas, com tipos/origem preservados e
coordenadas/colunas operacionais nulas. O dispositivo temporário foi removido.

Ainda precisam da placa: conexão RF/DHCP/mDNS, descoberta Core–Link, HTTPS no
ESP32, vazão com tráfego do caminhão, reconexão, pressão de memória durante TLS,
OTA e drenagem dos 1.793 registros observados na flash. Compilar/testar mocks
não comprova esses testes físicos. O sinal LAN observado era fraco (-94 dBm).

Após atualizar/configurar, os diagnósticos devem mostrar:

- Link: `network.station_mode=ROUTER`, `uplink.transport=WIFI` e HTTP 200 com ACKs.
- Core: `network.link_host` com o IP LAN do Link, ACKs crescentes e fila diminuindo.
- Cloud: registros reais desse UID, sequências coerentes e `raw` intacto.

Sem SD, a flash continua limitada a 24 segmentos de 48 KiB. O diagnóstico enviado
já tinha 24/24 e quatro recusas por lotação. A nuvem não amplia o armazenamento
durante falta de internet. Limpeza ou perda permitida pelo usuário não equivale a
sincronização concluída; só ACK de persistência libera o fluxo normal.

## Reproduzir

Ative ESP-IDF 5.5.2 e execute:

```sh
idf.py -B build-core -DTRUCKBOX_ROLE=core -DPROJECT_VER=0.2.2 build
idf.py -B build-link -DTRUCKBOX_ROLE=link -DPROJECT_VER=0.2.2 build
sh tests/run_all.sh
node cloud/test_ingest_mock.mjs --source /CAMINHO/PRIVADO/ingest-v25.ts --v25
python3 tools/build_release.py --output-dir /DESTINO/NOVO --package /DESTINO/Truckbox_0.2.2_TESTES.zip
```

Para voltar: use a imagem OTA correspondente da versão anterior arquivada.
Para o modo privado anterior, limpe somente o SSID externo do Link, mantenha as
credenciais `core_ssid/core_password` e a chave de vínculo. Não apague a NVS.
