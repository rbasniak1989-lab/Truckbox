# TruckBox Rev.B.1 — instruções de implementação para Codex Cloud

**Estado: preparação para integração, NÃO firmware novo pronto.** Esta branch contém o inventário e os módulos portáteis J1708, mas o binário original v0.1.0-b1 ainda não foi alterado.

## Fonte estável e prioridade de implementação

- Firmware que já comunica fisicamente: branch `firmware-revb1-ota-v0.1.0-b1`, pacote `firmware/releases/v0.1.0-b1/truckbox-b1.zip`.
- Hardware Core: ESP32-C6 (flash 8 MB), ESP-IDF 5.5.2, conforme README do pacote; identificar a configuração *exata* a partir do fonte real e dos logs de build.
- Criar código-fonte extraído e rastreável do Core/Link nesta branch, com instruções de build reproduzível. Não substituir a base que já comunica, não presumir GPIO, MID, transceiver, ou pinagem.
- Preservar app Motorista e Multimídia e seus protocolos existentes; versionar mudanças de formato para manter compatibilidade.
- Esta branch: `feature/revb1-j1708-17-pids`. PR #4 fica em rascunho até testes completos.

## FONTES J1939 RECUPERADAS — PRIORIDADE MÁXIMA PARA O CODEX

O Documento Mestre **existe** e foi recuperado da biblioteca histórica do usuário. Não declarar bloqueio por falta dele.

Arquivos agora presentes nesta mesma branch, em `firmware/j1939/references/`:
1. `DOCUMENTO_MESTRE_J1939_v1.1_2026-08-11.md` — transcrição integral em Markdown da versão 1.1 (11/08/2026), **incluindo suas 31 tabelas** de bytes, offsets, escalas, enumerações, PGNs proprietárias Volvo, tratamento de aliases e pendências.
2. `truckbox_j1939_parser_v1.py` — **implementação Python de referência**, ~737 linhas, já testada historicamente contra os dois Logs Mãe e que implementa muitas dessas regras.
3. `VALIDACAO_LOGS_MAE_PARSER_V1.md` — relatório dos testes dos Logs Mãe carregado e vazio, 11 testes originais, faixas operacionais, IDs, fatores de consumo, registros corrompidos e ressalvas.

**Limite real:** os dois ZIPs originais do Log Mãe foram pesquisados na biblioteca desta conversa, mas ainda não foram localizados como arquivos acessíveis:
- `TruckBox_2026-08-02_23-06-38(1).zip` — carregado, ~9,08 milhões de frames.
- `TruckBox_2026-08-03_23-21-21.zip` — vazio, ~9,85 milhões de frames.
Não afirmar que foram reproduzidos no Codex nem que estão ausentes de todas as fontes; solicitar upload se replay completo depender deles.

**Instruções obrigatórias antes de manter 47 sinais J1939 como nulos:**
- Comparar os 76 itens do CSV de inventário, um por um, contra as 24 seções de decodificações detalhadas do Mestre v1.1 e os decoders Python. Gerar matriz de cobertura `item/PGN → offset/escala/enum → evidência → status → implementação → teste`.
- Portar todas as fórmulas e estados **realmente fundamentados**, e criar testes de regressão reproduzíveis de cada campo contra exemplos, limites e códigos de indisponível do Mestre e do relatório. Não limitar implementação aos 29 itens que estavam no repo antes deste resgate.
- O parser de referência foi baseado originalmente na v1.0; confrontar correções e acréscimos da v1.1, em particular a calibração **2643 Nm** e fator **C0=0,9720031611625598** (PROVISÓRIO), o P5 da curva VEB 65249 em B18, e o tratamento específico de aliases PGN+SA.
- O relatório validou historicamente 27 PGNs úteis como decodificadas/tratadas; **61 sinais rotulados 'Validado' no inventário NÃO significam necessariamente 61 fórmulas independentes**. Alguns são estimativas, cálculos derivados, sinônimos ou cópias auxiliares. Não converter N/A para valores numéricos sem prova.
- Manter explicitamente sem interpretação os bits proprietários não fechados: 61441 ABS nominal, partes 65282/65286/65292, 65317 D0/D2, timezone 65254, Engine Gain Kp/limites 65251 e demais itens parcial/indisponível.
- Preservar logs originais e proveniência em cada leitura, e distinguir `VALIDADO_NOS_LOGS` de validação física no caminhão.

**Entrega exigida:** depois de revisar estas fontes, atualizar no PR o número de campos realmente implementados e de campos ainda nulos, com suas causas. O número antigo **29/76** é ponto de partida, não uma limitação técnica permanente, mas nunca prometer 76/76 sem evidência.

