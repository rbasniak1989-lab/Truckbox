# TruckBox Core Rev.B.1 — 0.2.12 candidata

Base preservada: `8ee91d4a439c20a6932a758e85203c7625163325`, branch `feature/revb1-v0211-j1708-10s-dual-lane`, Core 0.2.11. Desenvolvimento separado em `feature/revb1-v0212-reliable-j1708`. ESP32-C6, 8 MB, ESP-IDF **5.5.2**. Destinado ao Volvo FH540 2013 I-Shift Euro5 informado pelo operador. Identidade continua derivada do ESP; nenhuma credencial, UID ou viagem é substituída.

**Versão candidata para revisão. Não instalada automaticamente. Validação elétrica de TX pendente.** O pacote contém somente aplicação Core para OTA; o Link instalado é mantido. Fontes dos dois papéis permanecem no projeto. Nenhum merge, alteração de viagens ou implantação de backend faz parte desta entrega.

## O que mudou

- Perfil `J1708_ONLY`: não instala TWAI, não cria tarefa CAN e remove decoder/TP J1939 da RAM. Código, catálogo e testes J1939 continuam no repositório. Coleta geral, CAN e J1708 possuem opções distintas; CAN não pode ser reativada por configuração neste binário.
- As 17 entradas estão habilitadas no agendador central. Primeiro ocorre **uma única requisição estacionada**. Viagem exige confirmar TX e resposta em captura externa, com MID livre, e autorizar explicitamente.
- Em viagem: cadências individuais, fases distribuídas no primeiro minuto, espaçamento mínimo de 1.200 ms, timeout configurável inicialmente 450 ms e uma requisição pendente. Ciclos perdidos não geram repetição de todas as amostras perdidas. Três falhas consecutivas, incluindo timeouts no modo de viagem, interrompem TX. Colisão libera DE imediatamente.
- Autorização de viagem somente em RAM, por até 12 horas; reinício/OTA não a restaura. Uma falha de TX não desativa RX passiva. Depois de bloqueio por três falhas, estacionar, investigar e reiniciar para um novo teste supervisionado.
- Registros operacionais e 27 PIDs essenciais a cada 10 s; demais observações passivas, fontes secundárias e estados indisponíveis são preservados em pacotes de até 767 bytes a cada 60 s. HEALTH a cada 60 s. Todos os 48 caminhos passivos e os 17 PIDs foram exercitados e exportados no ensaio. Estado multifield mantém bytes para interpretação; `/api/sensors` continua disponível.
- Cada requisição, resultado, timeout e erro é registrado. Eventos repetitivos de eco/candidata são agregados em contadores. Uma resposta temporalmente compatível continua classificada como **candidata**, mesmo após comprovar eletricamente o transmissor.
- Formato compactado local, reversível, independente por registro, com CRC de sequência e conteúdo. `/api/gateway/batch` expande para o JSON original antes do Link/Cloud. Campos operacionais e arrays J1587 existentes são mantidos; unidades, qualidade e contabilidade são aditivos.

## Cadências e MIDs

A tabela executável está em `../j1708_17pid/src/tbx_j1708_policy.h`; CSV em [sensors/j1708_17_v0212.csv](sensors/j1708_17_v0212.csv). Alvos vêm da documentação Validator/NEXIQ de 29/10/2026 já utilizada no preset. Não foi localizada decisão posterior de 25 minutos nos refs examinados; mantidas as cadências solicitadas de 1/5/15 minutos.

| PID | MID destino | Intervalo | Informação |
|---:|---:|---:|---|
| 26 | 128 | 1 min | Ventoinha |
| 31 | 130 | 1 min | Faixa da transmissão |
| 32 | 130 | 1 min | Splitter |
| 33 | 130 | 1 min | Cilindro da embreagem |
| 34 | 130 | 1 min | Atuador da embreagem |
| 37 | 130 | 1 min | Ar da transmissão |
| 62 | 144 | 1 min | Inibição do retardador |
| 63 | 130 | 1 min | Atuadores da transmissão |
| 68 | 128 | 1 min | Limitação de torque |
| 117 | 140 | 1 min | Ar primário |
| 118 | 140 | 1 min | Ar secundário |
| 158 | 128 | 1 min | Tensão com ignição |
| 160 | 130 | 1 min | Eixo principal da transmissão |
| 250 | 128 | 5 min | Combustível total da ECU |
| 108 | 128 | 5 min | Pressão atmosférica |
| 235 | 128 | 15 min | Horas de marcha lenta |
| 236 | 128 | 15 min | Combustível em marcha lenta |

