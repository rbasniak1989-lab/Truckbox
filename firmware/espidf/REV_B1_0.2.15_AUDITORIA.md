# Core Rev.B.1 0.2.15 — candidata: proteção das requisições J1708

Investigação de 11/11/2026, dispositivo `tb-c6-d40592b9190c`, boot observado 290, Volvo FH 540 2013. Desenvolvimento separado em `fix/revb1-v0215-j1708-guard`. Não houve OTA, reset, alteração de configuração, transmissão real, deploy ou alteração de viagens. Consultas à Cloud foram somente de leitura.

## Resultado comprovado e limites da investigação

O fonte publicado da **0.2.14 foi localizado**, na branch `feature/revb1-v0214-field-validation`, commit de publicação `94cd01a7a64f24e742ce27a03ca3b547b340f129`, fonte `d7957a2b9cc4bc3ba9408579a805a0425d6064ce`. A comparação dos 281 caminhos do ZIP que existem na árvore local, incluindo 119 arquivos C/H/CMake/HTML, não encontrou divergências. SHA256 do ZIP de fontes: `974e60561dc4a4a3a486c7bd4bc8cd46f663927be4def6ab85699e876b49fe89`; aplicação OTA publicada: `03c50991a48854254ef6d31a71fce8ba7895467913ad9bf5dbfae45893f9ca72`. Evidência: `test_evidence/v0215_source_audit.json`.

O histórico identifica firmware 0.2.14/boot 290 e reproduz o evento relatado. **Não é possível comprovar o hash da imagem efetivamente gravada**: a 0.2.14 não registrava essa identificação. A candidata acrescenta versão e SHA256 do ELF ao BOOT_DIAGNOSTIC para futuras comparações; esse SHA não é o SHA do arquivo `.bin` e não prova retroativamente o boot 290.

Há dois problemas comprovados no software: uma única janela ruidosa podia interromper o modo ativo; e o evento não guardava os valores dessa janela, repetindo os números do ensaio estacionado. **Não há evidência suficiente para escolher qual ramo da condição disparou às 20:02:34, nem para atribuir os erros à transmissão elétrica do TruckBox.** Mudança normal de tráfego, agrupamento UART, ruído e interferência continuam hipóteses. Ausência de falha percebida pelo motorista não é prova elétrica.

## Condição exata da 0.2.14

