# Compatibilidade Cloud — patch local pendente

**Este diretório não implanta nem altera a Cloud.** A função `truckbox-ingest`
versão 24 foi examinada por leitura. O código completo dessa função contém
identificadores e dados de autenticação; ele não faz parte deste repositório
nem do ZIP de fontes. O patch público contém somente os trechos de tratamento
de telemetria necessários à revisão.

## Problema encontrado

A versão examinada inclui todo `sample_kind != HEALTH` no grupo chamado
`operational`, incluindo `RAW`, `DECODED` e `J1708_REQUEST_EVENT`. Essas linhas
são gravadas, mas também podem participar da atribuição de viagem, do cálculo
de consumo, da seleção de `telemetry_latest` e dos contadores de operação.
O teste reproduziu a substituição do estado mais recente por um diagnóstico
quando o estado anterior não tinha hodômetro, além do agendamento indevido do
cálculo de consumo. O efeito depende dos dados e filtros já existentes.

O firmware v0.2.0 gera esses tipos de registro. **A integração completa pelo
endpoint antigo ainda depende da revisão e aplicação deste patch no backend.**
A aquisição e o armazenamento local no Core não dependem dessa aplicação.

## Alteração proposta

- Gravar e confirmar as sequências de todos os registros recebidos, mantendo
  o payload e `raw.sample_kind` originais em `telemetry_samples.raw`.
- Permitir efeitos sobre viagem, consumo, estado mais recente e contadores
  somente para `raw.sample_kind=OPERATIONAL`, ou quando o campo está ausente
  no formato legado.
- Aceitar `unix_ms` somente com `time_valid=true`; aceitar `uptime_us/1000`
  para reconstrução temporal quando o gateway fornece uma referência válida
  para o mesmo `boot_id`.
- Quando não há referência UTC aplicável, usar `ts` como horário de recebimento
  e conservar o monotônico, o boot e `time_valid=false` dentro de `raw`.
  O patch não transforma esse horário de recebimento em fix GNSS.
- Manter latitude/longitude nulas quando ausentes, sem criar `(0,0)`.

A coluna SQL `sample_kind` continua com a representação aceita pelo backend
existente. A distinção de registros é feita pelo campo original dentro de
`raw`; não há migração de tabela neste patch. Autenticação, RLS, política de
ACK, escrita dos dados e configuração do projeto não são alteradas.

## Preparar a cópia para revisão

Obtenha por leitura a fonte da versão 24 e salve-a em diretório privado
ignorado pelo Git e pelo empacotador. Na pasta `firmware/espidf`:

```sh
python3 cloud/patch_ingest_v24.py private/ingest-v24.ts \
  --output private/ingest-v020-review.ts \
  --diff private/ingest-v020-review.patch
node cloud/test_ingest_mock.mjs \
  --source private/ingest-v020-review.ts --baseline private/ingest-v24.ts
python3 tests/test_cloud_patch.py
```

O preparador exige exatamente o SHA256 da fonte examinada:
`716bbaa96646b5adafa4e62fd33852f4b3326153f95282adaf2387eec2268fec`.
Se a fonte remota mudar, ele para sem gerar cópia. Reexaminar a nova versão
antes de adaptar o patch. Não versionar os arquivos completos em `private/`.
O diff público revisável está em `truckbox-ingest-v24-v020.patch`.

## Testes executados

Os mocks executaram localmente a função TypeScript completa, com Node
24.22.1, Supabase e Deno simulados; nenhum acesso de rede ou escrita na
Cloud foi realizado. Node informa que a remoção de tipos TypeScript usada
nesse teste é experimental.

- Regressão reproduzida na função original v24.
- Diagnósticos RAW/DECODED/requisições preservados e confirmados sem alterar
  estado operacional, viagem, consumo ou contadores.
- Lote misto atualiza o estado somente pela amostra OPERATIONAL e confirma
  também as sequências dos diagnósticos persistidos.
- Amostras legadas sem `sample_kind` continuam operacionais.
- UTC válido, monotônico em microssegundos e referência de outro boot
  respeitam suas qualidades; GPS ausente permanece nulo.
- Falha simulada na persistência não produz ACK nem efeitos operacionais.
- Seis testes Python verificam proteção por hash, preservação da fonte,
  fragmentos únicos e ausência do bloco de autenticação no diff público.

**Pendente:** revisão do backend, implantação separada, testes reais
Core → Android/gateway → Cloud, limites de taxa e retomada/ACK com a Cloud.
Os mocks não comprovam comportamento do banco, funções SQL ou rede reais.
