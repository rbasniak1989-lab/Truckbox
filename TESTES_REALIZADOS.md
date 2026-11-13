# Testes, evidências e limitações — TruckBox Next

Versão: 0.1.0-homologacao, Android min 26 / target 36. Base preservada: `06a9309d104367a49c88167d8f2b70bd7c1ffd1e`. Data: 13/11/2026 UTC.

**Rodada local concluída:** APKs homolog, demo e instrumentação compilados; 35 testes JVM/Compose aprovados, zero falhas/skips; 13 casos da API proposta e TypeScript strict aprovados; lint com zero erros e 10 avisos. Fonte verificada: commit `274865c3b70f433f215365e1c787df161f7ec7d0`. Não houve instalação em multimídia, acesso a ECU/Core real, deploy Cloud ou execução de migração. PASSOU abaixo identifica a camada local; não substitui aceite de campo.

## Escopo efetivamente executado

- Build dos APKs homolog, demo e teste instrumentado em JDK 21 completo, Gradle 9.4.1 e SDK 36.
- 24 testes de regras/DTOs e um de persistência local de aparência aprovados na rodada final.
- 10 testes nativos Compose/Robolectric SDK 35 aprovados, incluindo quatro matrizes, drawer, revogação, central de falhas e capturas. Os ajustes de contraste e dimensões foram conferidos nas capturas finais.
- 13 casos da biblioteca de API proposta executados localmente com Node 24; TypeScript 5.9.3 em modo estrito. Não usam Supabase nem simulam uma implantação existente.
- Capturas: cinco áreas × cinco resoluções mdpi = 25 PNG, mais dois estados opcionais explicitamente marcados TEST-DEMO. A captura desenha a View Android em Canvas nativo; não usa HTML, WebView, imagem do protótipo ou navegador. Não comprova desempenho/PixelCopy/imersivo em hardware.
- Scanner de credenciais nos novos fontes e DEX/XML dos APKs; checagem de ausência de PreviewData no APK homolog; assinatura v2 e applicationId por apksigner/aapt.
- Tentativa `adb devices -l` fora do sandbox: lista vazia. Sem aparelho, emulador ou `/dev/kvm`; execução instrumentada e toque físico bloqueados.
- `connectedHomologDebugAndroidTest` foi tentado e terminou com falha de resolução do executor UTP (HTTP 503 ao baixar `io.netty:netty-codec-http:4.1.110.Final` pelo repositório de dependências). Nenhum teste foi executado em dispositivo. A ausência de hardware permanece bloqueio independente.
- Após a indisponibilidade temporária do ambiente, foram recuperados os mesmos fontes, 35 resultados aprovados, 27 PNG e três APKs. Os hashes conferiram; não foi necessário recompilar fontes inalterados.

## Matriz T01–T50 do pacote