## Entrada J1939 — inventário que deve ser implementado

Arquivo **`firmware/j1939/INVENTARIO_J1939_76_SINAIS.csv`** contém os **76 sinais**, com:
`id, pgn, sinal, descricao, status_fonte, uso, prioridade, observacao, instrucoes_integracao, criterio_validacao`.

Base original: `TruckBox_Mapa_Sinais_Explicado_Prioridades_2026-11-06.xlsx`, planilha `1939 - Passivo`, baseada no Documento Mestre J1939 v1.1 (11/08/2026) e em capturas reais do Volvo FH 540.

- 61 **Validados nos logs**: criar decodificadores conforme evidência documentada, com qualidade **VALIDADO_NOS_LOGS**, não confundir com aferição física.
- 9 **Parciais**: decodificar por fórmula/bitmask quando houver evidência, rotular **EM_VALIDACAO**; nunca promover a VALIDADO por compilar.
- 2 **Pendentes**, 3 **Indisponíveis**, 1 **Sem decodificação**: manter os frames brutos e status correspondente; **não produzir valores sintéticos nem zero como valor válido**.
- São **76 informações derivadas/agrupadas em 29 PGNs com campos catalogados**, não 76 PGNs únicas. O inventário bruto inclui outras mensagens e aliases da captura.
- Não confundir aliases gerados pelo logger Android com PGN verdadeira da rede: trabalhar a partir da ID CAN 29-bit e reconstruir PGN/SA/PF/PS. Rever especialmente o mapa `aliasByPgnSa` em `app/src/main/java/br/com/truckbox/app/parser/TruckBoxJ1939Decoder.kt`; ele evita duplos no app, mas não comprova que a PGN da origem seja a versão traduzida.
- Rever referência Android `TruckBoxJ1939Decoder.kt` e `TruckState.kt`, comparando fórmulas com o Documento Mestre, e reproduzir somente comportamento comprovado no Core.
- Preservar sinais de fabricante Volvo isolados de sinais J1939 padrão; informar fonte, MID/SA, PGN/SPN (se confirmado), bytes, unidade, timestamp, idade, qualidade e versão do decoder.
- Sem novas solicitações J1939: **não há resposta ativa J1939 confirmada** no inventário. PGN65213/FEE5 ventoinha deve ficar marcada como investigação; não transmitir pedido automaticamente.

## Campos sensíveis que NÃO podem virar 'validados' por conveniência

- **PGN 65258:** 1F e 2F descrevem o **mesmo bogie de tração**; jamais somar como se fossem eixos adicionais.
- **PGN 65295:** peso total é **estimativa da eletrônica**, não leitura de balança e não pode ser anunciado como peso legal.
- **PGN 65262:** temperatura de combustível ainda parcial — pode reproduzir leitura da água; não exibir como medição independente.
- **PGN 65263:** nível de arrefecimento parcial; pressão do arrefecimento e campo blow-by auxiliar indisponíveis. Pressão do cárter em campo conhecido é separada de blow-by auxiliar indisponível.
- **PGN 65270:** restrição do filtro de ar frequentemente zerada; não criar alerta antes de validação física.
- **PGN 61442:** patinação da embreagem é derivada; estados internos da transmissão parciais.
- **PGN 65249:** referência de torque VEB sem escala física fechada.
- **PGN 65132:** estado de jornada do motorista com enum incompleta.
- **PGN 65317:** faróis e iluminação requerem teste de estado (D0/D2), não afirmar ativo por byte isolado.
- **PGN 65110/65320:** SCR/ARLA indisponível / mensagem Volvo desconhecida.
- **PGN 65217:** distância de viagem é DELTA de contador total, com marcos de início/fim e correção de reinício; não apresentar hodômetro como trecho.
- **PGN 61444:** potência é **derivada** de RPM/torque usando referência da calibração, não medição direta. A ECU deste Volvo foi modificada: não presumir que 540 hp é valor de calibração real.
- Qualidade, range checks, códigos SAE "not available"/"error", timeout e reset de contador são obrigatórios. Nunca gravar dado inválido como número válido.

## CHECAGEM HARDWARE CRÍTICA: J1708 pode transmitir via DE (DI fixo em zero)

**Não declarar a Rev.B.1 RX-only somente porque DI (U4 pino 4) está ligado ao GND.** O projeto da Rev.B pretende implementar transmissão *dominante/recessiva* usando o enable do driver, não DI como entrada de TX padrão:

