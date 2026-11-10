# 0.2.12 — candidata Rev.B.1

- J1939 desligada no perfil J1708_ONLY; J1708 continua independente.
- Primeira requisição supervisionada é única. Viagem requer comprovação externa e autorização explícita, sem autorização automática após reinício.
- Os17 PIDs usam intervalos individuais de1,5 e15 minutos, com uma requisição por vez. Três falhas consecutivas interrompem TX; recepção passiva continua.
- Recuperação preserva dados válidos e arquivos danificados, permitindo voltar a coletar e sincronizar após escrita interrompida. Somente segmentos confirmados e sem corrupção são reaproveitados.
- Compactação local reversível, leitura operacional a cada10 s e saúde a cada60 s. Dois cenários de duas horas offline simuladas passaram, seguidos de sincronização idempotente.
- Contadores de distância e combustível têm checkpoints A/B, origem selecionada e reconciliação quando fisicamente validados. Estimativas e lacunas desconhecidas ficam identificadas.
- Painel mostra armazenamento cheio/recuperando, espaço útil, autonomia estimada, ACK, recepção e bloqueios de requisições.
- Novas configurações usam chave NVS separada, preservando leitura pela0.2.11. Rollback manual exige pausar gravação e confirmar a fila antes de voltar ao leitor antigo.
- Link, credenciais, identidade, partições e histórico da Cloud são mantidos. SD/FRAM/modem não são ativados. Não houve merge nem gravação automática.

Validação elétrica da TX, brownout real, teste de viagem e fluxo Core → Link → Supabase com esta imagem continuam pendentes. Consulte [relatório completo](REV_B1_0.2.12_CANDIDATA.md).
