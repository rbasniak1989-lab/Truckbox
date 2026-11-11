# Core Rev.B.1 0.2.16 — polling J1708 permanente

Produção no ESP32-C6, derivada do fonte real 0.2.15, commit `4a6f2b72a0cf44b0a810483ea8c62ebe1b30a321`. Branch separada `feature/revb1-v0216-auto-j1708`. Código implementado e compilado; não instalado. Não houve OTA, reset, configuração/transmissão real, deploy, alteração de viagens ou merge.

## Funcionamento após ligar

A 0.2.15 mantinha o modo de viagem em RAM, com autorização de 12 horas, e iniciava desarmada. A 0.2.16 recupera a política da NVS e inicia o polling após UART, monitor de bordas, MID e três janelas recentes saudáveis. Normalmente são **30–40 segundos de rede saudável**, independentemente de GPS, Wi-Fi, internet, Android ou página aberta. `automatic.enabled` indica a configuração; `work_mode` indica operação efetiva, podendo ficar falso enquanto aguarda a rede.

Não há renovação pelo aplicativo nem expiração de 12 horas na produção. Ensaios antigos continuam separados e bloqueados enquanto o automático está habilitado. Não é exigida outra campanha NEXIQ.

## NVS e migração

Nova chave **`tb_cfg/v216`**, com `j1708_auto_enabled`, `j1708_local_mid`, `j1708_topology_attested`, `j1708_targets`, `j1708_auto_intervals_ms` e `j1708_launch_intervals_ms`.

- Padrão do Core: automático habilitado. Para **`tb-c6-d40592b9190c`**, migra o perfil utilizado: MID local **172**, prioridade de arbitragem 8 e alvos NEXIQ existentes. Consulta somente de leitura confirmou MID 172 em REQUEST do boot 294 em 11/11/2026. Continua evidência operacional indireta, sem comprovação elétrica externa inventada.
- Outros dispositivos não recebem MID local/topologia presumidos: aguardam perfil configurado. O firmware não escolhe outro MID por sorteio ou ausência momentânea de tráfego.
- A chave ausente é criada uma vez. Reboot/OTA seguintes carregam a configuração sem nova escrita. Opt-out salvo permanece desativado. Credenciais, identidade, viagem e contadores são preservados.
- Chave existente inválida não é substituída silenciosamente por padrão habilitado. Se somente a gravação da nova migração falhar, o automático é bloqueado e a instalação passiva/Wi-Fi anterior continua quando sua configuração base é válida.
- Não se escreve NVS por pedido/ciclo. Escritas ocorrem na migração, alteração administrativa explícita ou desativação por conflito de MID.
- `config` e `v212` continuam compatíveis com os parsers antigos; as novas frequências ficam em `v216`, sem reduzir o mínimo aceito pela 0.2.15 naquele formato.

Os testes executam o código real `config.c`, com apenas a fronteira física NVS simulada: migração única, reboot sem escrita, credenciais, opt-out, intervalos personalizados, corrupção e falha de gravação. Não foi gravada a NVS da placa nesta tarefa.

## Frequências

| PID | MID alvo | Hipótese inicial | Normal | Arrancada |
|---:|---:|---|---:|---:|
| 31 | 130 | Posição/range | 10 s | 2 s |
| 33 | 130 | Cilindro da embreagem | 5 s | 2 s |
| 34 | 130 | Atuador da embreagem | 15 s | 5 s |
| 63 | 130 | Atuadores da transmissão | 15 s | 5 s |
| 32 | 130 | Divisor | 30 s | 10 s |
| 160 | 130 | Rotação de eixo da caixa, função a validar | 30 s | 10 s |
| 37 | 130 | Ar da transmissão | 30 s | 15 s |
| 26 | 128 | Ventoinha | 60 s | 60 s |
| 62 | 144 | Inibição do retardador | 60 s | 60 s |
| 68 | 128 | Limitação de torque | 60 s | 60 s |
| 108 | 128 | Pressão atmosférica | 60 s | 60 s |
| 117 | 140 | Ar primário | 60 s | 60 s |
| 118 | 140 | Ar secundário | 60 s | 60 s |
| 158 | 128 | Tensão | 60 s | 60 s |
| 250 | 128 | Combustível total ECU | 5 min | 5 min |
| 235 | 128 | Horas de marcha lenta | 15 min | 15 min |
| 236 | 128 | Combustível de marcha lenta | 15 min | 15 min |

Uma entrada por PID, sem duplicação. Tabelas configuráveis por objetos administrativos `PID:milissegundos`; não permitem elevar o limite global. Significados proprietários e escalas pendentes continuam **EM VALIDAÇÃO**; nenhum decoder foi recalibrado. CSV em `sensors/J1708_PRODUCAO_v0.2.16.csv`.

## Fila, arbitragem e recuperação

Uma fila lógica de 17 entradas usa prazos/prioridade e um pedido em andamento. Durante arrancada, 31/33 precedem os demais sinais da transmissão e os secundários. Atrasos prolongados recebem prioridade para evitar inanição. Frequências são metas: ocupação, timeout e proteção podem adiá-las.

Espaçamento global **mínimo de 500 ms**, máximo de dois inícios/s. Advertência RX e os primeiros 30 s de retomada após falha usam pelo menos 1 s. O mesmo driver mantém guarda de ociosidade de pelo menos 5 ms, prioridade J1708, bordas RX, FIFO, fila UART e liberação imediata do DE21. Só envia pedidos PID128 para os 17 parâmetros; nunca comandos de controle/configuração/atuação.