- Projeto: branch `hardware-revb-j1708-4g`, `hardware/reva/apply_revb_j1708.py`, linhas 10–14 e 130–155: U4 = SN65HVD1781-Q1, `/RE=GND` (RX habilitado), `DI=GND`, `DE=J1708_DE`, `RO=J1708_RX`, 10k pulldown no DE; UART TX **invertido** deve acionar `DE`.
- Quando DE=0, saídas A/B alta impedância (recessivo); quando DE=1 com DI=0, A/B são forçadas ao estado oposto (dominante). Fonte: TI SN65HVD1781-Q1 datasheet (Device Functional Modes/Driver Function Table).
- A mesma rotina liga `J1708_DE` ao **pad 19 do U1**. ATENÇÃO: há erro no comentário do script que diz `GPIO14` de ESP32-C6-MINI-1; o BOM final `TruckBox_RevB1_FINAL_Run318_BOM.csv` especifica **ESP32-C6-WROOM-1-N8** e o pinout oficial Espressif define **pad 19 = IO21 / GPIO21**, NÃO GPIO14.
- CONFIRMAR pela **placa Rev.B.1 FINAL Run318 ou esquemático/netlist final + multímetro de placa desligada** a continuidade física `U1 pad19 ↔ U4 pin3 (DE)`, pulldown R4 e inexistência de conflitos com outros periféricos. Script de projeto demonstra intenção; não substitui verificação do produto fabricado.
- Se confirmado, configurar a UART TX mapeada ao **GPIO21** (ou GPIO real que o netlist final comprovar), TX invertido, 9600 8N1 conforme J1708 e temporização de espera de barramento / prioridade / monitor de colisão via RO. Nunca usar GPIO14 só porque o comentário do script o afirma.
- Com GPIO21/DE comprovado, DI=0 **não impede envio de requisições**: o TX controla habilitação do driver, não os dados em DI. Se não houver continuidade real no PCB fabricado, classificar TX como bloqueado por hardware e documentar a correção física necessária.
- Antes de transmissão: teste OFF-VEHICLE em bancada, com osciloscópio/analisador lógico no DE e par diferencial A/B isolado do veículo; confirmar idle/recessivo, bits dominantes, recepção, eco e checksum. Em seguida apenas teste estacionário, com limite de cadência e botão/caminho de corte imediato. Não habilitar TX operacional sem esses testes.

**Estado desta hipótese:** circuito planejado como bidirecional, mas a capacidade TX da peça Rev.B.1 montada NÃO está fisicamente comprovada. Não alterar firmware estável, nem soldar jumper sem comprovar continuidade/pinagem.

## Entrada J1708/J1587 — módulos já criados nesta branch

- `firmware/j1708_17pid/src/tbx_j1587_passive.c/.h` — 48 PIDs catalogados, 17 números básicos.
- `firmware/j1708_17pid/src/tbx_j1587_passive_full.c/.h` — mais 31 decodificações de estados, temperaturas, falhas e formatos especiais.
- `firmware/j1708_17pid/src/tbx_j1587_decoder.c/.h` — 17 PIDs solicitados com escalas SAE provisórias e bytes originais.
- `firmware/j1708_17pid/src/tbx_j1708_requests.c/.h` — fila dos 17, timeout inicial 450 ms, rate limit, desarme.
- Não ativar requisições sem supervisão, principalmente em viagem. Teste físico com caminhão parado e desligamento imediato em falha de link.

## Arquitetura mínima do Core

1. **RX** passivo J1939 e J1708 continuamente, reaproveitando drivers estáveis.
2. **Parsers** com separação de enquadramento, payload e dados de controle: ambos passam frames brutos e metadados aos decoders.
3. **Decoders** modulares por rede, preservando raw, origem, timestamp, versão de escala e estado de validação.
4. **Agendador J1708** independente da RX; não permitir que solicitações bloqueiem logging/gravação no SD.
5. **Fila persistente SD** com limites e proteção contra reinício / atraso; reenvio e ACK transacional devem continuar funcionando.
6. **API para Android** com campos antigos preservados e novos campos versionados. Para grandezas duplicadas: preferir J1939 validado, registrar J1708 como comparação; nunca somar peso/cálculos duplicados.
7. **Diagnósticos**: estatísticas RX/TX, taxa de frames, descartes, erros checksum, MID/SA, percentis de latência e falhas, heartbeat para observar comportamento antes/depois.

## REQUISITO OBRIGATÓRIO: coletar e salvar dados MESMO SEM GPS / ANTENA

