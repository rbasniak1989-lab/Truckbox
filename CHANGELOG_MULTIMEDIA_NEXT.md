# TruckBox Next — 0.1.0-homologacao

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
