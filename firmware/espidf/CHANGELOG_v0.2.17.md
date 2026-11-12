# 0.2.17 — candidata, não instalada

- Preserva a produção0.2.16 e as17consultas. Acrescenta dez PIDs candidatas na mesma fila, sem aumentar o limite global de2inícios/s.
- Suporta solicitação página2/PID439 com seis bytes e eco completo. Mantém os cinco bytes das consultas antigas.
- Completa PID194 com múltiplas falhas, contador opcional inclusive zero, estados ativo/inativo, página2, desconhecidos e multisseção192.
- Mantém histórico local limitado com persistência CRC A/B, sem resolver falha por ausência nem sobrescrever outra falha ao lotar.
- Acrescenta catálogo Volvo inicial versionado em português. Identifica divergências e correspondências candidatas; não transforma SID18 em confirmação de água no diesel.
- Acrescenta capturas250/236/235 no boot, parada, período estacionado e início/fim de viagem, com horários individuais, persistência e retentativas limitadas.
- Distingue referência ECU, captura tardia e integração/estimativa. Preserva dados financeiros e viagens existentes.
- Acrescenta I-Roll provável, percentual VEB e diferença motor/eixo161 com controles de idade e mudança de marcha; não confirma patinação mecânica.
- Preserva todas as observações passivas úteis por resumos limitados, inclusive origem secundária, unidades e indisponíveis. Não habilita dump bruto contínuo.
- Motorista0.6.2: consulta local/remota de falhas, histórico/paginação, filtro por viagem, cache limitado e fila persistente dos eventos de viagem.
- Prepara serviço Cloud de consulta autenticada e somente de leitura. Não realiza deploy nem modifica a ingestão/viagens em produção.
- Mantém Wi-Fi, OTA/rollback, identidade, partições, ACK, perfil J1708_ONLY e módulos J1939 no repositório.

Ver [guia e pendências](REV_B1_0.2.17_CANDIDATA.md) e [testes/memória](TESTES_v0.2.17.md). A versão0.2.16 funcional e os pacotes anteriores permanecem preservados.