**Situação de teste:** a antena GNSS ainda não chegou. O firmware Core v0.2.0 deve operar normalmente com o GPS ausente, sem fix, desconectado, não inicializado ou retornando erro.

- Inicialização da recepção **J1939 e J1708**, decodificação, agendamento J1708 autorizado, logger e **gravação persistente no microSD** devem ser completamente independentes do GNSS. Nenhuma espera bloqueante por GPS, satélites, fix ou horário GNSS na inicialização nem na coleta.
- **GPS opcional**: manter `gps_status=UNAVAILABLE/NO_FIX`, `has_fix=false` e latitude/longitude/altitude como valores nulos/ausentes. Não fabricar coordenadas `0,0` (isso produz ponto falso no Golfo da Guiné), velocidade GPS zero ou tempo satelital inexistente.
- Cada frame/sensor deve receber **sequência persistente/identificador e timestamp monotônico de captura**, inclusive sem relógio absoluto válido. Registrar a **fonte e qualidade** do horário: RTC/horário obtido da rede do caminhão/Android quando disponível, ou uptime monotônico relativo ao boot com `time_valid=false`; não inventar data/hora absoluta e não usar data de compilação como horário real.
- Não impedir viagem, contadores, médias, armazenamento ou transmissão de telemetria por falta de posição GNSS. Distância/velocidade podem vir das redes veiculares com suas fontes e qualidade; quando GPS não existir, **não criar mapa/rota fictícios**.
- O buffer persistente e o fluxo `Core → Android → Cloud` precisam aceitar dados sem localização. Enviar `gps=null`/campo omitido mantendo compatibilidade; não descartar lote de sensores por não haver lat/lon, nem mudar sua sequência/ACK. Quando a antena funcionar, associar posições **somente a partir dos fixes reais**; jamais retroatribuir posição aos registros anteriores.
- Se microSD ou memória persistente falhar, reportar erro real e preservar o máximo possível por fila limitada; **não afirmar que gravou**. Após reboot sem GNSS, continuar adquirindo dados, registrar boot_id, lacunas e estado de recuperação.
- Separar claramente as tarefas de RX/SD/Sync das de GPS. Falha, ausência ou timeout GNSS **não pode bloquear nem reiniciar os drivers J1939/J1708**.

### Testes obrigatórios sem GNSS

1. Inicializar Core com antena desconectada: verificar RX nas duas redes e persistência de frames/valores no microSD; `gps_status=NO_FIX`.
2. Registrar período prolongado sem fix, inclusive reiniciando o Core; validar sequência, continuidade e `time_valid=false` quando aplicável.
3. Receber dados pelo Android/Cloud sem GPS: confirmar aceitação de payload sem localização, ordenação, ACK e replay/reenvio após queda de internet.
4. Ligar GPS depois de a gravação estar em andamento: fix real habilita apenas novos registros geográficos; manter histórico antigo sem posição.
5. Simular GPS que trava/retorna erro durante coleta: telemetria e microSD continuam; sem lat/lon fictícias nem reinicialização do Core.

**Critério de aceite prioritário:** o Core deve ser utilizável como data logger J1939/J1708 **mesmo que nenhuma antena GNSS seja instalada**.

## Critérios de aceitação — não dar como concluído sem evidência

- [ ] Fonte real do firmware v0.1.0-b1 extraído da release, revisão de CMake/SDK/pinagem, compilação reproduzível ESP-IDF.
- [ ] Compilar os decoders J1939 para as **76 linhas**, respeitando a coluna `status_fonte`; relatório de cobertura por ID com teste de fixture real ou ausência fundamentada.
- [ ] Compilar e integrar os decoders J1708 e 17 requisições, sem modificar comunicação funcional além do necessário.
- [ ] Testes unitários (incluindo `J1708 J1587 decoder unit tests` já existente) e replays de logs sem perdas, regressões ou dados inventados.
- [ ] Serializar e persistir dados brutos + decodificados com status e idade; compatibilidade comprovada com app Android.
- [ ] Testes somente em bancada/caminhão parado com taxas TX/RX e erros monitorados; nenhuma transmissão ativa J1939 não validada.
- [ ] Gerar `.bin` com SHA256 e guia de flash/rollback, preservando **v0.1.0-b1**; não instalar, publicar como estável nem fazer merge sem validação.

**Nota ao Codex:** existe uma base Android com parte da J1939 já decodificada, mas **NÃO** existe ainda integração dos 76 sinais ao firmware ESP-IDF do Core nesta branch. A tarefa é implementar essa integração, não apenas editar os arquivos de referência.