Todos **EM VALIDAÇÃO**. MID local não tem valor padrão: escolher somente após conferir capturas e barramento. Não usar MIDs 128/130/140/144 ou outro MID observado. A ausência na lista local não prova endereço livre.

Intervalos persistentes podem ser alterados por POST autenticado em `/api/config`, por exemplo `{"j1708_intervals_ms":{"26":120000},"j1708_timeout_ms":450}`. Faixa de intervalos: 60.000–3.600.000 ms; timeout: 100–5.000 ms. Reiniciar e validar novamente antes de utilizar nova configuração. Não há autorização TX persistente.

## Validação física curta — veículo parado

1. Manter SD, FRAM e modem desligados. Conferir alimentação e EN; uma falha de alimentação continua exigindo reparo físico.
2. Técnico deve comparar a placa montada com [HARDWARE.md](HARDWARE.md), o fonte original e `hardware/reva/apply_revb_j1708.py`. Mapeamento comprovado do WROOM: RX20 e DE21; receptor permanentemente habilitado; DI documentado em GND. O código usa UART invertida em DE21 para dominante/recessivo. **DI baixo sozinho não prova impossibilidade de TX.** Confirmar continuidade, nível de DI, DE, RO, polarização e forma de onda A/B. Não propor cortes, GPIOs novos ou desvios de proteção sem essa medição.
3. Capturar a rede com NEXIQ/analisador externo, conferir MIDs ocupados e selecionar um MID local realmente livre. Não executar duas interfaces ativas com o mesmo MID.
4. Abrir o painel do Core, autenticar, aguardar velocidade zero recente, informar MID livre e confirmar verificação da topologia. PID 117 é a opção inicial por histórico de respostas; os outros 16 continuam configurados.
5. Autorizar **“Enviar uma requisição (parado)”**. Conferir quadro completo no par A/B e analisador externo: MID local, PID128, PID solicitado, MID destino, checksum, silêncio/arbitragem e temporização. Eco RX local sozinho não comprova TX externo.
6. Conferir resposta do MID/PID correto, checksum e tempo dentro do timeout. Colar os quadros TX/RX **externos**, em hexadecimal, nos campos do painel. Registrar validação; o Core compara bytes, checksum, teste/boot atual e velocidade zero. Essa confirmação é uma declaração do operador apoiada na captura, não uma medição elétrica autônoma do ESP.
7. Se não houver TX ou resposta compatível, manter viagem bloqueada. Salvar captura e medir DE21/RX20/DI/A/B para localizar o ponto defeituoso. Não aumentar timeouts ou trocar DI por suposição para disfarçar falha elétrica. Nenhum retrabalho específico foi validado neste ambiente.
8. Somente após sucesso, ainda parado, clicar **“Ativar durante a viagem”**. Não é necessário operar controles dirigindo. Em viagem o sistema consulta automaticamente; falha repetida desarma. “Parar requisições” continua disponível e libera TX.

`hardware_tx_available` significa interface/monitor de borda inicializados. O indicador de validação é separado. Os vetores com MID242 nos testes são **sintéticos**, não uma recomendação de endereço para o caminhão.

## Armazenamento, recuperação e capacidade

Partições e endereços OTA permanecem idênticos à 0.2.11. Partição FAT existente: `0x1e0000` bytes (1.875 MiB). Limite de dados passou de 24 para 37 segmentos de 48 KiB, mantendo pelo menos 128 KiB para FAT/WL e arredondamento. Também se verifica espaço real do filesystem antes de gravar.

Recuperação lê wrappers v1 da 0.2.11 e v2 novos, valida CRC/sequência, procura registros válidos após trechos incompletos, excessivos ou corrompidos e continua coletando/enviando. Não trunca o arquivo original. Sequências reservadas em NVS não são reutilizadas. Uma falha ambígua de fsync é recuperada antes de reenfileirar a amostra em voo, evitando duplicação local. O worker tenta recuperação do meio montado e montagem não destrutiva novamente; não formata um filesystem não vazio.

