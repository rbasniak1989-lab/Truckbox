# Roteiro de bancada e critérios de aceitação

Use fonte regulada, amperímetro, osciloscópio e gerador programável de cortes. Use bancada de 5 V de corrente limitada para a primeira energização. O veículo só entra após passar as verificações elétricas. Use um cartão de ensaio com imagem ou backup conhecido para testes deliberados de falha. Registre operador, data, origem da H707, build/commit, carregador, cabos, cartão, módulo UPS, célula, temperatura e montagem.

## Elétrica e mecânica

- Inspecione as duas faces, os jumpers, as ilhas, a lâmina isolante e a orientação de todos os terminais. Confirme resistência e continuidade com H707, J1 e chicote desconectados. Medir entre 3V3/GND, A/B/GND e entre sinais e alimentação. Os divisores de 4,7 kΩ e as capacitâncias geram leituras de resistência dependentes da escala. Consulte os nós do esquema antes de diagnosticar curto.
- Energize apenas a H707 com 5 V, sem célula, sem J1 e sem o chicote. Depois energize a perfurada via VCC3V3. Registre corrente em repouso, pico no 4G, temperatura e tensões em J1. Monte caixa e suportes e movimente manualmente o conjunto e conectores sob captura. Teste um trecho de vibração típico antes da instalação permanente, monitorando contatos. Procure fretting, alteração de folgas, marcas de solda no isolante e interrupções. Use [TESTES_J1708.md](TESTES_J1708.md) para a parte de barramento.
- Confira ausência de curto na USB-C, contatos expostos e nos polos da 18650. Verifique se slots, botões, antenas e conector da célula podem ser atendidos com a montagem presa. Registre fotos e os resultados.

## Comutação e partida

1. Configure a rota nativa e teste primeiro USB com bateria e carga, depois bateria sem USB, então retorno da rede. Numa tentativa com UPS externo, teste a saída em carga eletrônica antes de ligar a H707. Monitore cada caminho da tabela e a forma de onda de corrente com modulação/pico do 4G. Faça tanto com célula carregada quanto com célula perto do menor estado de carga operacional documentado. Não force um curto nos polos para simular baixa tensão.
2. Instrumente VBUS antes da comutação, no USB-C da H707, VCC3V3 de U1/ESP, DVDD4V2 do modem, e tensão nos terminais da bateria. Meça em pontos identificados e use sondas curtas. Configure trigger na queda/retorno de entrada. Registre mínimo, máximo, tempo abaixo do limiar e bordas. Observe o USB Serial/JTAG, mas durante corte físico use também contadores e rede local, pois a porta USB pode desconectar do computador.
3. Use uma carga repetível. Gere tráfego J1708 passivo numerado, lote de upload e escrita em microSD durante o corte. Faça cortes com Wi-Fi funcionando, sem Wi-Fi e com PPP conectado. Enquanto LTE transmite, repita os cortes com GNSS e microSD ativos. Inclua pior caso de pico de corrente e duas quedas consecutivas. Atenção à tensão diferencial e ao GND ao cortar e restaurar.
4. Aplique os valores abaixo como pontos de partida. Meça amplitudes, durações e rampa reais e registre a tensão indicada do gerador, corrente e temperatura. Repita pelo menos dez vezes por combinação representativa, incluindo no mínimo 100 transições totais na configuração escolhida. Após bancada, compare com a forma de onda medida na saída do carregador durante a partida no Volvo e reaplique o pior caso com margem. A entrada de 24 V do caminhão nunca vai para a H707.

| Série | Entrada nominal | Queda de partida proposta | Durações propostas |
|---|---:|---|---|
| P1 | 5,0 V | 4,7 V e 4,3 V | 50 ms, 200 ms, 1 s, 3 s |
| P2 | 5,0 V | 3,5 V e 0 V | 10 ms, 50 ms, 100 ms, 500 ms, 1 s e 3 s |
| P3 | 5,0 V | Interrupção completa e retorno lento | 5 s, 15 s, 60 s e retorno 0–5 V em 100 ms/1 s |
| P4 | 5,0 V | Duas quedas próximas e perfil real da partida | Registrar profundidade, intervalo e rampa |
| P5 | 5,0 V | USB desligado, partida na reserva e retorno de carga | Até o limite operacional contratado para a reserva, respeitando a célula |

Aceite de continuidade **zero reinicializações** em todas as quedas de partida aplicáveis, `boot_id` fixo e uptime crescente, modem retransmitindo sem reiniciar, microSD gravando, nenhuma lacuna de quadro ou de registro esperado e sessão de viagem sem novo início. As tensões de U1/ESP/SD e SIM7670G devem ficar nas faixas das respectivas fichas. Regime de 5 V deve permanecer dentro dos requisitos da LILYGO tanto no fim da queda para a rota com UPS quanto após o retorno. Compare `storage.pending_records`, total confirmado, erros e ACK com os registros produzidos. Se a H707 reiniciar ou algum registro esperado faltar, verifique a rota de alimentação e refaça a matriz. Corte deliberado até esgotar a reserva é um teste separado de recuperação, com reboot esperado.