[tbx_j1708_field.c, linhas 105–124 no commit auditado](https://github.com/rbasniak1989-lab/Truckbox/blob/94cd01a7a64f24e742ce27a03ca3b547b340f129/firmware/espidf/components/tb_core/tbx_j1708_field.c#L105):

```c
// A cada pelo menos 10 segundos, desde a última avaliação:
frames = field.frames - field.watch_frames;
errors = field.bad_frames - field.watch_errors;
observed = frames * field.before_duration_us;
expected = field.before_frames * (now - field.watch_at_us);
stop = (errors > 3 && errors * 100 > frames)
    || (field.before_frames && (observed * 4 < expected || observed > expected * 4));
```

O primeiro ramo usa contadores atuais de **todos os quadros inválidos**, não somente checksum. O segundo compara a taxa atual à taxa **histórica do ensaio inicial**, parando fora de 1/4 a 4 vezes essa referência. Não existia persistência de múltiplas janelas nesse teste; os contadores de três falhas de TX/timeout eram uma proteção diferente. Silêncio de 10 s e perda de auditoria também interrompiam, com outros motivos.

`capture.c:169–176` copiava `before_frames`, `after_frames`, erros e durações da sessão antiga para o evento; `capture.c:207–210` os serializava. Logo, **1916/1809 quadros e 0/2 erros no FIELD_FAILED são contexto histórico**, não os contadores da decisão de parada. Ao mesmo tempo, `before_frames/before_duration_us` realmente participavam do segundo ramo acima. Os dois fatos não se contradizem.

`capture.c:490–520` alimentava `bad_frames` tanto com checksum inválido quanto com quadro excessivo/agrupado, tamanho inadequado ou MID inválido. HEALTH separava parte desses casos. Por isso não se pode reconstituir `errors` a partir de `j1708_checksum_errors` por minuto.

## Evidências da Cloud — horário de Brasília, 10/11/2026

| Evento | Horário derivado da Cloud | Seq | Uptime ms |
|---|---|---:|---:|
| FIELD_SATISFACTORY | 19:40:51,573 | 47799 | 275323 |
| FIELD_CONFIRMED | 19:41:01,044 | 47804 | 284521 |
| WORK_ENABLED | 19:43:03,536 | 47856 | 406751 |
| FIELD_FAILED | 20:02:34,006 | 48876 | 1577473 |
| STOPPED | 20:02:33,813 | 48877 | 1577473 |

Os dois últimos registros têm o mesmo uptime. A pequena inversão do horário derivado não muda a ordem de sequência: falha e depois parada. Os eventos anteriores ao STOP repetem os mesmos números do baseline.

O boot contém 318 REQUEST e 318 RESPONSE_VALUE, cobrindo os 17 PIDs; 51 TX_ECHO, 51 RESPONSE_CANDIDATE e 51 RESPONSE_FRAME no ensaio inicial; nenhum evento TIMEOUT/LINK_ERROR encontrado. O código **suprime TX_ECHO e RESPONSE_CANDIDATE no modo de viagem** e grava RESPONSE_FRAME somente no ensaio. Portanto, 51 registros não significam que somente 51 ecos ocorreram.

Nas sete janelas HEALTH de 19:59 a 20:05, há 3598/3617/3602/3608/3590/3565/3629 quadros válidos e 5/1/10/2/6/9/13 erros de checksum; overrun e perda de eventos são zero. Isso é compatível com recepção passiva contínua, mas não revela a distribuição dos erros nos dez segundos da proteção nem os inválidos de enquadramento. Não prova ausência de interferência. Dados restritos aos contadores estão em `test_evidence/v0215_cloud_readonly.json` e `v0215_health_readonly.json`; sem coordenadas, tokens ou conteúdo de viagens.

## O que REQUEST, eco e resposta realmente demonstram

- REQUEST: o backend aceitou/enfileirou o quadro na UART. Não é medição elétrica independente.
- TX_ECHO: bytes recebidos pela UART coincidem integralmente com os cinco bytes esperados, dentro do prazo. É uma observação do receptor local, compartilhando controlador/caminho com TX.
- RESPONSE_VALUE/RESPONSE_CANDIDATE: J1587 válido, MID e PID esperados, pedido pendente, eco concluído e resposta no timeout. Não existe identificador de transação no pareamento. Uma transmissão espontânea com o mesmo MID/PID nesse intervalo pode ser aceita uma vez como candidata.
- Validação supervisionada continua **OPERATIONAL_INDIRECT**. Somente o caminho externo já existente, com confirmação explícita e evidência externa fornecida pelo operador, pode definir `physical_validated=true`. A candidata não preenche esses campos.

O particionamento permanece por idle UART de 10 bits a 9600 baud, com separação especial do eco exato de cinco bytes e limite de 21 bytes. O teste de integração executa o `j1708_task` e o `finish_j1708` reais, além dos decodificadores; não é apenas uma cópia do algoritmo de teste.

## Correção da candidata

Novo módulo pequeno `tbx_j1708_rx_guard.c/.h`; integração localizada em `capture.c`, `tbx_j1708_active.c` e `tbx_j1708_field.c`:

1. Contadores separados de quadros válidos, checksum, enquadramento, overrun, erro UART e falha de leitura. Um erro RX não incrementa automaticamente colisões/falhas TX.
2. Janelas recentes de 10 s e referência de três janelas aceitas contíguas, independentes do ensaio histórico. Um erro isolado pode gerar advertência; não desarma por si só.
3. Checksum/enquadramento são comparados separadamente pela proporção observada. Aumento suspeito requer mais de três erros na janela **e** limite inferior Wilson nominal de 99% atual maior que o limite superior da referência. Três janelas suspeitas consecutivas provocam parada; uma janela limpa zera a sequência. Falhas UART persistentes ou maioria de quadros inválidos também param, mesmo sem referência completa.
4. Taxa de quadros isoladamente não desliga o modo de viagem. Mudanças de motor/carga/tráfego não demonstram problema de TX. Permanecem arbitragem por ociosidade, monitor de bordas, backoff, um pedido pendente, espaçamento de 1,2 s, timeout e três falhas consecutivas.
5. Perder RX durante eco/TX libera e desarma imediatamente, sem inventar colisão; silêncio de 10 s, conflito de MID e relógio monotônico incoerente bloqueiam. Eco divergente libera TX imediatamente e conserva a política existente de repetição/parada. Auditoria perdida continua bloqueando validação/viagem indireta.
6. Janela atrasada de pelo menos 30 s não vira três observações. A referência é reaprendida, sem usar a calibração antiga como substituta. Referência com mais de 60 s não sustenta comparação. Contadores e referência são deste boot, sem restauração de autorização.
7. Falha real fica travada até reconfiguração explícita; configuração por si só não arma TX nem valida hardware. Parada manual/fim da autorização não são rotulados como defeito elétrico; exigem nova autorização. Wi-Fi, retomada de upload e reboot nunca ativam trabalho.
8. Diagnóstico de proteção é copiado por valor antes da mudança do controlador e inclui o estado depois da parada. Gravação/JSON continuam fora do lock de RX/TX. Os campos históricos recebem `baseline_scope=HISTORICAL_VALIDATION` e `baseline_error_metric=ALL_INVALID_FRAMES`.

**Justificativa e limites dos parâmetros:** 10 s preserva o intervalo/silêncio da proteção anterior; três observações reutilizam o princípio existente de três falhas e evitam decidir por um pico; o piso de mais de três erros mantém a tolerância anterior a poucos inválidos. A referência de 30 s acompanha o volume observado (~600 quadros por 10 s) e o período inicial já existente. O limite de idade de 60 s corresponde à aprendizagem mais a confirmação de três janelas. Wilson nominal de 99% explicita a incerteza amostral e substitui a suposição de que uma taxa fixa de 1% serve em toda condição. **99% é uma escolha de engenharia da candidata, não um valor SAE ou limiar calibrado eletricamente nos logs disponíveis.** Erros de rede podem ser correlacionados: não se declara probabilidade formal de falso alarme nem prova causal. O piso de maioria inválida é uma proteção de receptor inutilizável. A aprovação da candidata deve considerar esses parâmetros e o ensaio supervisionado; não existe log de dez segundos que permita calibrá-los retrospectivamente com certeza.

## Diagnóstico compacto `GUARD_DECISION`, versão 1

Mantém `sample_kind=J1708_REQUEST_EVENT`; usa `boot_id`, `uptime_ms` inteiro, `guard_id=boot:microssegundos`, `source=LIVE_PROTECTION_DECISION`, `reason`, `stopped`, `streak`, `fails`, `predicates`, `candidate_only=true`, `crc_hit`, `frame_hit`, `persist_hit` e, se aplicável, `pid/target_mid`. Não declara comprovação externa. Horário UTC continua sendo derivado pelo fluxo existente quando há âncora válida; boot/monotônico permanecem disponíveis sem internet/GPS.

| Campo | Ordem dos elementos |
|---|---|
| `w` | início ms, fim ms, válidos, checksum, enquadramento, overrun, erro UART, falha de leitura |
| `ref` | início ms, fim ms, válidos, checksum, enquadramento |
| `totals` | válidos, checksum, enquadramento, overrun, erro UART, falha de leitura, pedidos, ecos, candidatas, timeouts, divergências/colisões, timeout de eco, falhas backend TX |
| `ci_ppm` | limite inferior atual e superior da referência para checksum; nulo quando não houve comparação válida |
| `policy` | janela ms, persistência, nível nominal %, idade máxima ms, piso de erros exclusivo, timeout resposta ms, espaçamento ms, timeout eco ms |
| `mode` | flags antes e depois: armado=1, viagem=2, TX=4 |

Taxas são reproduzíveis: `válidos*60000/(fim-início)` quadros/min; `checksum/(válidos+checksum)` e `enquadramento/(válidos+enquadramento)`; ignorar intervalo inválido/obsoleto. A comparação de enquadramento é reproduzida pelos contadores e mesma política Wilson; `ci_ppm` registra especificamente checksum.

Máscara `predicates`: checksum elevado=1; enquadramento elevado=2; falha UART=4; maioria inválida=8; persistente=16; silêncio=32; relógio inválido=64; RX perdido em TX=128; três falhas de requisição=256; referência utilizável=512; janela obsoleta=1024. Parada por degradação ocorre quando há modo ativo e bit 16; os bits 32/64/128 identificam paradas imediatas. Os limites TX/MID/autorização continuam nos ramos existentes e são diferenciados por `reason`.

Somente transições de advertência/recuperação e paradas geram esse registro; não há JSON/armazenamento de cada janela nem captura bruta contínua. A API local acrescenta `requests.rx_guard`, com contadores atuais e causa/uptime explicitamente identificados como snapshot da última decisão. O painel traduz os motivos e avisa sobre nova autorização.

## Compatibilidade, teste e aprovação

Não foram alterados GPIOs, topologia de transceiver, MIDs/cadências, bytes PID128, escalas, sensores, política de armazenamento, codec, NVS, credenciais, identidade, partições, protocolo Link/ACK ou histórico Cloud. O problema CAN/J1939 não foi investigado nem modificado. Seu código e testes permanecem. microSD, FRAM e SIM não foram habilitados. O Link instalado não precisa de nova imagem para esta correção do Core.

Resultados e matriz A–J: [TESTES_v0.2.15.md](TESTES_v0.2.15.md). Changelog: [CHANGELOG_v0.2.15.md](CHANGELOG_v0.2.15.md). Diff integral e binários ficam no pacote candidato da branch separada.

Após aprovação da versão, o ensaio físico deve ocorrer estacionado, com alimentação estável, verificação de MID/topologia e autorização explícita pelo painel. Repetir a validação supervisionada ou usar a externa independente, revisar os novos diagnósticos e somente então confirmar o modo de viagem. Não há necessidade de operar controles dirigindo. Nenhum teste deste trabalho concede essa autorização.

Rollback preservado: aplicação OTA anterior **0.2.14/0.2.13/0.2.12**, manualmente e somente após autorização, sem apagar NVS/flash ou gravar tabela de partições. A 0.2.14 também restaura o comportamento antigo da proteção. Para 0.2.11 ou anterior, drenar a fila e seguir o procedimento de compatibilidade de armazenamento da 0.2.12. Não executar scripts USB que gravem partições como se fossem atualização OTA. O bootloader mantém rollback habilitado, que cobre falha de inicialização; isso não substitui reversão manual de regressão operacional.
