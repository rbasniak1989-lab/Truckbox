# TruckBox Motorista v0.6.5 — Gateway e fila offline

Aplicativo de celular separado do TruckBox Multimídia.

## Funções desta versão

- serviço foreground de Gateway em segundo plano
- acessa o ESP32 pela LAN da Starlink
- busca lotes em `/api/gateway/batch`
- envia os lotes para `truckbox-ingest` por HTTPS no Android
- grava cada lote primeiro em SQLite persistente no Android, com transacao duravel
- confirma ao ESP apenas depois do commit no Android
- envia a fila Android para a Cloud em segundo plano; remove somente as amostras confirmadas pela Cloud
- retoma a fila após reiniciar o serviço/aparelho (desde que o Android execute o gateway)
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

## Fila offline v0.6.5

O fluxo agora tem **duas confirmacoes independentes**:

1. ESP32 -> Wi-Fi local -> SQLite Android (transacao e synchronous=FULL) -> ACK ESP32.
2. Fila persistente Android -> Internet/Cloud -> ACK da Cloud -> remocao somente das linhas confirmadas no Android.

Se a Starlink ficar sem internet, o ESP32 continua acessivel no Wi-Fi e o Android recebe e guarda as amostras. Na volta da internet, o envio e retomado sem precisar reconectar o Core. O app permite iniciar/calcular rotas normalmente (v0.6.3 incorporada).

### Condicoes de seguranca

- So existe ACK do ESP32 quando a gravacao no SQLite teve sucesso.
- Duplicatas sao tratadas por `(device_uid, seq)`; conflitos de conteudo param a coleta para evitar descarte indevido.
- UID do Core precisa bater com o UID da Cloud configurado. O token so e exigido para transmitir a Cloud, nunca para guardar offline.
- Cada lote enviado a Cloud so e removido do SQLite depois da confirmacao do `ack_seq`.
- Limite da fila Android: **512 MiB** de dados JSON pendentes, com reserva de **64 MiB** de espaco livre. Quando nao ha espaco, o Android **nao confirma o Core**; a retencao volta para o ESP32.
- A fila fica no armazenamento **privado** do TruckBox Motorista. **Nao desinstale nem limpe os dados do aplicativo** enquanto houver amostras pendentes; isso apaga a fila. Recomenda-se desativar otimizacoes agressivas de bateria para permitir execucao prolongada em segundo plano.
- Sem internet E sem acesso local Wi-Fi ao ESP32, a coleta para e o Core retoma sua propria fila temporaria.

**Importante:** os arquivos guardados pelo app **TruckBox Recovery separado** nao sao migrados automaticamente para o Motorista. Esta melhoria so vale para dados coletados pelo Motorista v0.6.5 em diante.

Ver `OFFLINE_SPOOL_TEST_PLAN.md` para o roteiro de validacao antes de distribuir o APK.