Timeout inicial 450 ms, configurável. Um timeout permite uma retentativa programada dentro da fila/limite; não há repetição apertada. Resposta exige checksum/layout válidos, MID/PID esperados e janela temporal. Eco local e mensagem espontânea coincidente não viram prova causal nem elétrica externa.

Proteções da 0.2.15 preservadas: três falhas consecutivas, eco divergente, silêncio, relógio inválido, RX perdido durante TX, erros persistentes e conflito de MID. Perder RX em TX libera o transmissor imediatamente. Recepção passiva continua.

Paradas recuperáveis mantêm a política habilitada e aguardam **60, 120, 240 e até 480 s**, mais três janelas saudáveis novas posteriores à pausa. Retomar Wi-Fi não basta. Contadores são preservados; cada nova tentativa enfrenta os mesmos intertravamentos. Após operação estável, reduz o nível de espera.

Conflito de MID desabilita persistentemente e não escolhe outro identificador. Perfil ausente, NVS inválida ou relógio incoerente bloqueiam; esses casos podem exigir ajuste administrativo. O botão **Parar** interrompe TX e salva a desativação. A manutenção para rollback apenas pausa em RAM; OTA normal não apaga a política.

## TRANSMISSION_LAUNCH_MONITOR e diagnóstico

Usa velocidade/RPM recentes, pedal e saída de parada/manobra. Heurística inicial: parado até 1 km/h; intenção com pedal >=5%, RPM >=600 e velocidade <=3 km/h; ou saída recente de parada abaixo de 10 km/h. Sai a 15 km/h, por dados desatualizados ou após no máximo 30 s. Pedal mantido não renova indefinidamente a janela. São critérios de agendamento, não parâmetros mecânicos certificados.

Snapshots reúnem sete PIDs prioritários, RPM, velocidade, marchas, pedal/carga J1587 e torque/rotações/marchas/embreagem J1939 quando presentes. Mantêm as duas redes, MID/endereço, bytes, valor/nulo, unidade, idade e qualidade. Não geram dados CAN ausentes.

Flags candidatos: demora de acoplamento=1; oscilação de atuador=2; padrão de trepidação RPM/velocidade=4; possível desacoplamento RPM/velocidade=8. Exigem combinações de sinais recentes; indisponibilidade não vira zero ou leitura fresca. **`slip_confirmed=false`**.

Limitações: não há acelerômetro; polling de 2 s não mede a frequência física da vibração. PID160 tem semântica de eixo provisória: não se assume relação 1:1 com o motor nem se declara sua diferença direta anormal sem relação validada. Ambos são registrados. Candidatos não são confirmação de patinação real e não acionam módulos do veículo.

## Cloud, flash e painel

HEALTH mantém `j1708_work_mode`, `j1708_request_sent`, `j1708_request_answered`, `j1708_request_timeouts`, `j1708_checksum_errors`, `j1708_frames` e `request_event_dropped`. Adiciona a indicação automática. API local inclui `requests.automatic`: habilitação/NVS, estado, motivo, retomada, inícios/pausas e monitor de arrancada. Estatísticas paginadas acrescentam latência por PID. Último pedido se refere a bytes aceitos na UART, separadamente de eco/resposta.

Produção agrega sucessos para não gravar dois JSONs por pedido frequente. Contadores incluem **todos** os pedidos; valores continuam no cache e registros operacionais/sensores. Erros/proteções continuam gravados. Eventos `AUTOMATIC_STATUS` por minuto e `TRANSMISSION_SNAPSHOT` normalmente por minuto, a cada 10 s em arrancada e nas mudanças de evento usam o tipo já aceito `J1708_REQUEST_EVENT`. Não há RAW contínuo. Um candidato latente não gera gravações aceleradas indefinidamente.

Partes de snapshot compartilham `boot_id` e `snapshot_ms`. Cada `cells` tem tuplas:

`[rede, PID_ou_ID_catalogo_J1939, MID_ou_SA, valor_ou_null, unidade, bytes_hex, idade_ms, qualidade]`.

A referência de análise é sincronizada; não afirma aquisição simultânea de todos os sensores. O ID J1939 é do catálogo existente, não PGN/SPN. `launch_id` distingue episódios no boot.

O painel mostra o automático no fluxo normal e recolhe ensaios avançados. Abrir/fechar a página não arma nem mantém polling. Reabilitação administrativa só é necessária depois de desativação deliberada, não a cada partida.

## Preservação, atualização e rollback

Mantidos: credenciais/NVS base, identidade, Wi-Fi, Link, gateway Android, viagem, CRC/seq/ACK, sincronização offline, partições, rollback e decodificadores. microSD/FRAM/SIM não habilitados. A imagem mantém **J1708_ONLY** da base; suporte/código J1939 permanece, mas esta tarefa não resolve a ausência física de quadros CAN nem habilita a interface silenciosamente.

Resultados em [TESTES_v0.2.16.md](TESTES_v0.2.16.md), `test_evidence/v0216_*`; mudanças em [CHANGELOG_v0.2.16.md](CHANGELOG_v0.2.16.md).

Atualização manual, caminhão estacionado e alimentação estável: somente `truckbox_core_0.2.16_ota.bin` de aplicação no Core. Não apagar NVS/flash nem gravar partições. Após reinício, conferir versão, `automatic.enabled/persisted/state/reason`, quadros e contadores. Nenhuma ação durante a viagem.

Rollback manual para 0.2.15/14/13/12 mantém dados e ignora `v216`; polling volta à política manual antiga. Retorno à 0.2.16 recarrega a configuração. Para 0.2.11 ou anterior, seguir a drenagem/compatibilidade já documentada. Bootloader continua com rollback habilitado; não cobre automaticamente toda regressão operacional após boot.
