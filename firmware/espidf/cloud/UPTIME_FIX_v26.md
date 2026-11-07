# Ingestão v26 — milissegundos inteiros para SQL bigint

Correção somente na nuvem. Core 0.2.3 e Link 0.2.2 continuam instalados;
não é necessária outra gravação nem mudança de credenciais.

## Falha observada

Após corrigir a descoberta do Link, o dispositivo enviou 21 lotes com ACK:
168 registros HEALTH foram persistidos. Os lotes seguintes pararam antes de
gravar telemetria; Core informava HTTP 503 do Link e ACK estacionado em 5381.

Os logs da função e do PostgreSQL confirmaram erro 22P02:
`invalid input syntax for type bigint: "339.721"`.

O firmware registra microssegundos em `uptime_us`. O conversor anterior dividia
339721 por 1000 e tentava inserir 339.721 em `telemetry_samples.uptime_ms`, que
é bigint. A validação rejeitava a transação; nenhum ACK era retornado.
Os fixtures anteriores usavam microssegundos múltiplos de 1000 e não cobriam
esse caso da coluna SQL. A regressão agora inclui o contrato bigint.

## Correção e preservação

- Normaliza a coluna de milissegundos para inteiro não negativo com `Math.trunc`.
- Mantém o microssegundo original e a qualidade temporal no JSON `raw`, intactos.
- Valores ausentes/inválidos ficam NULL; não viram leitura de sensor zero.
- Não altera esquema, autenticação, chaves, posições GPS ou política de ACK.
- A função publicada passa a responder `ingest_version:26`.

O preparador `patch_ingest_v26.py` parte do snapshot v24 com SHA256 conferido,
aplica as mudanças já revisadas de v25 e gera a correção. O diff público
`truckbox-ingest-v26.patch` contém apenas v25 → v26, sem constantes privadas.
O fonte integral da função e os tokens continuam fora do Git/ZIP públicos.

## Verificação

- Mock executa a função TypeScript completa com validação bigint simulada:
  reproduz a falha em v25 e verifica v26 com microssegundos fracionários em ms,
  milissegundos fracionários, zero, campos ausentes, negativos e fora do limite.
- Mantém os testes de RAW/DECODED/eventos sem efeitos operacionais, legado,
  ausência de GPS, autenticação, sequência inválida e falha de persistência.
- Teste HTTPS em dispositivo temporário recebeu HTTP 500 sem ACK em v25.
  Após v26, o mesmo registro recebeu HTTP 200 e ACK 1; reenvio não duplicou.
- Consulta ao banco confirmou `uptime_ms=339`, `raw.uptime_us=339721`,
  classificação RAW e latitude/longitude nulas.

As evidências locais estão em `test_evidence/v026_*`. O dispositivo temporário
foi removido após conferir a persistência e a deduplicação.

Após a implantação, consultas separadas confirmaram a chegada do dispositivo
real: RAW e DECODED com origem CAN/J1708 e amostras OPERATIONAL, com avanço de
sequência e preservação de microssegundos/ausência de GPS. Eram registros da fila
persistida em boots anteriores. Isso confirma retomada do envio; não comprova
atividade CAN no boot atual nem captura sustentada sem perdas com flash limitada.

```sh
python3 cloud/patch_ingest_v26.py /PRIVADO/ingest-v24.ts \
  --output /PRIVADO/ingest-v26.ts --diff cloud/truckbox-ingest-v26.patch
node cloud/test_ingest_mock.mjs --source /PRIVADO/ingest-v26.ts \
  --v25 --v26 --v25-source /PRIVADO/ingest-v25.ts
```
