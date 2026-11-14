# Core 0.2.24 — candidata para compartilhamento da Starlink

Base: `bd723891cdaad2b23466c8311d13409b3d4a36c0`, branch `fix/revb1-v0223-polling-fuel` (0.2.23). Esta candidata não foi instalada no Core do usuário.

## Fluxo solicitado

```text
Starlink (Wi-Fi 2,4 GHz com Internet)
             │ STA do Core, DHCP
             ▼
Core — AP 192.168.4.1/24, WPA2 existente
             │ multimídia conectada somente ao AP do Core
             ├── HTTP local: /api/health e /api/sensors → instrumentos
             └── HTTPS via NAT: Cloud autenticada → viagem, GPS e dados autorizados
```

Sensor local não depende de Internet. Finanças/Jornada continuam independentes, autorizadas exclusivamente pela Cloud. NAT não cria usuário, vínculo, permissão nem dados na Cloud.

## Mudança isolada e limites

- ESP-IDF **5.5.2**, ESP32-C6, perfil J1708_ONLY, partições originais, AP e reserva `.2` para Link preservados. Nenhuma alteração em captura CAN/J1708, polling, pinos, NVS, contadores, fila, ACK, SD ou Motorista.
- Encaminhamento IPv4/NAPT apenas no **Core**. Tabela limitada a **64 conexões**, sem port forwarding, sem novo serviço de administração. Core STA usa SSID/senha já configurados no painel existente; nenhuma credencial entra no Git.
- Ativa após endereço/gateway STA válidos e AP disponível. Bloqueia redes sobrepostas, inclusive uma /25 dentro da /24 do AP. Desativa em queda de STA/perda de IP/AP parado; reconexão reaplica DNS e NAT. Mudança de endereço STA limpa as traduções anteriores.
- DHCP entrega DNS real da rede externa. Antes de STA conectar, anuncia `1.1.1.1` como fallback; lease do AP de 1 minuto permite renovar DNS sem reiniciar Wi-Fi. Não há proxy DNS nem interceptação de TLS.
- Reserva de RAM: não tenta alocar NAPT abaixo de 32 KiB livres ou bloco livre de 8 KiB. `INSUFFICIENT_HEAP` exige recuperar memória/reconectar para nova tentativa; captura tem prioridade. Capacidade e limiares precisam medição física.
- `/api/health.network.internet_sharing` informa configuração efetiva, DNS e motivo. **`napt_active=true` não comprova acesso à Internet**; `internet_verified` permanece falso. Cloud só apresenta estado autenticado após resposta real.
- O Link mantém a seleção anterior de uplink Wi-Fi/PPP. Esta candidata não altera nem instala firmware do Link.
- Wi-Fi de rádio único em AP+STA compartilha canal e capacidade. Starlink precisa oferecer 2,4 GHz; rede guest/isolamento, portal, conflito com `192.168.4.0/24` e DNS bloqueado podem impedir o fluxo.

Referências oficiais: [softap_sta em v5.5.2](https://github.com/espressif/esp-idf/blob/v5.5.2/examples/wifi/softap_sta/main/softap_sta.c), [esp_netif NAPT](https://github.com/espressif/esp-idf/blob/v5.5.2/components/esp_netif/include/esp_netif.h), [lwIP fixado pelo IDF](https://github.com/espressif/esp-lwip/tree/fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e).

## Verificação e instalação sob revisão

Workflow `core-v0224-wifi-sharing.yml`: compila a imagem real, confere identidade/tamanho OTA/perfil/partições/NAPT64, executa as regressões host existentes e novos testes dos handlers de rede. Testes host substituem interfaces ESP, não comprovam encaminhamento físico.

Antes de instalar, revisar diff e resultado do workflow, confirmar versão/perfil/placa realmente instalados e manter o binário 0.2.23 conhecido e o backup atual. Usar somente o fluxo OTA existente revisado, sem apagar NVS, partições ou armazenamento. Nenhum comando desta entrega faz flash/OTA. Aprovação para implementar código não significa que a candidata já esteja homologada ou instalada.

Roteiro físico pendente:

1. Configurar STA do Core na Starlink 2,4 GHz pelo painel existente; conectar a multimídia **somente** ao AP do Core. Renovar Wi-Fi/DHCP após a atualização, se necessário.
2. Confirmar IP/gateway/DNS recebidos na multimídia e `/api/health` local. Verificar `internet_sharing.status`, sem interpretar NAPT como Internet validada.
3. Abrir HTTPS público na multimídia; em seguida autenticar no canal Cloud homologado, com usuário/veículo/dispositivo autorizados. Conferir origem e idade do GPS; mapa depende de fornecedor/licença.
4. Conferir os sensores realmente disponíveis, cadência, memória mínima, watchdog, latência, perda de pacotes, fila e ACK durante carga de rede. Não inferir os 13 disponíveis a partir da resposta HTTP.
5. Desligar Starlink: sensores locais permanecem, instrumentos guardam último valor com idade, conteúdo Cloud protegido é removido no erro/expiração. Reconectar Starlink e verificar recuperação de DNS/HTTPS/Cloud sem mudar a rede da multimídia.
6. Exercitar reboot, renovação DHCP e sub-rede conflitante. Conferir Link/Gateway e contadores antes/depois. Confirmar ausência de regressão de coleta e armazenamento.
7. Se houver regressão, voltar à imagem Core 0.2.23 conhecida via mecanismo OTA/rollback já existente. Confirmar versão e coleta após o retorno. Não apagar dados para tentar recuperar rede.

Não declarar homologação física até registrar esse roteiro com Core, multimídia de 13”, Motorista e Cloud reais.
