# Testes — Core 0.2.13 candidata

Executados no ambiente de desenvolvimento, ESP-IDF 5.5.2 e compilador ESP32-C6. Sem porta serial da placa. As medidas de JSON são de execução nativa com o cJSON real do IDF, não de heap físico da Rev.B.1.

| Verificação | Resultado |
|---|---|
| Build Core 0.2.13, ESP32-C6, 8 MB | Passou; aplicação 1.474.512 B, limite OTA 3.145.728 B |
| Build do papel Link após alteração da API comum | Passou; somente verificação de compilação, binário Link não distribuído |
| Versão/projeto/IDF extraídos do descritor do `.bin` | `0.2.13`, `truckbox_core`, `v5.5.2` |
| Tabela de partições e arquivo original 0.1.0-b1 | Hashes preservados |
| JSON: equivalência de conteúdo/bytes com cJSON | Passou: números, não finitos/null, booleans, UTF-8, escapes, raw, objetos/arrays e referências emprestadas |
| JSON: poucos bytes livres | Impressor anterior falha com árvore de 25.624 B e saída de 14.401 B; nova serialização emite com zero novas alocações cJSON, sob orçamento adicional de 256 B |
| Construtor real do diagnóstico de captura | Completo: árvore 22.811 B, pico com impressor antigo 36.600 B. Compacto: árvore 6.805 B, 70,2% menor nessa fixture |
| Falhas de alocação do diagnóstico | 579 posições no caminho completo, 179 no compacto e posições da paginação; zero itens órfãos pelo alocador instrumentado/ASan |
| Paginação | Todos os 17 PIDs, no máximo oito linhas por página, offset fora do limite, mutex ocupado e OOM sem estatística parcial apresentada como completa |
| HTTP de produção | Saída equivalente, blocos até 1.024 B, falha em cada ponto de envio libera a árvore; `/api/health` sem alocação cJSON; OOM de `/api/state` retorna informação reduzida de RAM |
| Preparação real de lote da ponte | Uma alocação exata; JSON equivalente, oito sequências exatas; OOM, IDs inválidos, repetidos e fora de ordem rejeitados sem vazamento |
| Sensores compactos e HEALTH sob OOM | Tuplas completas com valor/idade/MID ou leitura omitida; sem deslocamento de posição nem vazamento |
| Diagnóstico de armazenamento ocupado | Backend pronto continua identificado como pronto/ocupado; quantidade desconhecida permanece nula; consulta posterior recupera a quantidade válida |
| Navegador com HTML real e servidor local simulado | 19 verificações passaram; painel público/admin, configuração, OTA, intertravamentos J1708, paginação, RAM parcial, parada, logout e layout móvel |
| Suíte existente `tests/run_all.sh` | Passou integralmente: protocolos, armazenamento/CRC/recuperação/worker, justiça de CPU, perfil, J1939, equivalência Python/C, J1587, viagem/combustível, captura, GPS, telemetria, J1708 ativa/UART e Link |
| Testes Python de pacote/configuração/painel/combustível | 24 passaram |

Os testes de requisição simulam o barramento e não certificam forma de onda, arbitragem ou uma resposta causal no caminhão. Os testes do navegador usam credenciais fictícias e não gravam nenhuma placa.

## Duas horas offline e reconexão

Dois cenários de **7.200 segundos virtuais**, com e sem relógio absoluto, usando os construtores de registros de produção, decoder J1587 e writer/recuperação/CRC/ACK reais em arquivos do host. O endpoint remoto é simulado.

| Medida | Sem horário absoluto | Com horário absoluto |
|---|---:|---:|
| Quadros de entrada | 433.624 | 433.624 |
| Requisições/respostas candidatas simuladas | 1.624 / 1.624 | 1.624 / 1.624 |
| Sensores no cache | 65 | 65 |
| Registros gerados | 5.904 | 5.904 |
| JSON original | 2.889.034 B | 2.953.439 B |
| Ocupação física dos segmentos | 1.587.067 B | 1.664.095 B |
| Orçamento inalterado | 1.818.624 B | 1.818.624 B |
| Reinícios com gravação interrompida | 14 | 14 |
| Descartes por espaço | 0 | 0 |
| Linhas únicas no servidor simulado após reconexão | 5.904 | 5.904 |
| Entregas, incluindo repetição após ACK perdido | 5.912 | 5.912 |
| Duplicações no servidor / pendências válidas finais | 0 / 0 | 0 / 0 |

Não houve gravação bruta contínua. As evidências de corrupção ficam preservadas: após os ACKs, 685.042 B e 685.560 B ainda ocupam os arquivos isolados nos respectivos cenários. Elas reduzem a autonomia futura até revisão explícita. O teste começa com o orçamento disponível e não garante duas horas quando a flash já contém pendências ou arquivos isolados.

A simulação equivale a tráfego de dois relógios virtuais; não é uma observação de duas horas na placa. GPS físico, falhas reais de alimentação e Core → Link → Supabase da candidata continuam pendentes. A consulta à Cloud descrita no guia comprova a atividade anterior da 0.2.12.

## RAM e repetição

Os ELFs 0.2.12 e 0.2.13 ocupam **201.620 B estáticos em RAM**, sem diferença de filas ou pilhas reservadas. A correção reduz alocações transitórias e elimina itens órfãos em erro; não promete RAM estática adicional. A diferença nativa de 70,2% considera apenas o construtor de captura, com estado sintético. A candidata acrescenta `heap_largest_block` para observar fragmentação/tamanho de alocação no hardware.

```sh
# Ativar ESP-IDF 5.5.2 antes.
bash tools/build_v020.sh core
sh tests/run_all.sh
# Playwright/Chromium disponíveis no ambiente:
node tests/web/test_flow.cjs
```

Resultados detalhados: `test_evidence/v0213_host.txt`, `v0213_browser.txt`, `v0213_browser.json`, `v0213_build_core.txt`, `v0213_build_link.txt`, `v0213_image.json` e `v0213_memory.json`. O pacote é verificado por manifesto/hashes e mantém fontes dos módulos irmãos. Validação física e instalação não foram executadas.
