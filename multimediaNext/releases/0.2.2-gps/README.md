# TruckBox Next 0.2.2 com GPS

Este APK parte da **Multimídia Next `0.2.1-core026-falhas` Black & Orange** do commit `2a6e56920149a54eb432e1635dea28ef403a6e2c`. A Home conserva os 13 instrumentos e o acesso aos DTCs. A coleta do GPS da central foi acrescentada em **Configurações → Conexões**. [APK](TruckBox_Next_0.2.2_GPS.apk) · [SHA-256](SHA256SUMS.txt)

## Instalação

1. Abra o APK em um Android API 26 ou superior. A instalação cria **TruckBox Next · GPS** com o pacote `br.com.truckbox.multimedia.next.gps` ao lado da Next anterior. O APK tem assinatura de homologação gerada pelo GitHub Actions.
2. Em **Configurações → Conexões**, configure o host local do Core. O IP mais recente informado para a rede Starlink do Volvo foi `192.168.1.251`. A leitura de viagens pode exigir reativar o vínculo da Cloud, que usa uma identidade própria para cada instalação Android.
3. Na mesma aba, abra **GPS da multimídia**. Use o ID `TBX-FH540-GPS01` e o token correspondente. Toque em **Ativar GPS para a Cloud** e conceda a localização precisa. Com uma leitura válida e internet, a indicação **Cloud confirmou o envio** mostra a primeira confirmação.

O Android guarda até 4.320 posições para reenvio em caso de interrupção. O serviço registra até uma leitura a cada 10 s, mantém o horário da captura e usa o mesmo identificador quando precisa repetir o envio. As posições chegam às tabelas de GPS da Cloud com a origem `ANDROID_MULTIMEDIA`. Após reiniciar a central, abra o app para retomar o compartilhamento. A visualização de mapas continua dependente da configuração do fornecedor de mapas da Next.

## Verificação

As cinco telas de referência em 1920×1080 são idênticas pixel a pixel às da base `0.2.1-core026-falhas`. A captura adicional de **Configurações → Conexões** mostra o controle do GPS. Os testes de interface e lint passaram, junto com os ensaios de contrato de marcha, torque e DTC, fila SQLite e ACK GPS. O primeiro envio real e as falhas ativas no Volvo dependem da conexão local da central. [Build Android e relatórios](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/45063694022)
