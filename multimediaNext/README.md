# TruckBox Multimídia Next — homologação

Aplicativo Android nativo novo, sem dependência de `:app`, `:multimedia` ou `:recovery`. Regras do pacote de produto v1 prevalecem. Cinco áreas, treze instrumentos Home e dados indisponíveis onde não há contrato validado.

- [Auditoria e evidências](../AUDITORIA.md)
- [Plano revisável](../PLANO_IMPLEMENTACAO.md)
- [Contratos e migrações propostas](../CONTRATOS_V1.md)
- [Instalação e rollback](../GUIA_INSTALACAO_NEXT.md)
- [Resultados e pendências](../TESTES_REALIZADOS.md)

`homolog` usa dados reais/indisponíveis. `demo` compila PreviewData em pacote `.demo` separado e não inicia rede. Nenhuma credencial Cloud é aceita ou embutida neste incremento; autorização/provisionamento reais aguardam backend seguro. A biblioteca TypeScript de referência em `backend-proposal/v1` não está implantada.

Fontes em `domain` contêm regras puras testáveis; `data` contém DTO Core e preferências locais; `ui` contém componentes e cinco áreas. Não usar heurística recursiva de aliases, amostras históricas como leitura atual, PID da boia, toggle local de autorização ou DTC de demonstração. Não alterar firmware, operações antigas ou produção para fazer uma tela parecer funcional.
