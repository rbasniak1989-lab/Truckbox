# Integração Core / Cloud e Home 13″ — revisão de 14/11/2026

Solicitação adicional: sensores reais do Core, viagens e dados operacionais autenticados da Cloud, rede Starlink, Home fiel ao ZIP e tipografia. A política Financeiro/Jornada continua exclusivamente remota. Sem gravação de firmware, migração ou deploy em produção neste trabalho local.

## Evidência nova

- O usuário confirmou que `http://192.168.4.1/api/state` abre no aparelho e forneceu foto da resposta. Nenhum UID, SSID, conteúdo pessoal ou credencial dessa foto será versionado.
- Os fontes do Core **0.2.23** foram localizados na branch `fix/revb1-v0223-polling-fuel`, commit `bd723891cdaad2b23466c8311d13409b3d4a36c0`. O inventário anterior se referia à `main`, que não contém firmware. Esta descoberta amplia a auditoria, sem alterar sua base histórica.
- Essa versão já oferece **GET `/api/sensors?offset=0&limit=8`**, schema 2, com origem J1939/J1587, SID/PID, SA/MID, PGN, unidade, qualidade, validação, boot, captura, uptime e idade. `/api/state` é diagnóstico compacto e não deve ser usado como mapa recursivo de sensores.
- A documentação de rede da 0.2.23 prevê Core/Link na mesma LAN Wi-Fi externa de **2,4 GHz**, incluindo Starlink. Os APs `192.168.4.1`/`192.168.5.1` são de configuração e **não fornecem internet por NAT**. Não é necessário substituir o firmware para inventar esse suporte.
- Há somente um projeto Supabase acessível e nenhuma branch de desenvolvimento. As rotas seguras de multimídia ainda não estão implantadas. Mudanças hospedadas precisam de revisão concreta antes da execução.

## Topologia

Core e multimídia na mesma LAN Starlink, com comunicação entre clientes habilitada. A multimídia faz HTTP local apenas ao Core e HTTPS autenticado à Cloud. O Link/Motorista continua responsável pela ingestão e pelos ACKs; o novo app não assume a fila. Com o roteador indisponível, a coleta e a fila do Core preservam seu comportamento existente. O app pode consultar o AP do Core ao trocar de rede, mas isso por si só não fornece acesso à Cloud.

## Incrementos testáveis

1. Corrigir seleção do Wi-Fi sem exigir internet, fallback mDNS/IP e configuração de IP privado na LAN Starlink; explicar falhas/contrato na tela. Não usar binding global do processo.
2. Implementar adaptador **do endpoint real de sensores schema 2**, paginação limitada e mapeamento explícito por protocolo/ID/unidade. Preservar qualidade, idade e origem; ignorar `last_valid_value` como leitura atual. Resultados em validação devem ser identificados, nunca promovidos a validados.
3. Home: corpo em duas colunas 2:1 em paisagem, 13 instrumentos em 3+4+6, rodapé fixo de Viagem Atual. Inter para textos e Oxanium 800 inclinado para números; fontes offline. Reaplicar imersivo ao retomar/focar a janela. Capturas nativas para revisão em 1920×1080 e 1280×800 e regressão nas outras resoluções do ZIP.
4. Concretizar backend versionado: autenticação de usuário e prova do dispositivo, vínculo aprovado na Cloud, políticas revogáveis, projeções explícitas de viagem/consumo e recursos protegidos separados. Migrações aditivas e rollback operacional, testes com Postgres isolado, nenhum dado real nos fixtures.
5. Integrar cliente Cloud Android tipado, sessão protegida, login/vínculo na área de Configurações, polling independente do Core e descarte de respostas tardias/expiradas. Permissões somente leitura. Não embutir credencial de ingestão/service_role nem usar os fallbacks públicos legados.
6. Compilar APK de homologação com chave debug preservada; scanner, unitários, testes nativos, API/SQL e evidências. Submeter alteração hospedada e plano de rollback à revisão antes de ativar Cloud real.

## Limites de aceite

A conectividade e os sensores do caminhão, o estado de validação J1587, a disponibilidade da Starlink, o firmware efetivamente gravado e a integração com Motorista/Link exigem teste no equipamento. IA, DTC/episódios, mapas/tarifas e jornada legal não serão simulados. A versão do app e a versão do firmware têm numeração independente. Não produzir OTA nova se a integração puder usar os endpoints já existentes da 0.2.23.
