# TruckBox Next

## Cloud v2 — compatibilidade de horários com Android 8–13

Ativação retornava HTTP 200 e vinculava o aparelho, mas o APK Android 13 recusava `expiresAt` com sufixo PostgreSQL `+00:00`: o `InstantPrinterParser` dessa plataforma exige `Z`. A API temporária agora normaliza os timestamps de ativação, política, lease, viagem e GPS para UTC com `Z`, preservando a fração original e os mesmos instantes. Código, vínculo, prazo e permissões mantidos; APK 0.2.0 continua válido.

Correção publicada na Edge `multimedia-next-v1` v2. Seis testes focados SQL/RSA/HTTP/serialização passaram, com TypeScript strict e Deno check. Proprietário confirmou “Vínculo consultado na Cloud”; ACK da política v1 confirmado no banco. Isso valida ativação/Cloud nesse aparelho, sem declarar o restante do hardware ou GPS/viagem real homologado.

## 0.2.0-homologacao — última leitura e canal Core/Cloud

Pedido posterior de campo: canal temporário sem contas publicado após aprovação explícita, um vínculo proprietário por até 72 h, Financeiro/Jornada autorizados na Cloud e GPS exclusivo do Motorista. Não habilita payload sem fonte verificada. Migração completa de contas não aplicada. Guia direto em GUIA_TESTE_VIAGEM_020.md.

- Home conserva a última observação real de cada instrumento entre consultas esparsas, NA e falha de comunicação; mostra idade/desatualização sem renovar a captura. Zero real continua sendo zero. Sinais nunca observados ficam Sem leitura. O cache não fornece velocidade/vazão atuais nem números para cálculos e não se mistura entre Core/veículo/MID/boot dos circuitos de ar.
- `/api/health` identifica o Core antes de ler sensores. Vínculo remoto pode exigir UID exato; captura de outro Core não reutiliza cache anterior. Configurações distingue encaminhamento anunciado pelo firmware de Internet/Cloud realmente verificadas.
- Cliente Cloud nativo HTTPS com DTOs, Supabase Auth, prova RSA do aparelho, refresh cifrado, nonce, ACK de política e polling independente. Quatro combinações Financeiro/Jornada, limpeza em revogação/offline/expiração e descarte de retorno tardio; sem toggle local nem conteúdo protegido persistido.
- Backend proposto concreto com consultas parametrizadas e migração privada aditiva; testes HTTP/RSA/Postgres isolados. Nenhum deploy/DDL hospedado. Read models de consumo/financeiro/jornada começam vazios, sem assumir confiabilidade dos dados legados.
- Viagem e GPS do veículo autorizado vêm da Cloud. Posição mostra captura/origem/precisão recebidas; não há extrapolação nem posição atual inventada. Mapa/rota/licença permanecem pendentes.
- Captura nativa adicional de retenção para 13″, explicitamente TESTE DE CONTRATO, e preservação dos 13 instrumentos, fontes do ZIP, proporção 2:1 e rodapé.

Firmware separado: [PR 27](https://github.com/rbasniak1989-lab/Truckbox/pull/27), candidata Core **0.2.24** para compartilhar Starlink com a multimídia conectada ao AP do Core. APK não altera firmware. Auth/Edge hospedados, conta/vínculo, validação de hardware e integrações comerciais continuam pendentes; ver REVISAO_CORE_CLOUD_020.md e TESTES_REALIZADOS.md.

## 0.1.1-homologacao — Core e Home de 13″

- Leitura do endpoint real `/api/sensors` schema 2 da candidata Core 0.2.23, paginado em lotes de oito. Mapeamento explícito de SID/PID, PGN e unidade; sem interpretar catálogo, histórico ou último valor válido como leitura atual.
- Origem, boot, captura, idade e qualidade preservados. Dados J1587 em validação aparecem como **Não verificado**; não autorizam cálculos, calibração ou ações. Replay não rejuvenesce a captura; conflito de payload ou boot entre páginas invalida a consulta.
- Conexões locais vinculadas ao Wi-Fi, incluindo redes sem internet, sem alterar a rota global Android. Fallback mDNS/IP e configuração de IP RFC1918 do Core na LAN Starlink; nenhum token enviado ao Core.
- Diagnóstico de conexão em Configurações e reteste imediato. Detalhe do sensor distingue número legado recebido de leitura atual.
- Home ocupa o viewport: instrumentos em 2/3, Intelligence em 1/3 e Viagem Atual fixa no rodapé. Permanecem os mesmos 13 instrumentos e circuito duplo em um cartão.
- Modo imersivo reaplicado ao retomar/focar; Inter para textos e Oxanium 800 inclinado para números, fontes locais OFL.
- 59 testes JVM/nativos, APKs homolog/demo/instrumentação compilados e lint sem erros. Capturas em todas as resoluções do ZIP. A mesma chave debug da 0.1.0 foi preservada.

Cloud real continua pendente de implantação revisada e configuração; esta versão testa Core e interface. Sem alteração em firmware, Motorista, multimídia antiga ou produção. Testes físicos ainda não realizados pelo desenvolvimento.

## 0.1.0-homologacao

- Auditoria com base Git e evidência do catálogo/Edge Functions/RLS, cobertura parcial de sensores e bloqueios reais.
- Novo módulo :multimediaNext independente de Motorista, Recovery e multimídia antiga.
- Cinco áreas Compose Black & Orange, drawer recolhido, paisagem/imersivo, noite São Paulo e preferências locais.
- Home com os 13 instrumentos contratados em 3+4+6, dois circuitos no mesmo cartão, inteligência e fita de viagem.
- Consumo por etapa, Viagem Atual, Caminhão/Geral com central de falhas exclusiva e estatísticas temporais; configurações de módulos somente leitura.
- APK demo isolado; homolog sem PreviewData, dados fake ou acesso ao fallback público legado.
- DTO Core explícito e conservador; sem origem/horário/qualidade verificáveis, não transforma números recebidos em leitura atual.
- Regras testáveis: quatro permissões independentes, revogação/expiração/escopo, tanque por eventos sem boia, replay, estatísticas, pedágios e jornada 24h.
- Proposta de API v1 com autorização antes da leitura, nova verificação antes de serializar, projeção explícita e testes locais. Sem deploy/adaptador de banco.
- Plano de migrações aditivas/rollback, dependências/licenças, scanner de credenciais e workflow separado.

Pendências materiais: identidade/Cloud segura, ledger persistido, edição de painel por usuário/veículo, DTC real, mapa/rota/GPS, tarifas/pagamento, confirmação Motorista do vazio e descanso, IA com evidências, instrumentação/hardware real. Os resultados de compilação e testes estão registrados separadamente em TESTES_REALIZADOS.md. Nenhum firmware, app antigo ou schema de produção foi alterado.
