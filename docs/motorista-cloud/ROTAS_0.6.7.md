# TruckBox Motorista 0.6.7

25 de novembro de 2026 · Android 8 ou superior · versionCode 67

## Base da atualização

O repositório Truckbox tinha o Motorista 0.6.4 na `main`, o 0.6.6 na branch
`fix/motorista-spool-malformed-batches-v066` e a revisão Cloud/GPS de 15/11
na branch `codex/motorista-cloud-gps` (commit `03366a72e962ad488ec43ed594adaacbc25cb90c`,
rotulada 0.6.5-cloud-gps). Esta última é a base por incorporar a sincronização
de viagens e a fila persistente do GPS do celular em um app que usa a Cloud.
A revisão 0.6.6 se concentrou na recuperação da fila do gateway de telemetria
do Core. O envio da telemetria do Core é feito pelo Link.

## Changelog

- O Motorista consulta a Cloud ao abrir, antes de transmitir a fila
  operacional. Identifica a viagem ativa pelo estado operacional e pelo ID
  registrados na Cloud.
- A ficha da viagem apresenta dados recebidos, incluindo início, carga,
  frete por tonelada e bruto, odômetro, distância, consumo e trecho vazio
  anterior, conforme disponíveis.
- Um início offline que conflita com outra viagem ativa fica guardado para
  revisão. Novas tentativas de envio reconhecem operações de viagem já
  confirmadas pelo identificador. A fila mantém os dados da operação original.
- A seção **Rota da viagem** permite calcular, selecionar, vincular,
  visualizar, alterar e recalcular a partir de uma posição recente.
  A vinculação exige confirmação e associa a proposta ao ID da viagem
  atualmente ativa. O backend grava a associação em `truckbox_trip_v1.bindings`.
- O cálculo HERE usa perfil de caminhão, 25 m, nove eixos e as dimensões,
  PBTC e peso bruto atual informados pelo motorista. A visualização distingue
  rota completa de rota restante, mostra a geometria e exibe os pedágios e
  os valores disponibilizados pelo provedor.
- O simulador de próxima rota e o início manual continuam acessíveis.
- O serviço GPS continua usando uma notificação de localização e uma fila
  SQLite de posições. O pedido de localização precisa no Android inclui
  também a permissão aproximada para permitir a revisão dessa escolha.

## Instalação e autorização

1. Instale **TruckBox Motorista Rotas** (`br.com.truckbox.driver.rotas`).
   Essa edição pode ficar instalada ao lado do Motorista anterior e tem
   configurações e fila próprias. Mantenha o aplicativo anterior e seus
   registros locais.
2. No novo app, abra **Config → Acesso à Cloud**, cole o código privado do
   Motorista já cadastrado para o seu caminhão e toque em **Salvar acesso**.
   Em **Status**, confira **Resposta recebida da Cloud**.
3. Em **Config**, ative **Compartilhar GPS com a Cloud** e toque em
   **Iniciar GPS / Revisar permissões**. Permita localização precisa e
   notificações.
4. Com localização e internet disponíveis no celular, confira em **Status**
   o horário da última posição confirmada. Se houver dois Motoristas
   instalados, pause o compartilhamento na versão anterior depois de
   confirmar o envio na edição Rotas, mantendo os dados desse app.

O build para o pacote original `br.com.truckbox.driver` foi compilado para
verificação. Uma atualização sobre uma instalação existente exige a chave
que assinou aquele APK. A chave de build disponível é diferente das
assinaturas anteriores verificadas. O APK de instalação ao lado usa a
assinatura de desenvolvimento deste build.

## Vincular à viagem ativa

1. Abra **Viagem → Viagem Atual** e confira a origem, o destino e o horário
   da viagem carregada da Cloud.
2. Em **Rota da viagem**, toque em **Calcular rota** e informe as dimensões
   medidas, o PBTC permitido e o peso bruto atual do conjunto, com a carga
   e demais pesos a bordo. Para calcular apenas o trecho restante, escolha
   **Partir da posição atual**.
3. Revise o traçado, a distância, a duração e os pedágios. **Selecionar rota**
   também permite escolher entre planos disponíveis na Cloud.
