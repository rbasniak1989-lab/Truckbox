# Core Rev.B.1 0.2.14 — candidata: Validação supervisionada em campo

Base: Core 0.2.13, preservado na branch `fix/revb1-v0213-http-memory`. Desenvolvimento separado em `feature/revb1-v0214-field-validation`. ESP32-C6, ESP-IDF 5.5.2, flash 8 MB. Atualização manual somente do Core; Link instalado compatível.

A alternativa atende à instalação em que NEXIQ e TruckBox não podem permanecer conectados juntos. O mapeamento do Volvo FH 540 2013 continua sendo o da captura NEXIQ de 29/10/2026. Não há novos GPIOs, alterações de driver ou contorno da topologia DE21/transceiver.

## Duas opções distintas

**Validação externa independente:** mantém o teste único estacionado, os campos TX/resposta observados pelo analisador e a confirmação externa com checksum, quadro e nonce do teste. Continua sendo a única opção que pode definir `physical_validated=true`.

**Validação supervisionada em campo:** mede o comportamento recebido pelo próprio Core, compara períodos passivos com janelas de requisição e solicita confirmação do operador. O resultado é **operacional indireto**, não comprovação elétrica externa nem prova causal de cada resposta. Não preenche `tx_observed_hex`, `response_observed_hex` ou qualquer declaração externa.

A consulta somente de leitura à Cloud confirmou, no boot 270, 17 REQUEST para 17 PIDs, 17 TX_ECHO completos, 17 RESPONSE_VALUE com MID igual ao alvo e 17 RESPONSE_CANDIDATE, sem eventos TIMEOUT desse boot. Esse histórico não foi convertido em autorização nem importado como medição do novo protocolo. Uma sessão nova é obrigatória na candidata.

## Como executar no caminhão

1. Estacione, mantenha a alimentação estável e os módulos necessários à leitura ligados. Atualize manualmente o **Core** com `truckbox_core_0.2.14_ota.bin`; reabra a página para carregar o painel novo e confira a versão.
2. Autentique-se. Confira a pinagem/topologia Rev.B.1 e escolha um MID local verificado como livre. A lista de MIDs observados ajuda, mas a ausência de um MID não prova que esteja livre. Os MIDs de destino vêm do preset NEXIQ; não há MID local escolhido automaticamente.
3. Marque a autorização para **Validação supervisionada em campo** e clique em iniciar. O firmware exige velocidade zero recente recebida do veículo, além da declaração de caminhão estacionado. Permaneça parado e acompanhe o resultado, sem enviar testes paralelos.
4. Aguarde: janela passiva anterior nominal de 30 s; três rodadas dos 17 PIDs, cada pedido precedido por uma janela sem transmissão; janela passiva posterior nominal de 30 s. Normalmente leva cerca de três minutos. A autorização do ensaio termina em dez minutos. A ordem dos PIDs e os intervalos variam entre rodadas para reduzir coincidências de fase com mensagens periódicas.
5. Revise contadores, janelas de controle, MIDs e latências. Se aparecer **Resultado operacional indireto satisfatório**, marque a aceitação explícita da evidência indireta e confirme. Resultado inconclusivo ou interrompido não libera o modo de viagem. A confirmação exige o ID da sessão deste boot, velocidade zero recente e MID local sem conflito.
6. Ainda parado, clique em **Ativar durante a viagem** e autorize. O agendamento passa a funcionar automaticamente nas cadências abaixo, sem interação do motorista enquanto dirige. A autorização dura até 12 h e termina no reinício/OTA/parada. Não há rearme automático.

A recepção passiva e a gravação continuam durante todas as etapas, inclusive após a interrupção das requisições. GPS e internet não são requisitos do ensaio. As confirmações e registros usam boot, sequência e relógio monotônico. A aceitação operacional não valida automaticamente escalas, odômetro ou contador de combustível PID 250; as qualidades e configurações desses sensores são preservadas.

## Critério operacional desta candidata