| ID | Resultado / camada | Evidência e limite |
|---|---|---|
| T01 | BLOQUEADO | IDs distintos no Gradle/APK; instalação simultânea em Android não executada. |
| T02 | PASSOU local | Captura nativa 1024×460 com rolagem; não é Chrome/WebView. |
| T03 | PASSOU local | Capturas 1024×600, 1280×800, 1672×941 e 1920×1080; densidade mdpi. Hardware e outras densidades pendentes. |
| T04 | PASSOU local | NativeUiTest abre drawer inicialmente fechado, seleciona área e verifica fechamento. |
| T05 | PASSOU local | DomainRulesTest verifica ordem exata dos 13; NativeUiTest verifica cartões; ar duplo em um cartão. |
| T06 | BLOQUEADO | Personalização por usuário/veículo e persistência Cloud não implementadas sem identidade segura; UI informa bloqueio. |
| T07 | PASSOU regra local | Edição exige parada atual verificada e autorização; velocidade ausente/antiga/movimento bloqueiam. No APK, edição permanece indisponível. |
| T08 | PASSOU local | CoreContractTest impede promover pressão recebida sem metadados a leitura atual; histórico aninhado não é fallback. |
| T09 | NÃO TESTADO automático | Só Efficiency usa fade/rotação, botões pausa/avanço implementados; ciclo temporal de 14s e distração em campo não medidos. |
| T10 | BLOQUEADO integração | Estados Core/Cloud independentes e sem falso conectado na estrutura local. Perda de rede técnica real ainda não executada. |
| T11 | PASSOU regras locais | Domínio e API proposta calculam etapas carregada/vazia separadamente; backend real pendente. |
| T12 | PASSOU regra local | Parado usa L/h, movimento km/l e vazão inválida não fabrica média; medição de lenta física pendente. |
| T13 | PASSOU regra local | BOTH_FULL explicitamente confirmado inicia 720 L no exemplo; um tanque só não calibra. Não persistido na Cloud. |
| T14 | PASSOU regra local | Ledger 720−150=570 L. |
| T15 | PASSOU regra local | Parcial +100 resulta 670 L; não recalibra. |
| T16 | PASSOU regra local | Mais 20 L, incluindo lenta, resulta 650 L. |
| T17 | PASSOU regra local | Boia/PID de nível não é entrada do ledger; não há correção por nível. |
| T18 | BLOQUEADO | Replay/out-of-order/duplicata e reconstrução da função testados; persistência/transações Core/Gateway/Cloud/reboot reais ausentes. |
| T19 | PASSOU estado sem dados | Previsão indisponível sem rota/relevo; nenhum consumo futuro fictício. |
| T20 | PASSOU regra local parcial | Etapas isoladas; API proposta elimina receita de vazio, mesmo que fonte de teste carregue receita anterior. Operação real pendente. |
| T21 | BLOQUEADO | GPS real/antigo não integrado; mapa informa indisponível sem mover marcador fictício. |
| T22 | BLOQUEADO | Detecção geográfica não implementada; UI não chama passagem de pagamento. |
| T23 | BLOQUEADO | Agregação idempotente por passage_key testada; processamento de trilha GPS e passagem reais ausentes. |
| T24 | PASSOU regra local | Revisão IDENTIFIED substitui FORECAST da mesma chave; replay não soma duas vezes. |
| T25 | PASSOU regra local | OWNER conta uma vez, CARRIER não reduz custo próprio; reembolso separado. Sem tarifa/licença real. |
| T26 | BLOQUEADO | Nenhum vínculo automático novo; confirmação no Motorista e contrato Cloud ainda necessários. Reconciliação antiga identificada na auditoria. |
| T27 | PASSOU local / BLOQUEADO Cloud | Financeiro negado e Jornada liberada em regra, composição e handler de referência; API implantada não corrigida. |
| T28 | PASSOU local / BLOQUEADO Cloud | Financeiro liberado e Jornada negada, independentes; ausência do botão/modal em Compose. |
| T29 | PASSOU local / BLOQUEADO Cloud | Ambos liberados em testes; dados exclusivamente fixtures de teste identificadas. |
| T30 | PASSOU local / BLOQUEADO Cloud | Ambos negados por padrão homolog; cartão/chip inexistentes na composição. |
| T31 | PASSOU local / BLOQUEADO Cloud | PolicySession apaga visões, rejeita versão/resposta tardia; UI fecha modal; handler reautoriza após fetch. Revogação real não implantada. |
| T32 | PASSOU regra local | Desconexão, expiração e relógio antes da emissão negam e limpam dados; nenhum cache protegido persistente. |
| T33 | PASSOU regra local | Tenant, veículo, dispositivo, usuário e sessão distintos não compartilham visões/políticas. Login real pendente. |
| T34 | BLOQUEADO | Contrato ACK proposto, não implementado/implantado. Nenhuma UI afirma política aplicada. |
| T35 | BLOQUEADO | Soma 24h, parado subtotal, lacunas e conflitos testados; segmentos, estado atual, última parada e confirmação Motorista reais pendentes. |
| T36 | PASSOU regra local | Descanso não confirmado não é contado como descanso legal. Revisão jurídica e classificação real pendentes. |
| T37 | PASSOU local | Geral é primeira; NativeUiTest confirma central de falhas só nela. |
| T38 | PASSOU estrutura local | IA → sensores → estatísticas → expansão; NativeUiTest abre Motor e expande. Nenhum mini gráfico. |
| T39 | NÃO TESTADO com série real | Seletor e janelas implementados; viagem requer legStartedAt. Recalcular com captura real permanece pendente. |
| T40 | PASSOU regra local | Média temporal irregular, deduplicação, qualidade, lacunas e conflitos; extremos observados. |
| T41 | PASSOU regra / BLOQUEADO ECU | Perda muda estado atual a UNKNOWN sem deactivatedAt; captura ECU inexistente nesta sessão. |
| T42 | PASSOU regra / BLOQUEADO ECU | Inativação exige confirmação e horário posterior à última presença; API/ECU reais pendentes. |
| T43 | BLOQUEADO | Modelo mantém IDs de episódios distintos; armazenamento/reincidência DTC real não implementados. |
| T44 | PASSOU estado sem dados | Central informa captura indisponível; nenhum DTC/código/horário do protótipo entra no APK. |
| T45 | PASSOU estado sem dados | “Análise indisponível / Dados insuficientes”; nenhum laudo gerado ou score arbitrário. |
| T46 | PASSOU local | Configurações mostra estados de leitura, sem chave Financeiro/Jornada. |
| T47 | PASSOU regra / BLOQUEADO hardware | Limites 18–6 São Paulo e override testados; brilho físico 12% e leitura noturna não verificados. |
| T48 | PASSOU build / BLOQUEADO instalação | APKs gerados, IDs/assinatura/scanner verificados; instalação física ainda não realizada. |
| T49 | BLOQUEADO | Fonte do Gateway preservada e nova app não envia lote/ACK; teste com multimídia desligada exige ecossistema real. |
| T50 | PASSOU processo local | Plano aditivo/rollback documentado; nenhuma migração, deploy, merge ou alteração de produção executada. |

