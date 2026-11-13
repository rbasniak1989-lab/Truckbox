# Plano revisável — TruckBox Multimídia Next

## Arquitetura

Módulo novo :multimediaNext, Kotlin/Compose nativo, sem dependência de :app/:multimedia. ApplicationIds: br.com.truckbox.multimedia.next e .next.demo. Demo separado com banner permanente e sem rede; homolog com dados reais/indisponíveis.

Separar domínio puro, DTOs/repositórios tipados e componentes/telas. Estado protegido efêmero e por tenant/veículo/dispositivo/usuário/sessão; política somente pela Cloud, versionada e com validade/ACK. Financeiro e Jornada independentes, deny por padrão e sem chaves locais.

Cinco áreas: Home, Consumo, Viagem atual, Caminhão/Diagnóstico e Configurações. Drawer recolhido/sobreposto, paisagem/imersivo, #E87924/grafite, Oxanium/Inter licenciadas, noite 18–6 São Paulo e brilho local quando suportado.

## Home contratual

Exatamente 13 posições em 3+4+6: pressão óleo, turbo, combustível; temperatura água, óleo motor, admissão, i-Shift; marcha, tensão, ambiente, pedal, dois circuitos de ar em um cartão e torque. Não substituir essa lista.

Sem novas páginas de Histórico, Comparativos, Relatórios, cadastro de frete, planejador ou Jornada isolada. Jornada é modal opcional da Viagem; Financeiro é cartão opcional independente. Na área Caminhão, Geral é primeira e concentra falhas; nas outras abas: IA, sensores, min/média/máx e expansão.

## Incrementos e verificação

| Ordem | Tarefa | Prova |
|---|---|---|
| 1 | Shell, tema, cinco rotas e ambientes | Build, IDs distintos, drawer, noite e preferências. |
| 2 | Home 13 posições e inteligência | Ordem, pressão dupla e screenshots. |
| 3 | Consumo por etapa | L/h parado, carregado/vazio e ledger 720→570→670→650 com confirmação, sem boia. |
| 4 | Viagem e módulos opcionais | Quatro matrizes, revogação e vazio sem receita. |
| 5 | Caminhão | IA primeiro, central Geral, estatísticas temporais e DTC confirmado. |
| 6 | Configurações | Permissões read-only; edição apenas parada/autorizada e persistida por usuário/veículo. |
| 7 | Autorização e contratos v1 | Verificar antes de consultar/serializar; testes negativos e sem endpoints fictícios. |
| 8 | Integração e entrega | Unitários/UI/instrumentados, cinco viewports, scanner, APK e roteiro físico. |

## Contratos e migrações propostos

São propostas de produto, não descrições de schema implantado. Validar ownership/autenticação e staging antes de gerar DDL definitivo.

- Identidade, vínculos e sessões por dispositivo/usuário/veículo.
- Políticas independentes versionadas, auditoria e ACK.
- Ledger combustível por evento/epoch/seq e confirmação inequívoca de ambos tanques cheios.
- Observações/episódios DTC com UNKNOWN após desconexão e fim só confirmado.
- Praças/tarifas versionadas/passagens/responsável/reembolso e confirmação do vínculo do vazio.
- Segmentos/correções/confirmações de Jornada, com revisão legal.
- Evidências/versões de insights e layouts de 13 posições.

Adições com privilégios mínimos e autorização em cada consulta; nenhuma abertura genérica de acesso. Não assumir confirmação de ambos tanques ou vínculo do vazio a partir de registros antigos. Revisar todos os caminhos de autorização existentes.

Rollback operacional: desabilitar novos serviços/vínculos/reconciliadores e negar módulos, preservando dados/auditoria. Não apagar registros como forma padrão de reversão. Não executar produção sem revisão/autorização.

## Condições para integração e aceite

Provisionar identidade e sessão, validar contratos em staging, capturar telemetria autorizada, comprovar timestamps/qualidade e persistência/replay. Validar 13 sensores, DTC, tanque, jornada e ecossistema Core/Motorista/Cloud. Aprovar fornecedores/licenças/custos de mapas/relevo/tarifas e pendências legais.

Testar os cinco viewports e o Android 13” real com toque, densidade, brilho/noite, imersivo, offline/reinício e operação com multimídia desligada. Sem dados, usar Sem leitura/Dados insuficientes/Não verificado. Mocks não substituem campo.