- Pelo menos 60 quadros passivos com checksum válido na janela inicial; silêncio de dez segundos ou degradação persistente de checksum impede a aprovação.
- Cada um dos 17 PIDs precisa completar **três pedidos, três ecos UART completos e três respostas candidatas**, todas com MID/PID esperados, checksum/layout válidos e dentro do timeout configurado (450 ms inicialmente).
- Cada PID tem três janelas sem transmissão, com duração igual ao timeout. Um PID apresenta contraste quando respondeu nos três ensaios e apareceu em no máximo uma dessas três janelas de controle.
- São necessários **pelo menos 9 dos 17 PIDs com contraste**. Este limiar de maioria é uma decisão explícita do protocolo operacional da candidata, não um critério de certificação SAE nem um cálculo de significância estatística. PIDs que também aparecem em duas ou três janelas de controle permanecem identificáveis pelos seus contadores; suas respostas continuam candidatas com sobreposição passiva.
- Se nem a maioria apresentar contraste, o resultado será **inconclusivo**, mesmo com 51 ecos e 51 mensagens coincidentes. Não existe botão para transformar esse resultado em sucesso.
- A taxa passiva após o teste deve ficar entre metade e o dobro da taxa anterior. Durante a supervisão e o modo de viagem indireto, a observação em janelas de cerca de dez segundos interrompe TX por silêncio, aumento de checksum acima de 1% com mais de três erros, ou taxa fora de um quarto a quatro vezes a referência. A comparação utiliza as durações efetivamente medidas.
- Colisão/eco divergente, falha UART, timeout ou perda dos registros de auditoria interrompem o ensaio de validação. No modo de viagem, permanecem a parada por três falhas consecutivas e a supervisão adicional do barramento. Conflito do MID local, movimento/velocidade desatualizada durante o ensaio, OTA, fim da autorização e parada manual também encerram TX.

Os tempos de latência são medidos pelo relógio do software entre o início do pedido e a resposta já recebida/interpretada. Não são medidas de borda elétrica ou de propagação do transceiver. A captura bruta da resposta e seu checksum são preservados nos eventos do ensaio; não há armazenamento bruto contínuo de todo o tráfego.

## Cadências preservadas

| PID | MID alvo NEXIQ | Intervalo de viagem |
|---:|---:|---:|
| 26 | 128 | 1 min |
| 31, 32, 33, 34, 37, 63, 160 | 130 | 1 min |
| 62 | 144 | 1 min |
| 68, 158 | 128 | 1 min |
| 117, 118 | 140 | 1 min |
| 108, 250 | 128 | 5 min |
| 235, 236 | 128 | 15 min |

Permanece um único agendador central, um pedido em andamento, espaçamento mínimo de 1,2 s e arbitragem por barramento ocioso/monitor RX já existente. O perfil J1708_ONLY, os sensores passivos e os contadores acumulados não mudaram.

## Diagnóstico e registros

`/api/state?compact=1` informa `j1708.requests.validation_method` (`NONE`, `EXTERNAL_INDEPENDENT` ou `SUPERVISED_FIELD_INDIRECT`) e `field_validation`: fase, ID da sessão, ensaios concluídos, quantidade de PIDs com contraste, quadros/erros antes e depois, durações medidas e perda de auditoria. `field_validation.external_electrical_proof` permanece falso; o campo externo independente é `physical_validated`.

GET autenticado `/api/j1708/requests?offset=0&limit=4` retorna comparações por PID. O painel consulta cinco páginas pequenas para limitar RAM. Os eventos `FIELD_*`, `REQUEST`, `RESPONSE_VALUE` e `RESPONSE_FRAME` seguem o armazenamento/ACK existente para a Cloud. Janelas sem transmissão não recebem quadros TX inventados. Resultados por PID são enfileirados gradualmente; perda de evidência impede a aprovação. Aceitação na fila não é confirmação de gravação durável ou de recebimento na Cloud; continuam valendo fsync/CRC/seq/ACK existentes.

POST autenticado no mesmo endpoint aceita novas ações `start_field` e `confirm_field`, além de `arm`, `confirm_physical`, `enable_work` e `stop`. `confirm_field` exige `authorized`, `stationary`, `local_mid_free_verified`, `indirect_validation_acknowledged` e `session_id` correspondentes. Confirmação indireta não envia nem substitui campos de captura externa. O resultado para promoção expira em quinze minutos; reconfiguração, reinício e parada invalidam a liberação.

## Testes, limites e rollback

Veja [TESTES_v0.2.14.md](TESTES_v0.2.14.md), os arquivos `test_evidence/v0214_*` e o [changelog](CHANGELOG_v0.2.14.md). Testes sintéticos não são validação elétrica ou observação na Rev.B.1. Esta candidata não foi instalada automaticamente, não ativou requisições reais e não modificou viagens da Cloud.

NVS, identidade, credenciais, partições OTA, codec/dicionário local, recuperação e ACK foram preservados. microSD/FRAM/SIM não foram habilitados. Rollback manual para **0.2.13 ou 0.2.12** usa a aplicação OTA anterior e formato de armazenamento compatível. Para 0.2.11 ou anterior, siga o procedimento de pausa/sincronização da 0.2.12; não apagar pendências nem mudar partições. O bootloader continua com rollback automático habilitado.