- Registros válidos pendentes são preservados e enviados em ordem. ACK exige sequência realmente oferecida e presente; não confirma lacunas nem bytes corrompidos.
- Somente arquivos sem corrupção integralmente confirmados são reaproveitados, conforme autorização do usuário.
- Arquivos com corrupção vão para `.BAD` depois que todos os registros válidos receberam ACK. O arquivo inteiro é preservado, sem sobrescrever outro arquivo `.BAD`. Não há exclusão automática dessas evidências.
- Durante falha de recuperação, `pending_records` e `pending_bytes` são nulos e `pending_known=false`. Zero registros **válidos** após recuperação não significa que os bytes isolados estejam resolvidos.
- `corrupt_records` conta ocorrências/regiões, não uma contagem exata de mensagens perdidas. `unrecoverable_records=null`; tamanho isolado e descartes antes da durabilidade ficam explícitos. Uma amostra aceita na fila RAM ainda não é durável.

**Teste de volume equivalente a duas horas, acelerado no host:** 60 quadros/s, 433.624 quadros processados, todos os 48 PIDs passivos e 17 solicitados, 1.624 requisições/respostas candidatas, 5.904 registros. Código real dos decoders, cache, serializers, codec, writer, recuperação, batch e ACK. Sem GPS, Android ou internet; servidor simulado após reconectar. Foram injetados 14 reinícios com gravação interrompida. Reconexão entregou 5.904 registros únicos, com perda deliberada de um ACK e repetição idempotente, sem perder ou duplicar registros no servidor simulado.

| Relógio durante o offline | JSON original | Arquivos gravados | Limite | Descartes por lotação |
|---|---:|---:|---:|---:|
| Sem horário civil | 2.889.034 B | 1.586.587 B | 1.818.624 B | 0 |
| Horário sincronizado previamente | 2.953.439 B | 1.663.617 B | 1.818.624 B | 0 |

O teste demonstra volume e lógica em POSIX/ASan/UBSan, **não** duas horas em uma placa real nem comportamento físico do FAT/WL durante brownout. ZIPs originais dos Logs Mãe e dump real da flash 0.2.11 não foram fornecidos; não foram reprocessados nem apresentados como recuperados.

A capacidade depende do espaço inicial, do tráfego e das evidências preservadas. O ensaio com horário deixou 685.694 B de arquivos `.BAD` após o ACK: eles continuam ocupando flash e reduzem a próxima autonomia. Backlog legado também ocupa espaço até ACK. Não se promete duas horas adicionais sobre uma flash já cheia ou com muitos arquivos isolados. O painel mostra espaço efetivo e estimativa pela taxa deste boot; revisão/exportação de evidências e reparo da alimentação continuam necessários. Não foi criado apagador automático para ocultar esse limite.

## Distância e combustível

Checkpoint NVS A/B com CRC a cada 10 s, leitura dos blobs v1/v2 existentes e manutenção do blob legado para rollback. Se o firmware anterior avançar o checkpoint após um rollback, a candidata importa os novos totais legados e invalida âncoras antigas, em vez de ressuscitar uma viagem antiga. O checkpoint não comanda criação, encerramento ou mudança de modo das viagens da Cloud; vazio/carregado continuam sob controle existente dos aplicativos/Cloud.

PID 245 entra na distância de viagem somente após marcar sua validação física na configuração e confirmar o MID correspondente. Os campos `j1587_odometer_mid` e `j1587_fuel250_mid` selecionam explicitamente a origem, inicialmente128; outras origens continuam preservadas no cache e nos pacotes, mas não entram no contador validado. Conferir o MID na captura e reiniciar após alterá-lo. Deltas substituem estimativas de velocidade do mesmo intervalo, inclusive após reboot; não são somados duas vezes. Mudança de origem, contador para trás e salto incompatível são registrados/rebaselados. Sem tempo monotônico entre boots, o limite conservador é 2.500 km por delta restaurado; além disso exige investigação. O odômetro absoluto recebido continua preservado.

Combustível integrado por vazão, estimado em lacunas e medido/reconciliado por PID 250 são identificados separadamente em `trip_accounting`. Estimativa não é somada silenciosamente a `trip_fuel_l`. Integração exige mesma fonte e intervalo de até 3 s. Lacunas de até 5 min no mesmo boot admitem estimativa identificada; durante desligamento, somente uma distância validada e um modelo L/km previamente observado permitem estimativa. Sem fundamento, `unknown_gap=true`; não se inventa combustível de marcha lenta ou tempo com motor ligado.