## Itens não funcionais / não liberados neste incremento

Provisionamento/login e política Cloud real; API protegida implantada e correção dos caminhos legados públicos/RPC; personalização do painel por usuário/veículo; ledger autoritativo persistido/transacional; viagem/rota/mapa/GPS/ETA; identificação e cobrança de pedágios, tarifas e reembolso; confirmação de vazio; episódios DTC; análises reais de IA; classificação e revisão legal de Jornada; atualização automática assinada/rollback remoto; testes físicos 4G/SD/GPS Rev.B.1 e Android 13”.

Telas/contratos ausentes mostram indisponibilidade. As funções puras e fixtures dos testes não provam persistência, fornecedor licenciado, pagamento, ECU ou política remota já existente. O novo app não consome os endpoints legados que expõem objetos financeiros/de jornada.

## Falhas encontradas e corrigidas durante verificação

Ambiente inicial sem javac; JDK local e CA do ambiente configurados mantendo TLS. Runtime Robolectric precisou receber proxy e diretório próprio; qualificadores Android de viewport foram corrigidos. PixelCopy sem janela física foi substituído por desenho da View em Canvas nativo. Capturas permitiram identificar contraste inadequado do cabeçalho e ajustar tema e dimensões; a rodada final confirma a correção. Lint exigiu includeSubdomains explícito e SdkSuppress nos testes; sem baseline para ocultar erros.

Lint final: zero erros e 10 avisos — OldTargetApi (1), GradleDependency (3), NewerVersionAvailable (3), ModifierParameter (2) e DiscouragedApi para orientação em Android 16+ (1). SDK 37/mudanças de plataforma não foram adotados silenciosamente sem avaliar hardware e o contrato atual.

## Artefatos verificados

| APK | SHA-256 |
|---|---|
| multimediaNext-homolog-debug.apk | `3773df046a928e3f7f301788b9e10887b69fd0eb20ef9501cfb014a0e120b61d` |
| multimediaNext-demo-debug.apk | `d898e7329e6ca7e16ebc8c2fd4f9a9d1ee346973db06a4707a13a57d26b4a8e1` |
| multimediaNext-homolog-debug-androidTest.apk | `d608b7781e3a55cda96233650c5380957deed733cd5f0af66215eae9b9c0ff7a` |

Homolog: `br.com.truckbox.multimedia.next`; demo: `br.com.truckbox.multimedia.next.demo`. Assinatura debug v2 verificada. Os APKs não incluem credenciais de produção, e o homolog não contém PreviewData. As capturas de módulos opcionais usam fixtures identificadas como TEST-DEMO.
