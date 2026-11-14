# Estado da entrega — TruckBox Next

14/11/2026. Incremento 0.1.1 Core/Home disponível localmente, com 59 testes aprovados, APK assinado com a mesma chave debug e capturas para 13″. Lê o contrato real de sensores da 0.2.23, usa Wi-Fi local/LAN Starlink e corrige a Home/imersivo/fontes. A integração Cloud real ainda depende da implementação e implantação revisada do novo canal autenticado. Nenhuma mudança em firmware, Motorista, multimídia antiga ou schema de produção.

Os registros a seguir documentam a entrega histórica 0.1.0. A declaração de identidade entre os commits abaixo aplica-se somente a essa entrega, anterior ao incremento 0.1.1.

## Incremento disponível

Cinco áreas Compose Black & Orange, 13 instrumentos Home em 3+4+6, drawer recolhido, paisagem/imersivo, aparência/noite, DTO Core conservador, domínio com regras críticas e API proposta tipada. Homolog e demo têm pacotes distintos. PreviewData está apenas no flavor demo; homolog mostra dados reais/indisponíveis. Financeiro/Jornada começam negados e não possuem toggle local.

O código publicado é idêntico ao código testado no commit local 274865c3b70f433f215365e1c787df161f7ec7d0; a reaplicação sobre a branch de revisão gerou 58e98cc. A mudança de parentesco Git não alterou os fontes do aplicativo.

## Verificação

- APKs homolog, demo e instrumentação compilados e com hashes recuperados intactos.
- 35 testes JVM/Compose aprovados, zero falhas/skips; 13 casos da API proposta e TypeScript strict aprovados.
- Lint: zero erros e 10 avisos registrados em TESTES_REALIZADOS.md.
- [CI GitHub do módulo Next aprovada](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43437312280), com artefatos publicados pelo workflow.
- 27 capturas nativas preservadas em [evidências](docs/multimediaNext/evidencias/screenshots/README.md).
- Assinatura v2, applicationIds distintos, scanner e ausência de PreviewData no homolog verificados.
- Teste instrumentado compilado; execução BLOQUEADA por ausência de Android conectado e falha HTTP 503 no download do executor UTP. Nenhum teste físico foi aprovado.

[Matriz T01–T50 e pendências](TESTES_REALIZADOS.md) · [Instalação e rollback](GUIA_INSTALACAO_NEXT.md) · [Changelog](CHANGELOG_MULTIMEDIA_NEXT.md)

## Auditoria e autorização

O proprietário autorizou explicitamente a publicação do inventário e dos achados detalhados da Cloud no repositório público. [AUDITORIA.md](AUDITORIA.md) e [catálogo sanitizado](docs/multimediaNext/evidencias/cloud-catalog-2026-11-13.json) contêm evidências, sem tokens, chaves privadas ou registros pessoais/financeiros. A autorização de publicação não autoriza deploy, migração, merge ou alteração de produção.

## Bloqueios restantes

Provisionamento/login e política Cloud reais, remediação dos caminhos de acesso identificados, ledger persistido, personalização por usuário/veículo, viagem/rota/mapa/GPS/ETA, pedágios e tarifas licenciadas, confirmação do vazio, DTC, IA com evidências, jornada legal, atualização remota e hardware real.

APIs/entidades ausentes são propostas e não respostas simuladas de serviços existentes. Dados indisponíveis permanecem identificados. Não se declara o app comercial pronto.
