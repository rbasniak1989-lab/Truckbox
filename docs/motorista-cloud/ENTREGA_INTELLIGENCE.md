# TruckBox Intelligence · entrega dos aplicativos

## Versões

| Aplicativo | Versão | versionCode | Pacote de instalação |
| --- | --- | --- | --- |
| Motorista Rotas | 0.6.8 | 68 | `br.com.truckbox.driver.rotas` |
| Multimídia Next | 0.2.5-inteligencia | 8 | `br.com.truckbox.multimedia.next.gps.consumo.tempo` |

A base do Motorista é `d1fefbf46f09a013b95c8e8410ae1396fa9b8be9`, na branch `feat/motorista-rota-viagem`. A Multimídia usa a revisão 0.2.4 entregue localmente na branch `feat/revb1-live-sensors-next` sobre `2fac8c5ba9a4de85e71808c24a530d31e545861f`, registrada no commit `1dd3f1625a9d77974c6d0bc3fc933d74662f4db4`. A auditoria conferiu branches remotas, artefatos de build e os checkouts locais. Os pacotes mantêm os identificadores dessas edições para atualização sobre APKs de mesmo certificado.

## Changelog

### Multimídia

- Consome `fuelRange` no snapshot assinado, com saldo, capacidade, percentual, qualidade e hora da fonte ou da consulta. Exibe a autonomia convencional da Cloud com a origem da média. A estimativa de tanque usa a metodologia que já está no Supabase.
- Mostra vazão local em L/h, consumo em km/L em movimento com velocidade e vazão recentes, qualidade provisória quando aplicável e corte confirmado. A janela completa de 10 s fornece vazão média ponderada em L/h quando a amostra instantânea falta. A média e a idade ficam visíveis.
- Mostra evolução por intervalos válidos, com últimos 10, 50 e 100 km, etapa observada e blocos de 50 km. O fallback coleta deltas do contador integrado local com odômetro. Com cobertura integral e velocidade válida na mesma janela, a central também pode estimar a distância daquela janela. A série local começa com as observações do app. Falhas de cobertura ficam fora da média.
- Lê histórico e comparação topográfica quando publicados no snapshot. Calcula a média exibida do intervalo com a soma da distância coberta dividida pela soma dos litros do mesmo conjunto.
- Exibe traçado, posição, pedágios e progresso quando entregues pelo canal autenticado. O perfil de elevação, os cartões de 50 km, 100 km e destino, a autonomia inteligente e os alertas vêm de previsões cuja viagem, rota, revisão e método coincidam.
- Mantém os últimos dados operacionais em memória durante interrupções de rede, com idade e validade. A apresentação financeira obedece à política de acesso e não é retida offline.

### Motorista

- Solicita uma rota principal e até três alternativas pelo contrato existente. Com múltiplos planos na resposta, desenha todos no mesmo quadro e permite escolher a opção a vincular. Explica a resposta com uma única rota.
- Recebe rodovias, perfil de elevação, pedágios e previsões independentes por plano. Mostra menor distância e tempo. Destaca menor consumo e custo somente quando a comparação tem dados suficientes. A escolha de pagador faz parte da consulta.
- Expõe pontos intermediários quando o backend anuncia suporte. Valida a confirmação dos pontos aplicados antes de oferecer os resultados como percurso personalizado.
- Retira a projeção a partir da média da viagem em andamento. As previsões de novas rotas aceitam o método por relevo e histórico. O registro de previsão original continua disponível como histórico.
- Mantém a vinculação explícita pelo ID da viagem ativa, com a mesma fila, GPS e credencial da edição anterior.

## Testes e limites da homologação

O Motorista passou por 49 testes e a Multimídia por 111, ambos sem falhas
ou casos ignorados. As suítes usam JUnit, Robolectric e testes Compose.
Os builds de referência foram executados com JDK 21, Gradle 9.4.1
e Android SDK 36.

```sh
gradle :app:testDebugUnitTest :app:lintDebug :app:assembleCloudTest
gradle :multimediaNext:testHomologDebugUnitTest :multimediaNext:lintHomologDebug :multimediaNext:assembleHomologDebug
```

| Cenário | Verificação |
| --- | --- |
| Tanque e autonomia | Contrato publicado conferido em leitura. Teste de DTO preserva o valor da Cloud sem reestimá-lo pela média exibida. |
| Consumo parado, em movimento, corte e perda de internet | Testes do fluxo local, qualidade provisória, dados vencidos e janela completa de 10 s. |
| Séries e blocos de 50 km | Testes de diferenças de contador, reinício, lacuna, limite dos blocos, velocidade integrada e deduplicação de janelas. |
| Alternativas e custo | Testes de resposta antiga com uma rota e múltiplas alternativas com previsões independentes, custo completo/parcial e pontos intermediários. As respostas ampliadas são fixtures. |
| Vinculação à viagem ativa | Regressão da seleção, confirmação e leitura do vínculo na interface do Motorista com backend de teste. O caso real foi somente lido. |
| Progresso, horizonte e falta de relevo | Testes de identificação de rota, revisões diferentes, recorte pela distância restante e rejeição de projeção legada. Sem perfil e histórico válidos não há estimativa topográfica. |
| Média baixa em serra | Caso de teste passa média realizada de 0,80 km/L, duas previsões independentes e um plano legado. Os dois planos topográficos mantêm seus respectivos litros e a projeção legada é rejeitada. O treinamento real depende do Cloud. |
| Offline e autorização | Testes de preservação do saldo na perda de rede, expiração, logout e remoção de dados protegidos. |
| Contrato publicado | Os endpoints de consulta operacional e planos do Motorista responderam HTTP 200 no veículo em leitura. A resposta real tinha uma rota HERE, geometria e ausência de previsão e de elevação. O snapshot de campo já publica `fuelRange`. |

Os APKs foram compilados para Android 8 ou posterior. A validação física na central de 13 polegadas, a comparação com o caminhão em movimento e a troca de rota com previsão em produção dependem do aparelho e das projeções da Cloud. Nenhuma consulta de teste solicitou uma nova rota para a viagem em andamento.

## Dependências na Cloud

- Retornar a principal e as alternativas completas no `truckbox-trip-v1` e anunciar suporte a pontos intermediários.
- Publicar rota, progresso, caminho observado, histórico revisado e `routeForecast` no snapshot autorizado. A consulta interna de extras só é acessível ao serviço. O app pode mostrar o tanque imediatamente pelo canal de campo existente.
- Enriquecer os intervalos históricos com a posição e altimetria verificáveis. Conciliar PID 250 e `w183` em intervalos disjuntos, associar peso, direção e condição da carga e medir a cobertura.
- Executar e validar o modelo segmentado por veículo, persistir previsões independentes, revisões e previsão original e publicar custos e reservas com suas fontes.
- Publicar o contrato de armazenamento offline após reiniciar e os segmentos necessários a um recálculo com GPS local. A entrega atual tem cache de sessão e consumo local independente da rede.

[Contratos, unidades, esquema JSON e regras de validação](CONTRATOS_TRUCKBOX_INTELLIGENCE.md)
