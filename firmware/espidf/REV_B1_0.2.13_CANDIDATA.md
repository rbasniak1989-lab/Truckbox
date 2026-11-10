# TruckBox Core Rev.B.1 0.2.13 — candidata: memória do diagnóstico

Correção incremental da 0.2.12, preservada na branch `feature/revb1-v0212-reliable-j1708` e na release anterior. Desenvolvimento em `fix/revb1-v0213-http-memory`. Instalação manual somente no **Core**. O Link instalado continua compatível e não precisa ser atualizado.

## Problema e evidência

O Core 0.2.12 do usuário retornou intermitentemente `{"ok":false,"error":"Sem memória disponível."}` em `/api/state`. Uma consulta posterior no boot 268 mostrou 67.504 B livres, mínimo de 17.848 B, 37.525 quadros J1708, zero descartes da fila RX e 125 lotes confirmados. Portanto, essa mensagem identificava falha ao alocar o diagnóstico JSON em RAM; ela não provava que a flash estivesse cheia. O maior bloco contíguo ainda não era informado nessa versão.

Uma consulta somente de leitura à Cloud confirmou HEALTH da 0.2.12/boot 268 com aproximadamente 3.900 quadros J1708 por minuto e chegada dos registros cerca de 4–6 segundos após a janela. Nenhuma tabela, credencial ou viagem foi modificada. Essa evidência é da imagem anterior, não valida fisicamente a candidata 0.2.13.

O código anterior construía toda a árvore cJSON, depois alocava uma segunda cópia crescente para enviar a resposta. A ponte também imprimia um lote inteiro em outro buffer crescente. Foram encontrados caminhos de inserção manual de nós que não liberavam o item quando a alocação de uma chave falhava. Os testes novos reproduzem a falha do impressor antigo sob orçamento baixo e verificam a liberação de memória em cada falha simulada. Não há rastreamento físico do ponto exato da alocação que falhou no veículo.

## O que mudou

- Respostas JSON HTTP usam blocos de até 1.024 B em pilha, sem alocar uma segunda representação completa; liberam progressivamente os nós já emitidos. Falhas de rede também liberam toda a árvore.
- `/api/state` completo continua disponível. O painel usa `/api/state?compact=1`, mantendo os estados, intertravamentos e contadores agregados. Os 17 detalhes são consultados por GET autenticado `/api/j1708/requests?offset=0&limit=8`, depois offsets 8 e 16. `last_frame`, `pids` e `stats` completos continuam no endpoint original. Uma página incompleta por falta de RAM é rejeitada, não apresentada como estatística completa.
- `/api/health` fornece versão, identidade e RAM livre/mínima/maior bloco sem alocação cJSON. A resposta reduzida não contém sensores nem garante que a aquisição esteja funcionando. O HTTP/TCP ainda depende dos recursos do sistema.
- Se o diagnóstico completo falhar, o painel tenta essa consulta reduzida, informa a limitação e invalida informações antigas antes de permitir novos testes ativos. A parada de transmissão permanece acessível ao administrador. Configuração e OTA continuam sujeitas à autenticação existente.
- Preparação de lote para o Link usa uma única alocação do tamanho exato. Os IDs oferecidos, limites de lote e validação do ACK são preservados; não é confirmado nenhum registro por essa mudança de serialização.
- Inserções de itens JSON liberam itens rejeitados, inclusive em erro de alocação. Arrays de sensores mantêm integralmente `[valor/bruto, idade, MID, bruto opcional]`; falta de RAM não pode deslocar a idade para a posição do valor. O mesmo vale para os arrays legados de HEALTH.
- Consulta de armazenamento sem obter o mutex informa `busy=true`, `pending_known=false` e quantidade nula. Se o backend já estava pronto, não o declara avariado. Isso não substitui a recuperação de corrupção; falhas reais continuam reportadas pelo armazenamento.

Não foram reduzidas a fila de gravação, as cadências de coleta ou a retenção offline. O formato local `z2`, sua versão/dicionário, NVS, identidade, credenciais, partições, viagens, interpretação dos sensores e regras de requisição permanecem compatíveis com a 0.2.12. J1939 continua desativada no perfil J1708_ONLY. microSD, FRAM e modem não são habilitados.

## Testes e memória

Resultados reproduzíveis e limites estão em [TESTES_v0.2.13.md](TESTES_v0.2.13.md), com logs em `test_evidence/v0213_*`. Compilação usa ESP-IDF **5.5.2**, ESP32-C6, flash de 8 MB e a tabela de partições preservada. O pacote contém aplicação OTA, fontes completos, símbolos ELF/mapa, hashes e ferramentas Windows.

Na fixture nativa que chama o construtor real do diagnóstico de captura, a árvore completa ocupa 22.811 B, contra 6.805 B compacta: redução de 70,2% **nessa parte do JSON sintético no computador**. Não equivale a 70,2% da RAM do ESP32. A economia física depende de tráfego, maior bloco livre e concorrência do upload; a candidata ainda precisa ser observada no hardware.

## Atualizar manualmente

1. Com o veículo estacionado e alimentação estável, baixe e confira o hash de `truckbox_core_0.2.13_ota.bin`. Não use ZIP, bootloader ou partições como aplicação OTA.
2. Abra a página do Core pelo roteador ou AP, autentique-se como administrador, selecione **Core/neste dispositivo** e envie esse `.bin`. O pacote não dispara nenhuma atualização.
3. Após o reinício, confira `version=0.2.13`, `capture_profile=J1708_ONLY`, recepção J1708 e continuidade dos ACKs. Reabra o painel para carregar o HTML atualizado. Consulte `/api/health` se houver limitação de RAM e compare `heap_largest_block`, `heap_free` e `heap_minimum` durante coleta + sincronização + acessos repetidos.
4. Verifique `storage.phase`, `pending_known`, fila e `bridge.last_cloud_ack_age_ms` em várias consultas. `BUSY` significa consulta concorrente; não significa fila vazia. Não apague registros para resolver uma mensagem de RAM.

Para primeira instalação/recuperação USB, o ZIP contém `flash_core.cmd COM3`, verificação de imagem e tamanho real da flash. A gravação USB inclui regiões de boot/OTA e não é a opção normal para esta correção. Consulte também [o guia anterior](REV_B1_0.2.12_CANDIDATA.md).

## Requisições ativas e rollback

As duas respostas candidatas do PID 26 no diagnóstico do usuário não substituem confirmação externa da transmissão. Continua necessário: teste único estacionado, evidência externa e autorização explícita do modo de viagem. Não começa automaticamente após atualização ou reinício. Pinagem, MIDs, cadências de 1/5/15 minutos e parada após três falhas estão descritos no guia 0.2.12 e não mudaram nesta correção.

Rollback **para 0.2.12**: enviar manualmente sua aplicação OTA anterior. As duas versões compartilham o mesmo codec local e configurações; não é preciso esvaziar nem apagar os registros para essa troca. A atualização/rollback reinicia a placa e invalida autorizações ativas. O rollback automático do bootloader continua habilitado.

Para **0.2.11 ou anterior**, permanece obrigatório o procedimento de manutenção/pausa e sincronização do guia 0.2.12, pois essas imagens não leem os registros compactados `z2`. Não trocar partições durante OTA nem apagar pendências.

## Pendências reais

Observação prolongada de RAM/pilha e repetidos acessos no Core real; interrupções reais de energia; transmissão elétrica J1708 comprovada externamente; autonomia e reconexão na placa. A estabilidade física da alimentação e o circuito do cartão exigem correção de hardware. Não foram reprocessados os ZIPs originais completos dos Logs Mãe, que continuam indisponíveis.
