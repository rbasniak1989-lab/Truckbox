# TruckBox Next — Core 0.2.26, torque e central de falhas

**Entrega:** APK separado `br.com.truckbox.multimedia.next`, versão `0.2.1-core026-falhas` (código 4).
**Origem:** base aprovada Black & Orange da branch `codex/multimedia-next-auditoria`.
**Escopo:** Android apenas. O Core, Link, dados da viagem e Supabase não são alterados.

## Mudanças feitas

- HOME conserva **13 cartões 3+4+6**, primeira linha pressões e torque em **%** no último cartão; as demais quatro páginas aprovadas permanecem.
- Core `/api/live`: acrescenta marcha **MID130/PID163**, torque **percentual derivado de MID128/PID93** e pressão do combustível PID94 como complemento aos sensores paginados. Confere UID, origem, PID/MID, idade e qualidade antes de exibir. Torque usa **2.643 Nm de referência histórica**, não é % direto medido na ECU, e permanece **em validação**. TTL do torque 65 s, marcha 5 s, combustível 15 s.
- Core `/api/diagnostics?offset=...&limit=8` (GET somente leitura): varredura paginada de até 32 posições a cada 15 s, sem enviar comandos ou mexer nas requisições J1708; guarda último estado conhecido por UID.
- **Home / Monitoramento e alertas**: mostra código DTC e última condição ativa, diferenciando leitura recente de estado atual desconhecido. Tocar leva a **Caminhão → Geral**.
- **Caminhão → Geral** centraliza **Ativas / Inativas / Histórico** e modal com MID, SID/PID, FMI, origem, primeira/última observação, ocorrências, qualidade. Não inventa descrição, gravidade crítica ou horário de inativação.
- Perda de Wi‑Fi, silêncio da ECU, reboot e ausência em outra página **não desativam** falha. Inatividade só aparece com estado explícito recebido.
- Dados de viagem, média e consumo continuam com o canal Cloud autenticado e independentemente das leituras locais.
- App usa `applicationId` distinto do experimental `br.com.truckbox.multimedia`: pode instalar lado a lado, sem desinstalar o antigo. Build exige Android **8 ou superior (API 26)**. APK de homologação tem assinatura debug da CI; não garante atualização sobre outra assinatura do próprio Next.

## Limitações reais

- O Core publica cache de DTCs SAE/J1587, com qualidade `SAE_PARSED_OEM_UNCONFIRMED`. Não é diagnóstico OEM completo validado nem prova elétrica externa. A Home não substitui os alarmes do painel do Volvo.
- O Core conserva até 32 ocorrências; sem backend próprio de episódios não prometemos histórico vitalício ou horário de inativação retrospectivo.
- A leitura do torque percentual depende de uma referência histórica de 2.643 Nm e pode ser negativa em freio motor; nunca utilizar como parâmetro de manutenção sem validação.
- O novo aplicativo não muda contrato financeiro/Jornada; sua exibição segue autorização remota. Se a Cloud ainda não estiver configurada/autorizada, viagem e consumo aparecem indisponíveis.
- Instalação e teste no Android 13” são pendentes; compilação CI não comprova funcionamento físico.
- Sem mudança de firmware, partições, flash, dados históricos, PIDs ou frequência de requisições.

## Verificações de homologação

1. Com Core 0.2.26, conferir marcha 11→12 e torque em %, identificação da fonte e idade; sem sinal, exibir última leitura/sem leitura conforme qualidade.
2. Com DTC real do PID194, confirmar alerta na Home, toque para Geral, detalhe com código e horas reais.
3. Simular perda Wi‑Fi: preservar DTC reportado, trocar status para não confirmado; nunca marcar inativo.
4. Receber DTC com `state=INACTIVE`: migrar apenas aquele código para Inativas; reenvio idêntico não duplica.
5. Confirmar Cloud de viagem/consumo independente e nenhuma operação de OTA, configuração, limpeza de ECU ou cadastro na tela.
6. Instalar o APK `multimediaNext-homolog-debug.apk` lado a lado com o experimental se ambos os pacotes forem compatíveis com o Android.

**Não usar o APK demo** para medir valores do caminhão. Sempre identificar a variante `homolog`.
