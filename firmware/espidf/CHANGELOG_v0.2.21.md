# 0.2.21 — candidata com manutenção da flash por Wi-Fi

- Instalar a BIN não inicia limpeza ou formatação automaticamente.
- Backup privado de `storage` pelo painel administrativo.
- Recriação somente de `/tbflash`, mediante confirmação em duas etapas, parada
  medida, aceite da perda, frase e desafio efêmero deste dispositivo/boot.
- Preserva firmware, OTA, identidade, credenciais, NVS e contadores de viagem.
  Apenas a marca de proteção dos arquivos descartados é reiniciada na NVS.
- Verifica escrita, sync e releitura; telemetria só segue após persistência real.
- Preserva a política automática e proteções J1708, com pausa temporária possível
  durante manutenção pela exclusão de OTA. Recepção passiva preservada.
- Sem repetição destrutiva após reboot/resposta perdida; exige nova confirmação.
- Build, suíte host, FatFs real e navegador simulado passaram; teste físico pendente.

Procedimento e limites: [MAINTENANCE_v0.2.21.md](MAINTENANCE_v0.2.21.md).
