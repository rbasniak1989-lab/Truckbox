# Core 0.2.1: correcao do rollback por RAM na Rev B1

O log enviado da placa confirmou a sequencia: OTA de 0.2.0 validada, aplicacao
0.2.0 inicializada, servicos prontos, `startup heap failed: ESP_ERR_NO_MEM` aos
15.982 s e rollback explicito para 0.1.0-b1. A guarda exige 20.000 bytes de heap
livre; esse log anterior nao informa quanto estava livre naquele instante.

O Core retinha simultaneamente a arvore cJSON do lote e sua representacao de
envio durante uma espera HTTP de ate 45 segundos pelo Link. A ampliacao dos
decodificadores em 0.2.0 ja ocupa mais RAM que 0.1.0-b1.

## Mudancas

- Lotes Core->Link limitados a 8 amostras / 8192 bytes, antes 16 / 16000.
  O intervalo de 100 ms apos ACK continua permitindo escoar a fila em lotes.
- Arvore cJSON liberada antes da chamada HTTP; ficam apenas o texto e ate 8 IDs.
  O texto tambem e liberado antes de analisar o ACK.
- ACK continua exigindo o ID exato de uma amostra enviada, inclusive com lacunas
  nas sequencias. Nao basta estar entre o menor e o maior ID.
- Diagnostico inclui `heap_largest_block`; o log de inicializacao inclui heap
  livre, minimo e maior bloco. A guarda de 20.000 bytes e o rollback permanecem.

Capacidades das filas de captura, gravacao e cache de sensores nao mudaram.
Decodificadores J1939/J1587, modo passivo, particoes, NVS e formato dos registros
permanecem os de 0.2.0. O Link nao precisa de uma nova imagem para esta correcao.

## Validacao realizada

- Compilacao Core ESP32-C6, flash de 8 MB, ESP-IDF v5.5.2.
- Tabela de particoes identica ao pacote 0.2.0 publicado.
- Teste das funcoes reais de serializacao: preservacao do corpo HTTP, liberacao
  antecipada, ACKs exatos com lacunas, limites, entradas invalidas e limpeza
  apos falhas de alocacao, com AddressSanitizer e UndefinedBehaviorSanitizer.
- Fixture host com 8 registros: heap retido durante a espera passa de 20.193
  para 3.581 bytes. Esta medicao usa alocador host; nao e uma medicao na PCB.
- Testes existentes de telemetria, armazenamento/transacoes, GPS, integracao de
  captura e J1939 passaram.

Validacao fisica da imagem 0.2.1 e da margem de RAM sob trafego real: PENDENTE.
O log comprova a causa do rollback anterior; a efetividade desta correcao deve
ser confirmada na placa. Nao se desativou a recuperacao para forcar a aprovacao.

## Reproduzir o build

Na pasta `firmware/espidf`, com ESP-IDF v5.5.2 ativo:

```
idf.py -B build-core -DTRUCKBOX_ROLE=core -DPROJECT_VER=0.2.1 build
```

Esta entrega e apenas a aplicacao OTA do Core. O empacotador historico
`tools/build_release.py` continua documentando a entrega completa 0.2.0.