| Rota | Célula/estado de carga | VBUS antes→mínimo | Duração | VCC3V3 mín. | DVDD4V2 mín. | Vbat mín. | Boot antes/depois | SD/J1708/PPP | Evidência |
|---|---|---|---|---|---|---|---|---|---|
| Nativa | pendente | — | — | — | — | — | — | — | — |
| Externa, se necessária | pendente | — | — | — | — | — | — | — | — |

## Recuperação com interrupção completa

1. Gere amostras e deixe um lote pendente sem rede. Fotografe o estado, grave sequências já confirmadas e faça 20 cortes completos deliberados em momentos diferentes da escrita e do ACK. Reenergize. Verifique que o boot novo incrementa `boot_id`, que a pasta e o cartão são os mesmos e que o estado mostra a recuperação. Compare os registros válidos e isolados antes/depois. Linhas confirmadas por `fsync` e válidas por CRC devem continuar legíveis quando o cartão mantém sua integridade. Cauda incompleta permanece isolada, sem ACEITE prematuro do servidor.
2. Simule falha de montagem e depois recoloque o cartão original. Verifique retentativa automática. Com pendências, experimente cartão de teste diferente e confirme que a fila não muda de mídia. Restaure o original e confira os registros. Mantenha cópia antes de qualquer inspeção FAT em computador. Nenhuma formatação automática é prevista para o cartão.
3. Simule HTTPS indisponível, resposta sem ACK, ACK fora do lote e ACK válidos em ordem. Confirme retenção até o ACK de uma sequência ofertada, reenvio e avanço cumulativo. Os intervalos de IDs reservados podem deixar saltos após reboot. Cruze `sample_kind`, timestamps e contadores de perdas para distinguir saltos previstos de dados faltantes.
4. Verifique restauração do checkpoint de viagem e dos marcos do ECU PID 250 e do odômetro já validados. Compare valores contra o simulador. Uma reinicialização dentro do cenário de partida continua sendo reprovação, ainda que o checkpoint tenha restaurado a viagem.

## Estabilidade e integração

- Mantenha por pelo menos 24 h carga J1708, gravação, AP local, Wi-Fi/Starlink, 4G, GNSS, visualização da multimídia e sincronização. Tenha volumes de teste suficientes para formar backlog, perder a rede e drená-lo. Veja 10 s entre os registros operacionais em regime e 60 s entre `HEALTH`. Registre jitter de filas, overruns e perdas.
- Com Wi-Fi e PPP, confirme saída preferencial pelo Wi-Fi. Desconecte o Wi-Fi e valide ACK via PPP, retorno ao Wi-Fi e serviço local disponível. Teste associação sem internet e anote o comportamento de DNS e rota. Valide fix GNSS, coordenadas, data/UTC, idade e ausência de dados inventados fora de cobertura. Confirme PPP e GNSS simultâneos com a versão do SIM7670G anotada.
- Abra `/api/live`, `/api/state?compact=1`, `/api/diagnostics` e a multimídia. Compare RPM, velocidade, PID 250, falhas PID 194 e dados de viagem com o gerador/dados autorizados. Confirme nuvem de teste com os mesmos campos/IDs e sem reutilizar cadastro de outro dispositivo em paralelo. Faça upload de amostras de erro e sucesso e recupere-as pela aplicação.
- Salve mínimos de heap interno, maior bloco interno, PSRAM, falhas TLS, contagem de drops e filas. Aceite sem tendência de vazamento, WDT, resets, overrun persistente ou falta de memória. O limite de startup é 20.000 bytes de heap interno. Adote como meta de ensaio pelo menos 24 KiB internos livres e maior bloco ≥8 KiB durante o teste combinado, ou investigue a margem restante com as medições.
- Com carga reproduzível e OTA autorizada só na unidade de bancada, tente imagem C6 e observe recusa. Faça OTA de uma segunda imagem Rev.C e verifique versão, partição e rollback. Verifique que o TX fica bloqueado durante a atualização, que o cartão retém o backlog, e teste rollback com imagem de teste que falhe no boot. Anote hash e arquivo de cada versão.

| Ensaio físico | Condições / duração | Resultado | Evidência | Responsável |
|---|---|---|---|---|
| Inspeção e pinagem | — | Pendente | — | — |
| J1708 RX/TX | — | Pendente | — | — |
| Comutação em carga | — | Pendente | — | — |
| Recuperação e checkpoint | — | Pendente | — | — |
| Estabilidade, operação remota e OTA | — | Pendente | — | — |
