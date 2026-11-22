# TruckBox Multimídia 0.3.3 com GPS

O APK lê o receptor GPS da central Android e envia os pontos à TruckBox Cloud. Ele usa o cadastro GPS do caminhão e mantém a leitura dos sensores no Core. A instalação cria **TruckBox Multimídia GPS** ao lado da versão anterior, com identidade Android própria.

## Ativação na central

1. Instale [Truckbox_Multimedia_0.3.3_RevB1_GPS.apk](Truckbox_Multimedia_0.3.3_RevB1_GPS.apk) e abra **TruckBox Multimídia GPS**. Em **Config**, ajuste o host do Core para a rede atual. Na rede Starlink do Volvo, o último IP informado do Core foi `192.168.1.251`.
2. Em **Config → GPS da multimídia**, informe o ID e o token do cadastro GPS. O ID do Volvo já vem sugerido como `TBX-FH540-GPS01`. Use o token GPS que corresponde a esse ID.
3. Toque em **Ativar GPS para a Cloud** e conceda localização precisa. Com um sinal válido e internet, a tela mostra **Cloud confirmou o envio** e a hora da última confirmação.
4. O serviço de GPS pode continuar ativo enquanto outro aplicativo está em primeiro plano. Depois de ligar a central ou se o Android encerrar o processo, abra o APK novamente para retomar a opção gravada. A ação **Parar compartilhamento** fica em Config e na notificação quando a central permite notificações.

A coleta aceita até uma posição a cada 10 s. Durante queda de internet, os pontos ficam no Android com o horário original. A fila mantém até 4.320 posições e reenvia com o mesmo identificador após voltar a conexão. Na Cloud, a função `truckbox-gps` grava a fonte `ANDROID_MULTIMEDIA` em `gps_samples` e atualiza `gps_latest`. A consulta de sensores e viagens continua pelo fluxo de telemetria já instalado. O mapa que lê apenas `telemetry_latest` precisa consultar a fonte de GPS para exibir essas posições.

## Evidência

Código compilado de `dd0c687631653df8d710dac3243fe65e1450f7ae`. Os testes cobrem horário e identidade preservados, invalidação de fix antigo, campos ausentes, ACK de outra posição, deduplicação e retomada de registros no SQLite. O contrato da função `truckbox-gps` v4 foi exercitado com dispositivos e posições fictícios em um simulador local. A leitura GPS e a confirmação com as credenciais do Volvo dependem de validação na central. [Build e testes Android](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/45052058838) · [SHA-256 do APK](SHA256SUMS.txt)