4. Toque em **Vincular à viagem atual** e confirme **Vincular**. A confirmação
   da Cloud aparece na tela. Com uma rota vinculada, **Visualizar rota**
   abre os detalhes e **Alterar rota** permite escolher outra proposta.
5. Na Multimídia com suporte a rotas, mantenha **Viagem Atual** conectada à
   Cloud. A consulta de dados complementares lê a mesma associação de rota.

## Contratos e disponibilidade

- O Motorista usa `truckbox-trip-v1` para `plans`, `plan_truck` e
  `attach`. O vínculo é feito separadamente dos comandos de início e
  encerramento. O histórico percorrido permanece nos dados de GPS e
  telemetria.
- A Multimídia lê `truckbox_trip_route_v1` por
  `truckbox_multimedia_trip_session_extras_v1` com a autorização da
  central. O mesmo contrato entrega o traçado e os pedágios disponíveis.
- O recálculo pelo GPS requer uma posição `ANDROID_PHONE` com precisão de
  até 75 m registrada nos últimos 90 segundos na Cloud.
- A vinculação exige conexão para confirmar que a viagem ainda está ativa.
  O GPS mantém a fila de posições durante interrupções de internet.
- O contrato HERE retorna uma proposta por solicitação. O seletor apresenta
  as propostas já disponíveis, inclusive o plano anterior enquanto estiver
  válido. Planos Google antigos são identificados com perfil carro.
- A seção exibe a estimativa de combustível do plano original quando ela
  está registrada, ou estima pela média medida da viagem quando disponível.
  Altimetria de planos antigos depende dos dados da simulação. O HERE atual
  fornece traçado, duração, distância e pedágios, com cobertura variável
  para as tarifas.

## Verificações

Os testes de código usam dados sintéticos. As conferências em produção foram
leituras do estado operacional, da viagem, do contrato de rotas, do estado do
GPS e da prontidão da função HERE.

| Verificação | Resultado |
| --- | --- |
| JVM e Robolectric em Android 13 e 15 | 47 testes, zero falhas e zero ignorados |
| Fluxo da rota na interface | Seleção, confirmação, leitura do vínculo e traçado sem reiniciar o app |
| Sincronização operacional | Viagem externa prevalece, dados do trecho vazio são preservados e início conflitante fica na fila |
| GPS e permissões | Captura inválida ou antiga rejeitada, fila persistente, reenvio e deduplicação, tratamento de permissão negada |
| Cloud em produção | Viagem ativa conferida em leitura, HTTP 200 na saúde do roteamento HERE e consultas periódicas da central ao contrato compartilhado |
| Android Lint | Zero erros e 28 avisos |
| APK de testes instrumentados | Compilado. A execução em dispositivo depende de um aparelho conectado |
| APK de instalação | Pacote, versões, permissões, assinatura v2 e integridade verificados. Varredura do binário sem credenciais de servidor ou código de acesso embutido |

O APK `TruckBox-Motorista-0.6.7-Rotas.apk` tem 18.894.663 bytes.

- SHA-256 do APK `e883ff2b121b3a6176b3d63fc6b41e181c41322f7f621c82ded5357d07956777`
- SHA-256 do certificado `3ab267aee6aaa5c3e32afa446487836a8fe8a5a7143315fdad19a9c2c8734f90`

Comandos de verificação no checkout:

```sh
gradle --no-daemon --max-workers=1 :app:testDebugUnitTest :app:lintDebug :app:assembleCloudTest :app:assembleDebug
python3 scripts/check_driver_cloud_apk.py app/build/outputs/apk/cloudTest/app-cloudTest.apk
apksigner verify --print-certs app/build/outputs/apk/cloudTest/app-cloudTest.apk
```

Para homologação em campo, use a notificação ativa do GPS e confira as
posições confirmadas após apagar a tela, abrir outro app e recuperar a
internet. A execução desta entrega cobre a fila e a interface em ambiente
de teste. Tela apagada, bateria e atualização do mapa na central instalada
dependem da conferência no aparelho. Inicie o serviço pelo Motorista aberto,
com a localização habilitada no Android. [Regras do serviço de localização](https://developer.android.com/develop/background-work/services/fgs/service-types#location)
