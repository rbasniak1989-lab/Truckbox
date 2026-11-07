# Relatório executado — Rev.B.1 de testes

Atualização v0.2.3: inclui regressão com sockets TCP dual stack reais, corrigindo
a descoberta do Link no Core. Suites host e builds executados novamente.
Leia PEER_FIX_v0.2.3.md. O Link 0.2.2 permanece compatível.

Histórico v0.2.2: suites deste relatório executadas novamente, acrescentando
seleção Wi-Fi/PPP do Link, uploader/ACK/memória e cache de pares em C real com
fronteiras simuladas. Ingestão v25 implantada e testada por HTTPS/banco sem GPS.
Detalhes e limites físicos em WIFI_LINK_CLOUD_v0.2.2.md e evidências v022.

## Histórico da entrega v0.2.0

Sessão de desenvolvimento:06/11/2026, America/Sao_Paulo. Alvo real de compilação:
ESP32-C6-WROOM-1/8MB, ESP-IDF **v5.5.2** commit
`30aaf64524299d3bde422ca9a2848090d1bc5d0f`. Não houve flash, merge ou implantação Cloud.
Não foi usada placa/simulador de periféricos ESP32; os ensaios de falha descritos
abaixo executam código C real no host com mocks somente nas fronteiras de hardware/RTOS.

## Build de referência e build novo

O fonte interno do ZIP funcional foi extraído e compilado separadamente antes da
integração. Core original:1.376.688bytes; Link original:1.423.520bytes. Ambos passaram,
com os mesmos tamanhos da release; partições e dados OTA iguais por SHA256. Hashes
inteiros de apps recompilados podem mudar com data/metadados de compilação.

Core e Link0.2.0 também compilaram com sucesso, sem warnings/erros nos logs finais.
Ambos cabem nos slots3MiB e utilizam a mesma tabela de partições da base.

| Imagem | Bytes | SHA256 |
| --- | ---: | --- |
| core | 1433824 | `7939dac1ef5de536785e7b53fd8ea174e2b185ae617574c1c9bf1907b84dec90` |
| link | 1423520 | `82e44b2839fe85cb0a96be51f8adf26874a78eee4a44be7586495a9639cbd84f` |

Bootloaders, tabela e dados OTA foram gerados pelo ESP-IDF e incluídos com os
offsets reais de flasher_args.json. Os hashes finais são repetidos no manifesto.
Os dois ZIPs v0.1.0-b1 foram comparados com a branch funcional e permanecem idênticos.

## Testes de software executados — PASS

- **J1939:** catálogo76/29PGNs,55fórmulas/enums/grupos,15brutos/parciais,6pendentes/NA; limites, little-endian, escalas, estados SAE, ausência de fórmula, source addresses, alias específico, peso sem soma, retenção do último válido e deltas sem reset artificial.
- **Equivalência:**80comparações escalares/componentes C×Python original, incluindo curva de motor, VEB com P5/B18, pressões/temperaturas, calendário, cruise e I-Roll. Diferenças intencionais de segurança em inválidos são documentadas. Vetores construídos do Mestre, não replay integral.
- **Consumo:**1.000amostras válidas geradas confrontadas com FuelPowerAccumulator Python; modelo2643Nm/C0=0,9720031611625598 e integração retangular atual até1s. Casos de FF, gap, reset, origem e mistura de fontes não integram intervalo desconhecido.
- **TP.BAM:**mensagens30/19bytes, quatro sessões intercaladas, sequência, órfão/duplicata, limites, timeout, origem/destino e IDs exatos dos frames físicos. Nunca simula DLC30 no CAN clássico.
- **J1587:**quatro suítes originais e bridge48+17; exemplos documentados de29/10, múltiplosPIDs, página2/PID440, raw, checksum, tamanhos, NA, estados, datas,ASCII,DTC. Leitura fora de limites251/252 e NA de antigos campos brutos corrigidas.
- **17requisições:**catálogo/ordem/framingPID128/checksum, semTX por padrão, MIDs explícitos, autorização/estacionamento/topologia, guarda5ms, espaçamento250ms, timeout450ms, eco/colisão, lease60s, erros, parada síncrona do callback de aborto e wrap temporal. Respostas candidatas não são prova causal.
- **Captura real:**migração dos checkpoints v1→v2, reboot, ausência deGPS, preferência de fontes, retorno de contador após estimativa, reset e NVS fora do mutex RX. Modelo derivado e fronteiras de fallback também ensaiados.
- **GPS real do Core:**ausência de coordenadas/horário fictícios, idade versus instante real de aceitação, proteção contra atribuição retroativa a frames enfileirados, expiry/perda/retorno e preservação da velocidade veicular.
- **Armazenamento real store.c:**semGPS/offline, reboot, histórico imutável apósGPS, seq reservada sem reutilização, ACK idempotente e rejeição de gaps, falha NVS/fsync, cauda interrompida, CRCcorrompido, capacidade cheia retendo backlog e buffer durante recuperação. Registro só ofertado após fwrite/fflush/fsync/fclose.
- **Serializer real:**GPS completo, NA/erros, sete linhas65263, dez componentes de cada curva, payload64bytes, cache32bytes com marcação de truncamento, divisão abaixo de768bytes, preservação de todos os framesTP físicos, TX_ECHO, overflow de fila e rejeição pelo store com contadores.
- **Regressão da base:**configuração Core/Link, política OTA/imagens e papéis,4.000registros validados por parserJSON independente, FRAM/CRC/gravações interrompidas, NMEA e outboxGPS, seis testes do gravador Windows sem acesso USB.
- **Pacote:**sete testes de layout/hashes/offsets/versão/IDF, incluindo extração do ZIP e compilação do decoder real com módulos irmãos. Vinte e um testes Python do diretório tests/ passaram no total; as suítes C e de referências são adicionais.
- **Cloud local:**a regressão ingestv24 foi reproduzida; função completa executada em VM com Supabase/Deno mocks, sem rede/banco real. Patch conserva diagnóstico/ACK, exclui efeitos operacionais de RAW/DECODED/eventos, preserva legado e respeita qualidade temporal/GPS ausente. Seis testes Python do preparador também passaram.

C de integração, armazenamento, transporte, serializer e controle ativo foi
executado com AddressSanitizer/UndefinedBehaviorSanitizer nas suítes indicadas.
Há executores sem sanitizer por padrão que também foram repetidos com sanitizers;
todos com compilação C11 estrita e warnings tratados como erros.

Executores principais:

```sh
. /caminho/esp-idf-v5.5.2/export.sh
cd firmware/espidf
sh tests/run_all.sh
bash tools/build_v020.sh all
```

O teste da função Cloud completa exige obter por leitura a fonte privada v24;
consulte cloud/README.md. O fonte integral privado não foi incluído no pacote.
As saídas consolidadas estão em test_evidence/host_tests.txt e cloud_mock.txt.

## O que não foi executado

**Não foram reprocessados os ZIPs originais dos Logs Mãe**, ausentes no ambiente.
As estatísticas de milhões de frames em VALIDACAO_LOGS_MAE_PARSER_V1.md são históricas.

Não houve ensaio da placa: boot sem antena, recepção elétrica, microSD físico,
queda de alimentação, duração/vazão, OTA/rollback reais, modem ou colisões no
barramento real. Não houve envio real Core→Android→Cloud nem aplicação do patch
Cloud. TCP/AP/HTTP e hardware da base foram preservados no fonte e nos testes
portáteis aplicáveis; isso não comprova Wi-Fi/RF ou compatibilidade física.

PENDING.md lista impedimentos e limites, sem substituir pendência por suposição.
