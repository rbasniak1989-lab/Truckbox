# TruckBox Motorista v0.6.1 — Android Gateway

Aplicativo de celular separado do TruckBox Multimídia.

## Funções desta versão

- serviço foreground de Gateway em segundo plano
- acessa o ESP32 pela LAN da Starlink
- busca lotes em `/api/gateway/batch`
- envia os lotes para `truckbox-ingest` por HTTPS no Android
- confirma o lote no ESP apenas depois do `ack_seq` da Cloud
- GPS Android integrado
- envia GPS para o ESP (`/api/gps`) e para a Cloud (`truckbox-gps`)
- cadastro de viagem
- encerramento de viagem
- abastecimento
- despesa
- manutenção
- sincronização operacional periódica com a Cloud
- app não força paisagem e não precisa ficar com a tela ligada

## Instalação lado a lado

`applicationId = br.com.truckbox.driver`

O TruckBox Multimídia continuará com outro applicationId e poderá ser instalado no mesmo ecossistema sem transformar a multimídia em gateway.

## Configuração

Na aba Config:

1. Core na Starlink: `truckbox.local` ou IP LAN do ESP.
2. Device UID Core: UID do dispositivo de telemetria.
3. Token Cloud Core: credencial do mesmo dispositivo.
4. Device UID GPS: dispositivo separado de GPS.
5. Token Cloud GPS: credencial do dispositivo GPS.

Tokens não estão incluídos neste pacote-fonte.

## Serviço em segundo plano

O Android pede localização e inicia `TruckBoxGatewayService` como foreground service. A notificação mostra o estado geral.

A multimídia pode permanecer desligada ou ser usada para outra função sem interromper o envio.

## Integridade da fila

O fluxo é transacional por ACK:

ESP -> Android -> Cloud -> ACK Cloud -> Android -> ACK ESP

Se a Cloud receber e o ACK local falhar, o ESP mantém o lote. O Android o envia novamente; o `seq` permite idempotência na Cloud.
