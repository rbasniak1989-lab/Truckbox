# Testes — Core 0.2.14 candidata

ESP-IDF 5.5.2, ESP32-C6, flash 8 MB. Testes nativos usam o controlador/agendador real, clock e interface UART/barramento simulados. Não representam ensaio elétrico na Rev.B.1.

| Verificação | Resultado |
|---|---|
| Build Core para o target real | Passou; descritor, tamanho OTA, perfil e hashes em `v0214_image.json` |
| Proteções/caminho externo da 0.2.13 | Suíte existente passou: nonce/quadros externos corretos, checksum, velocidade recente, MID, eco, arbitragem, leases, timeouts e parada |
| Caminho supervisionado completo | Passou: antes, janelas sem TX, 3 × 17 pedidos com ecos/respostas, depois, resultado indireto e confirmação separada |
| Coincidências espontâneas | 51 ecos e 51 respostas candidatas com todos os PIDs também presentes nos controles resultam em INCONCLUSIVE; não liberam viagem |
| Limite de contraste | Nove PIDs com contraste permitem resultado satisfatório; oito resultam em inconclusivo |
| MID/PID/temporização | Alvos errados e duplicatas não contam como respostas; timeout interrompe ensaio; latência é medida pelo clock monotônico |
| Confirmação pelo dono da captura | Rejeita falta de autorização, caminhão em movimento, velocidade antiga, conflito de MID, nonce de outro boot e resultado ainda em medição |
| Falhas e interrupção | Eco divergente/colisão, silêncio, checksum anormal, expiração, perda de auditoria, movimento e parada bloqueiam ensaio; três falhas de resposta continuam parando o modo de viagem |
| Reboot | Inicia sem autorização nem validação persistida; não arma TX |
| Registros de campo reais do firmware | Serialização testada com checksum/raw sintéticos; não fabrica campos externos, PID/TX para eventos globais nem transmissão em janela silenciosa; libera memória em cada falha de alocação |
| Auditoria por PID | 482 B na fixture; dentro do limite de 767 B de JSON; evidência incompleta não é aceita como registro completo |
| Memória/diagnóstico | Falhas em todos os pontos de alocação, estado de campo ativo e inativo, referências e falhas de envio HTTP passaram; detalhes consultados em páginas de quatro PIDs pelo painel |
| Navegador com HTML real e servidor local | 20 verificações passaram, incluindo ambos os caminhos, confirmações separadas, resultado inconclusivo, campos externos vazios, OTA, baixa RAM, parada e layout móvel |
| Suíte geral | `tests/run_all.sh` passou: armazenamento/recovery/CRC/ACK/worker, justiça de CPU, J1939/Python-C, J1587, captura, viagem/consumo, GPS, J1708/UART, Link e 24 testes Python |

Os testes do dono da captura usam estado de resultado sintético para verificar os limites de autorização/nonce; a máquina de estados completa é exercitada separadamente com três rodadas efetivas do controlador nativo. Não são evidência de resposta causal na rede real.

## Duas horas offline com metadados do modo indireto

O teste foi atualizado para incluir o volume de eventos de requisição associados a uma sessão supervisionada aceita, com o serializador de produção. Cada cenário usa 7.200 segundos virtuais, 433.624 quadros sintéticos, 65 sensores no cache, 1.624 requisições/respostas candidatas e 5.904 registros. Não habilita gravação bruta contínua.

| Medida | Sem relógio absoluto | Com relógio absoluto |
|---|---:|---:|
| JSON original | 2.991.346 B | 3.055.751 B |
| Segmentos ocupados | 34 | 36 |
| Bytes físicos | 1.655.124 B | 1.732.149 B |
| Orçamento | 1.818.624 B | 1.818.624 B |
| Reinícios com cauda interrompida | 14 | 14 |
| Descartes por capacidade | 0 | 0 |
| Linhas únicas após reconexão simulada | 5.904 | 5.904 |
| Entregas incluindo repetição de ACK perdido | 5.912 | 5.912 |
| Duplicações no servidor simulado | 0 | 0 |
| Pendências válidas finais | 0 | 0 |

Os arquivos isolados permanecem: 685.622 B e 686.535 B após os ACKs, nos dois cenários. Eles reduzem o espaço futuro. A simulação de autonomia começa com o orçamento disponível após a validação; não inclui os arquivos anteriores ou uma sessão de calibração ainda pendente de envio. São duas horas de volume simulado, não duas horas observadas na placa.

## RAM

| Uso | 0.2.13 | 0.2.14 | Custo |
|---|---:|---:|---:|
| RAM estática do ELF | 201.620 B | 202.148 B | +528 B |
| Item da fila de eventos (DWARF ESP32-C6) | 168 B | 232 B | +64 B |
| Fila de 16 eventos | 2.688 B | 3.712 B | +1.024 B |

Custo conhecido adicional: **1.552 B**. Filas RX/gravação e pilhas mantidas. O estado do ensaio usa dados fixos; não aloca um buffer por quadro. O espaço do resumo de saúde e do resumo por PID é compartilhado na fila. Não se trata de medição de heap na placa.

Na fixture de diagnóstico nativo com campo ativo: árvore completa 36.922 B, compacta 8.489 B (77% menor nessa parte do JSON sintético). Com campo inativo: 23.290 B e 7.284 B. Os tamanhos do cJSON nativo não equivalem aos do ESP32. A serialização progressiva e o fallback `/api/health` da 0.2.13 permanecem.

## Reproduzir e validar na placa

```sh
# Ambiente ESP-IDF 5.5.2 ativado.
bash tools/build_v020.sh core
sh tests/run_all.sh
# Playwright/Chromium disponíveis:
node tests/web/test_flow.cjs
```

Evidências em `test_evidence/v0214_*`. Consulte o guia `REV_B1_0.2.14_CANDIDATA.md` para executar o ensaio parado e autorizar viagem. Faltam a execução supervisionada real desta imagem, observação de RAM/pilha na placa, análise de eventuais variações de tráfego do veículo e operação prolongada. O firmware não corrige defeitos elétricos de alimentação/transceiver, e resultado indireto não substitui analisador independente quando este estiver disponível. Não houve OTA automática, merge ou alteração de viagens.