A reconciliação por PID 250 exige marcar a validação desse contador. Usa diferenças entre leituras da mesma ECU, preserva valores acumulados, substitui integração/estimativa coberta pelo contador e recusa resets/saltos. Os totais `integrated_l` e `direct_l` são diagnósticos acumulados que podem cobrir o mesmo intervalo: **não somar** ambos; `trip_fuel_l` é o total operacional observado/reconciliado. Sua qualidade distingue integração, contador e composição. PIDs 235/236/250 nunca são zerados na ECU.

Queda de energia ainda pode eliminar a fração volátil desde o último checkpoint; um contador validado permite reconciliá-la depois. Sem medição ou modelo válido, a lacuna fica explicitamente desconhecida. Não é possível medir combustível com a placa sem alimentação.

## Atualização e rollback

Para esta candidata, usar somente `truckbox_core_0.2.12_ota.bin` na opção **Core**, depois de aprovação. Aguardar a confirmação automática local após 15 s e verificar versão, perfil, armazenamento, fila e ACK. Não enviar bootloader ou tabela de partições por OTA. Nenhum token vai no pacote.

Rollback manual para 0.2.11 requer sincronizar primeiro: o leitor antigo não entende v2 compactado.

1. Estacionar e parar requisições. POST autenticado para `/api/storage/prepare-rollback`, corpo `{"authorized":true,"stationary":true}`. Isso pausa novas gravações até reboot; RX/cache continuam. A pausa é manutenção explícita, não perda oculta de dados.
2. Manter Core/Link/roteador online. Aguardar `/api/state` mostrar `storage.rollback_ready=true`, fila RAM vazia e `pending_records=0` com `pending_known=true`. Esse sinal é emitido somente depois que o worker terminou a amostra em voo e a Cloud confirmou todos os registros válidos. Arquivos `.BAD` permanecem preservados.
3. Fazer OTA **manual** do binário 0.2.11 conhecido e verificar versão/serviços. NVS, identidade e partições não são apagadas. Requisições continuam exigindo autorização conforme o firmware instalado.
4. Sem Cloud não apagar pendências para forçar rollback. Manter a candidata para recuperar/enviar, ou exportar evidências para análise. Reboot cancela a pausa de manutenção. Se ocorrer rollback automático antes da confirmação da candidata, os primeiros registros foram mantidos em v1 para o leitor anterior.

Recuperação USB manual: pacote inclui `flash_core.cmd COM3`, `monitor.cmd COM3`, bootloader, tabela e aplicação. Conferir 8 MB e preservar NVS/armazenamento; nunca usar `erase-flash`. Seguir [UPGRADE_RECOVERY.md](UPGRADE_RECOVERY.md); para esta versão, prevalecem as restrições acima sobre dados compactados.

## Testes e pendências

Execução reproduzível: ativar ESP-IDF 5.5.2, `bash tools/build_v020.sh core`, `sh tests/run_all.sh`; navegador: `node tests/web/test_flow.cjs` com Playwright/Chromium. Fontes e dependências travadas acompanham o ZIP.

Executados os oito scripts pedidos, toda a suíte existente e testes adicionais de codec/fuzz, cortes em 90 posições, arquivo não final/oversized/CRC da 0.2.11, writer em falha ambígua, repetidos reinícios, cadências/wrap/configuração, single-shot/prova externa, bloqueio por timeout/colisão, contabilidade/CRC/migração/rollback e duas horas offline em dois estados de relógio. Evidências finais em `test_evidence/v0212_*`. Relatório de RAM em [MEMORIA_v0.2.12.md](MEMORIA_v0.2.12.md).

Pendentes na placa real: medição de TX/DI/DE/RX/A/B, MID local livre, capturas reais das 17 respostas, alimentação/EN/U8 sob trepidação, brownout durante escrita FAT/WL, duas horas offline em hardware, sincronização pelo Link/Supabase real e stack/heap sob carga. Filesystem fisicamente ilegível continua podendo exigir extração/reparo especializado; o firmware não o formata. SD/FRAM/SIM permanecem sem habilitação automática. Nenhum teste desta entrega certifica eletricamente a placa.
