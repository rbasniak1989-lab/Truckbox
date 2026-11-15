# 0.6.5-cloud-gps

- Motorista passa a usar somente Cloud para comunicação; GPS vem do celular.
- Remove a dependência de Core online para enviar posições e as chamadas
  locais de telemetria/ACK/GPS ao ESP32.
- Adiciona fila GPS SQLite, identificação estável, confirmação do caminhão e
  horário, reenvio após reconexão, preservação no reinício e pausa explícita.
- Adiciona fila operacional ordenada e imutável para viagens e lançamentos;
  protege alterações offline contra snapshots atrasados.
- Não substitui referências inexistentes de consumo/odômetro por zero.
- Adiciona código privado do celular e proteção Keystore para os tokens.
- Mostra estados separados de GPS e operações sem presumir conexão.
- Inclui variante de teste que instala ao lado do Motorista existente.
- Nenhuma mudança de firmware, multimídia, Recovery, schema ou Edge Function
  de produção faz parte do código desta versão.

Resultado dos testes e hashes do APK constam em `VALIDACAO.md` após o build.
