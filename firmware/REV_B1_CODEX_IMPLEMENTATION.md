# TruckBox Rev.B.1 — instruções de implementação para Codex Cloud

**Estado: preparação para integração, NÃO firmware novo pronto.** Esta branch contém o inventário e os módulos portáteis J1708, mas o binário original v0.1.0-b1 ainda não foi alterado.

## Fonte estável e prioridade de implementação

- Firmware que já comunica fisicamente: branch `firmware-revb1-ota-v0.1.0-b1`, pacote `firmware/releases/v0.1.0-b1/truckbox-b1.zip`.
- Hardware Core: ESP32-C6 (flash 8 MB), ESP-IDF 5.5.2, conforme README do pacote; identificar a configuração *exata* a partir do fonte real e dos logs de build.
- Criar código-fonte extraído e rastreável do Core/Link nesta branch, com instruções de build reproduzível. Não substituir a base que já comunica, não presumir GPIO, MID, transceiver, ou pinagem.
- Preservar app Motorista e Multimídia e seus protocolos existentes; versionar mudanças de formato para manter compatibilidade.
- Esta branch: `feature/revb1-j1708-17-pids`. PR #4 fica em rascunho até testes completos.

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

## Critérios de aceitação — não dar como concluído sem evidência

- [ ] Fonte real do firmware v0.1.0-b1 extraído da release, revisão de CMake/SDK/pinagem, compilação reproduzível ESP-IDF.
- [ ] Compilar os decoders J1939 para as **76 linhas**, respeitando a coluna `status_fonte`; relatório de cobertura por ID com teste de fixture real ou ausência fundamentada.
- [ ] Compilar e integrar os decoders J1708 e 17 requisições, sem modificar comunicação funcional além do necessário.
- [ ] Testes unitários (incluindo `J1708 J1587 decoder unit tests` já existente) e replays de logs sem perdas, regressões ou dados inventados.
- [ ] Serializar e persistir dados brutos + decodificados com status e idade; compatibilidade comprovada com app Android.
- [ ] Testes somente em bancada/caminhão parado com taxas TX/RX e erros monitorados; nenhuma transmissão ativa J1939 não validada.
- [ ] Gerar `.bin` com SHA256 e guia de flash/rollback, preservando **v0.1.0-b1**; não instalar, publicar como estável nem fazer merge sem validação.

**Nota ao Codex:** existe uma base Android com parte da J1939 já decodificada, mas **NÃO** existe ainda integração dos 76 sinais ao firmware ESP-IDF do Core nesta branch. A tarefa é implementar essa integração, não apenas editar os arquivos de referência.
