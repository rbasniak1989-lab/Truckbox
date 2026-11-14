# Plano revisável — :multimediaNext

Atualização posterior autorizada para viagem de teste: canal temporário sem contas implantado conforme GUIA_TESTE_VIAGEM_020.md, restrito ao vínculo proprietário por até 72 h, com ambos os módulos autorizados. A implantação completa de contas/12 tabelas abaixo permanece proposta. Firmware candidato separado no PR 27; sem flash.

Base: AUDITORIA.md e contrato mestre v1. Nenhum deploy/migração de produção autorizado neste incremento.

## Arquitetura

Módulo Android independente, applicationId `br.com.truckbox.multimedia.next`, flavor homolog (dados reais/indisponíveis) e demo separado (`.demo`, banner permanente). Kotlin/Compose nativo, tema #E87924/grafite, Oxanium/Inter licenciadas, paisagem/imersivo, drawer sobreposto recolhido. Nenhuma dependência de `:app` ou `:multimedia`.

Pacotes: domain (sinais com origem/tempo/qualidade; regras puras), data (DTOs explícitos, HTTP só leitura Core, preferências locais tipadas), ui (shell e cinco áreas). Estado protegido é efêmero e escopado a tenant/veículo/dispositivo/usuário/sessão; nunca persistir valores protegidos. Política só chega de transporte Cloud autenticado, com versão, expiração e ACK. Offline sem política verificável nega. Aparência local não muda autorização.

Backend proposto v1 em diretório de proposta fora do deploy automático: contratos separados para monitoramento, Financeiro e Jornada; verificações antes de buscar/serializar dados; DTOs sem objetos SQL completos; testes negativos dos quatro cenários, revogação, identidade e expiração. Endpoints legados não servem de fallback. Migrações aditivas/rollback e revisão descritos em CONTRATOS_V1.md.

## Incrementos e provas

| Ordem | Tarefa pequena | Verificação / condição |
|---|---|---|
| 1 | Shell, tema, cinco rotas, flavor isolado, relógio São Paulo/noite 18–6 e brilho local | Build homolog e demo; drawer fecha ao selecionar; APK IDs distintos. |
| 2 | Home 3+4+6 e pressão dupla num só cartão, 2/3 instrumentos + 1/3 inteligência, fita viagem | Teste da ordem; screenshot nas cinco dimensões; sem mock homolog; rotação só Efficiency. |
| 3 | Consumo com seleções etapa e janela; métricas e previsões explícitas | Testar carregado/vazio, instantâneo L/h parado; ledger tanque 720→570→670→650, replay/reboot/reset/lacuna. Saldo comercial aguarda backend. |
| 4 | Viagem atual com mapa/rota/posição indisponíveis quando ausentes, módulos independentes | Quatro matrizes; modal jornada fecha e some ao revogar; ausência de receita no vazio. Sem cadastro/planejador. |
| 5 | Caminhão seis abas, Geral primeiro, IA primeiro, DTC só Geral, estatísticas e expansão | Média temporal irregular; códigos/horas só de fonte; conexão perdida mantém episódio e estado atual desconhecido. |
| 6 | Configurações e preferências aparência, conexões independentes, personalização segura | Sem switches protegidos; editar só com parada verificada e autorização. Persistência de layout Cloud depende de autenticação. |
| 7 | Autorização remota e DTOs backend v1, proposta de migrations reversíveis | Testes locais server-side antes da entrega; **sem** alegar implantação/provisionamento real. |
| 8 | Integração existente que passar auditoria, hardening, APK, documentação | Testes JVM/UI/screenshot; tentativa de instrumentação; scan de segredos; lista T01–T50 com evidência. |

## Critérios para liberar integração real

1. Provisionamento de identidade de multimídia e login por usuário/veículo, renovação e revogação; autorizações em cada consulta, incluindo endpoints antigos e RPCs.
2. Aprovar contrato e aplicar adições em staging isolado com backup, testes RLS/API entre tenants e rollback. Não promover automaticamente.
3. Conferir `/api/health` e `/api/sensors` schema 2 da fonte 0.2.23 localizada, boot/captura/idade/qualidade; validar os 13 instrumentos no FH, combustível e vazão incluindo lenta. A solicitação posterior autoriza preparar a candidata Core 0.2.24 de compartilhamento Starlink em branch separada; flash/OTA continua sujeito a revisão. Ver PLANO_INTEGRACAO_CORE_CLOUD.md.
4. Validar ledger autoritativo, ambos tanques cheios, concorrência e reconstrução tardia; Motorista deve registrar semântica inequívoca. Vazio só se vincula com confirmação.
5. Aprovar fornecedores/licenças/custos de mapa, relevo e tarifa; captura DTC e legislação/classificação Jornada.
6. Executar no hardware Android 13” toque/noite/imersivo/offline/reinício e integração Core/Motorista/Cloud com a multimídia desligada.

Ao faltar contrato, a tela mostra Sem leitura/Dados insuficientes/Não verificado. Por pedido posterior do usuário, um instrumento com observação real anterior conserva o último número com idade e indicação de desatualização; nunca é tratado como atual para cálculos. A implementação local pode avançar, mas aceite integrado permanece BLOQUEADO. Lista detalhada em TESTES_REALIZADOS.md; nenhuma pendência de fornecedor ou campo será ocultada por demonstração.
