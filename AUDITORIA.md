# Auditoria pública — TruckBox Multimídia Next

Data: 13/11/2026. Base de código auditada: `06a9309d104367a49c88167d8f2b70bd7c1ffd1e`.

Este resumo publica evidências do código já público, o plano de produto e o resultado das verificações locais. O inventário interno detalhado da Cloud aguarda autorização específica para publicação. Esta branch contém documentação; o módulo e os APKs continuam somente no workspace indisponível.

## Contrato

Foram lidos primeiro ESPECIFICACAO_MESTRE.md, MATRIZ_DE_ACEITE.md e ESTADO_ATUAL_E_BLOQUEIOS.md; depois as sete especificações e todos os HTML/PNG de referência. O mestre prevalece sobre menus históricos e dados fictícios.

ZIP recebido: TruckBox_Entrega_Completa_Codex_v1.zip, SHA-256 `f50649d1bb49caf0780c4467f193add40963f810f76c25762393daff44b73b0d`. Os 26 hashes do manifesto conferiram. O produto foi construído localmente em Compose nativo, sem WebView dos protótipos.

## Evidências no código público da base

| Fonte | Constatação |
|---|---|
| settings.gradle.kts | :app, :multimedia e :recovery; módulo Next ainda ausente na base. |
| app/build.gradle.kts | Motorista possui applicationId próprio e deve ser preservado. |
| multimedia/build.gradle.kts | Legado usa br.com.truckbox.multimedia e SDK 36. |
| multimedia/.../MainActivity.kt | UI antiga possui navegação e estrutura diferentes das referências aprovadas. |
| .github/workflows/android-build.yml | A CI antiga aplica patches antes do build; não foi alterada. |
| gradle/wrapper/gradle-wrapper.properties | Gradle 9.4.1. Scripts/JAR wrapper foram gerados no trabalho local. |
| Árvore auditada | Firmware não encontrado nesta árvore. Não foi modificado. |

A comparação local com a base para app/multimedia/recovery não mostrou diferenças. A implementação nova tem applicationId exclusivo de homologação e não substitui o legado.

## Decisões técnicas

Core continua coletando; Motorista continua responsável por operação/Gateway/GPS/planejamento; Cloud por persistência/autorização; multimídia por acompanhamento. A nova app não envia ACK de lotes, não reinicia contadores e não altera fretes.

Sem origem, timestamp e qualidade comprováveis, uma resposta HTTP não estabelece leitura atual. O adaptador novo é tipado e conservador. Sem contrato seguro disponível, módulos protegidos começam negados. A API proposta é uma biblioteca local de referência, sem deploy ou adaptador de banco.

Não houve alteração de schema/Edge Functions de produção. Detalhes internos da auditoria Cloud não acompanham esta versão pública.

## Limites e dependências

Não foram comprovados fisicamente os 13 sensores, DTC, GPS/4G/SD do Core ou o funcionamento conjunto com hardware. Não foram comprovados fornecedores/licenças/custos de mapa, relevo e tarifas nem validade jurídica de classificações de jornada. Veículo parado não comprova descanso; identificação GPS não comprova pagamento.

Persistência autoritativa do tanque requer ambos tanques cheios confirmados, consumo confiável incluindo lenta e eventos idempotentes; boia não é entrada. Financeiro/Jornada exigem políticas Cloud independentes, sem toggle local.

Novos fontes e APKs foram inspecionados por scanner de credenciais; zero ocorrências pelos padrões executados. Nenhuma credencial de produção foi introduzida. Isso não certifica todo o histórico Git ou dependências.

## Próximo passo

Recuperar o workspace, publicar o incremento de código revisável e os APKs já compilados, e seguir as condições de integração e campo do plano. Não declarar app comercial pronto. Consulte PLANO_IMPLEMENTACAO.md e ESTADO_DA_ENTREGA_NEXT.md.
