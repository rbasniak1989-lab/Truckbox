# Integração Core / Cloud e Home 13″ — revisão 0.2.0 / 0.2.24

**Atualização autorizada:** para a viagem de teste, o proprietário solicitou acesso sem contas, ambos os módulos liberados para o próprio equipamento e GPS exclusivamente Motorista. Foi implantado o canal temporário descrito em GUIA_TESTE_VIAGEM_020.md: duas tabelas privadas, nova Edge, um vínculo de até 72 h. A migração completa de 12 tabelas e Auth abaixo permanecem etapas futuras. Nenhum firmware foi gravado.

O usuário confirmou a topologia: **multimídia conectada somente ao Wi-Fi do Core**, sensores pelo Core, viagens/GPS pela Cloud, Internet da Starlink compartilhada pelo Core. Também pediu conservar a última leitura real dos instrumentos quando a próxima consulta não trouxer aquele sinal. Essas instruções substituem a proposta anterior de conectar a multimídia diretamente à LAN Starlink e ocultar números após cinco segundos.

## Arquitetura revisável

```text
Starlink 2,4 GHz → STA Core → NAPT/DNS → AP Core → multimídia
                           │                     ├─ HTTP local → sensores
                           │                     └─ HTTPS → Auth / Cloud → viagem e GPS
                           └─ Link/Gateway/Motorista mantêm ingestão e ACK existentes
```

O app não faz ACK, gravação de GPS, alteração de contadores, configuração ou OTA do Core. Queda da Starlink não impede o HTTP local; valores antigos no painel têm idade/qualidade visíveis e não autorizam cálculos. Financeiro/Jornada são independentes, somente Cloud e removidos no erro/expiração, sem cache protegido persistente.

## Evidências e incrementos

1. **Core 0.2.23 localizado:** branch `fix/revb1-v0223-polling-fuel`, `bd723891cdaad2b23466c8311d13409b3d4a36c0`. A main auditada inicialmente não contém firmware. Essa fonte tem `/api/health` e `/api/sensors` schema 2, páginas de oito, boot/captura/idade/origem/qualidade. O usuário confirmou leitura de sensores na multimídia; isso não comprova todos os 13 nem validação física pelo desenvolvimento.
2. **Retenção por sinal:** última observação utilizável do mesmo Core fica em memória, sem inventar zero nem atualizar sua captura. NA/ausência/erro de comunicação conservam o número como antigo. Troca de host/Core/veículo limpa contexto. Dois circuitos de ar não se completam entre MIDs/boots diferentes. Velocidade/vazão de cálculo não usam esse cache. `last_valid_value` do firmware não é promovido a leitura atual.
3. **Home nativa 13″:** 13 cartões, 3+4+6, proporção 2:1, rodapé Viagem Atual, Inter/Oxanium locais, imersivo reaplicado; capturas em cinco resoluções. Fonte/cores/layout não são WebView.
4. **Firmware separado:** [PR 27](https://github.com/rbasniak1989-lab/Truckbox/pull/27), candidata **0.2.24**, implementa NAPT IPv4 limitado a 64 conexões, DNS DHCP, recuperação e proteção de RAM/sub-rede. A 0.2.23 possui STA, porém **não compartilha Internet**. Candidata sem flash/OTA; captura, polling, pinos, NVS, fila/ACK e partições preservados. Roteiro físico e rollback em `firmware/espidf/STARLINK_MULTIMIDIA_v0.2.24.md` daquela branch.
5. **Canal Cloud proposto:** `backend-proposal/v1/cloud` e pasta Supabase local. Auth verificado, sessão ativa no banco, prova RSA do Android Keystore, nonce de uso único, aprovação remota usuário/aparelho/veículo, política versionada, ACK idempotente e rechecagem antes de serializar. Migração privada aditiva gerada pelo CLI oficial; sem deploy nem DDL de produção.
6. **Android Cloud:** DTOs tipados, HTTPS independente do Core, login em Configurações, refresh cifrado no Keystore/AES-GCM, polling de 10 s, lease máxima de 20 s reduzida pelo tempo da requisição, descarte por escopo/versão/epoch. Só chave pública de projeto pode entrar no build. CI sem configuração fica explícita; APK configurado não presume a existência da nova API.
7. **Viagem/GPS:** metadados de viagem e última posição de `gps_latest` somente do veículo autorizado, sem selecionar frete/notas/horas por tabela inteira. GPS mantém captura/origem/precisão recebidas, sem extrapolação; o registro encontrado em 14/11 era de **30/10**, não GPS atual. Mapa/rota continuam dependentes de fornecedor/licença. Contadores, finanças e jornada só entram por read models verificados; começam vazios.
8. **Entrega e revisão:** build APK, testes Android/JVM/Compose, API/SQL/RSA isolados, lint, scanner e evidências; imagem firmware separada com regressões. Plano concreto de implantação/rollback em `REVISAO_CORE_CLOUD_020.md` antes de solicitar autorização hospedada.

## Bloqueios reais

Há somente um projeto Supabase acessível e nenhuma branch de desenvolvimento. A conta do aplicativo correspondente ao e-mail informado não foi encontrada em Auth; nenhum usuário/senha/convite foi criado. Provisionamento seguro e aprovação de vínculo são necessários. Os fallbacks públicos legados da auditoria continuam pendência de segurança e não são usados pelo canal novo.

Sem hardware disponível ao desenvolvimento: NAT/DNS/HTTPS na Starlink, Keystore físico, treze instrumentos/cadências, Link/Motorista/Cloud, barras da central de 13″ e desempenho precisam de ensaio real. IA, DTC/episódios, pedágios, ledger persistido e jornada legal não são simulados. App, firmware e canal Cloud possuem entregas e aceites independentes; compilar não significa homologar em campo.
