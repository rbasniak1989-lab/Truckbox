# v0.6.1 — Trip State + Trip Consumption UI

- Corrige o reenvio de `end_trip` de viagens antigas pelo serviço de sincronização.
- Uma viagem concluída histórica passa a ser sincronizada por `upsert_trip`, sem forçar `EMPTY_MODE`.
- `end_trip` automático de recuperação só ocorre se não houver viagem local ativa e o encerramento ainda estiver pendente no Cloud.
- Tela **Viagem** deixa de mostrar o contador bruto `Combustível Core` como se fosse consumo.
- Tela **Viagem** passa a mostrar `Consumido na viagem`, calculado a partir do baseline do Core.
- Adiciona `Distância da viagem` e **Média geral (km/L)** em tempo real.

# Changelog v0.6.0

- Fork oficial do app de celular: TruckBox Motorista.
- Application ID próprio (`br.com.truckbox.driver`).
- Retirado modo obrigatório paisagem/tela sempre ligada.
- Foreground service une GPS e Gateway.
- Telemetria passa por lote/ACK ESP -> Android -> Cloud.
- Cadastros operacionais continuam local-first e são reenviados periodicamente.
- Multimídia explicitamente fora do caminho crítico.
