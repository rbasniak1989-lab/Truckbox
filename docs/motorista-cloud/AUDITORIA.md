# Auditoria do Motorista Cloud / GPS

Data: 15/11/2026. Base: `origin/main`, commit
`06a9309d104367a49c88167d8f2b70bd7c1ffd1e`, Motorista v0.6.4.
Escopo autorizado: atualizar o Motorista para falar somente com a Cloud e
enviar o GPS real do celular. Firmware, multimídias, Recovery e schema de
produção permanecem fora desta alteração.

## Evidências anteriores à alteração

| Achado | Evidência na base | Consequência |
|---|---|---|
| GPS dependia da presença do Core | `app/src/main/java/br/com/truckbox/app/gateway/TruckBoxGatewayService.kt:214`: `if (coreOnline && valid && fix != null ...)` | Sem Core acessível, a posição do celular não era enviada. |
| Motorista fazia gateway de telemetria e enviava GPS ao Core | Mesmo serviço, `core.pushGps(location)` e laço de coleta/ACK | Acoplamento desnecessário para o fluxo solicitado. |
| Sem leitura virava referência de combustível zero | `MainActivity.kt:188`, `:202`, `:207`: `gateway.coreFuelLiters ?: 0.0` | Risco de cálculo a partir de referência inexistente. |
| Operações eram reenviadas sem fila de ações imutáveis | Serviço antigo, `client.syncTrip(trip)`; UI também enviava diretamente | Início/encerramento offline podia ser perdido ou mudar a ordem de ações. |
| Tokens eram strings em SharedPreferences | `preferences/TruckBoxPreferences.kt` da base | Precisam de proteção e não devem estar no APK/repositório. |
| Assinatura do APK anterior disponível difere da chave local | SHA-256 do certificado anterior começa `d8f22560`; chave desta entrega começa `13e9becd` | Não orientar desinstalação do Motorista anterior. APK de teste instala ao lado. |

## Cloud inspecionada em modo de leitura

Projeto Supabase utilizado pelos aplicativos existentes. Foram consultados os
fontes publicados de `truckbox-app` v20 e `truckbox-gps` v4 e as colunas/índices
de `devices`, `gps_samples` e `gps_latest`. Nenhuma resposta de GPS artificial
foi enviada à produção para teste.

- `truckbox-app`: POST HTTPS com `device_uid`, `token`, `action` e `payload`.
  `snapshot` retorna viagens, abastecimentos, despesas, manutenção e estado
  operacional, todos filtrados pelo caminhão da credencial. Não oferece
  leitura instantânea de sensores para o Motorista.
- `upsert_trip` e `end_trip` aceitam referências de odômetro/combustível nulas.
  Retornam `item.client_uid` e `item.truck_id`. O cliente só confirma a ação
  quando ambos coincidem com a solicitação/configuração.
- `truckbox-gps`: POST individual, origem `ANDROID_PHONE`, `client_uid` estável,
  horário da captura, coordenadas e campos opcionais. Retorna `accepted_ts` e
  `truck_id`. Deduplicação existente em `(truck_id, client_uid)`.
- Essa função usa `Number(v)` para campos opcionais: JSON `null` viraria zero.
  O novo cliente omite velocidade/altitude/direção/precisão quando o Android
  não informou o valor; assim a função preserva SQL NULL sem alteração remota.
- `gps_latest` só recebe uma posição de horário igual ou posterior na lógica
  atual da função. O reenvio do histórico pelo novo aplicativo é serial.
- `devices`: autenticação por hash SHA-256 do token e `active`, com vínculo ao
  caminhão. O schema atual não tem validade temporal para esse token.

## Implementação resultante e limites

- Clientes locais e telas antigas sem uso movidos intactos para
  `app/legacy-core`, fora do APK. O serviço mantém o nome do componente antigo
  para compatibilidade, mas executa apenas GPS e sincronização operacional.
- Destinos de rede fixos HTTPS; sem HTTP local, socket TCP, ACK ao Core,
  seleção obrigatória de Wi-Fi ou redirecionamento de credenciais.
- GPS real validado por tempo monotônico, coordenadas finitas e origem do
  Android. Posições de localização simulada são recusadas. Precisão aproximada
  permanece identificada pelo valor de precisão real informado.
- SQLite privado guarda pontos selecionados a cada 10 segundos, até 100 mil.
  Ao atingir o limite, preserva o histórico e informa que a fila está cheia.
  Só remove após confirmação do mesmo caminhão e horário. Recomeçar o processo
  preserva a fila; pausar preserva a fila e interrompe captura/reenvio.
- Ações operacionais ficam imutáveis, persistidas e ordenadas: início, fim,
  próximo início. Alterações locais pendentes resistem a snapshots atrasados.
- Novos códigos usam um único token de equipamento, protegido pelo Android
  Keystore/AES-GCM. Tokens antigos, quando existentes, migram para o armazenamento
  protegido. Chave privada, código e token não entram no Git ou APK.
- A troca de caminhão por importação de outro código é recusada neste teste,
  evitando misturar dados locais antes de existir um fluxo de múltiplas contas.

## Pendências reais

1. Testar GPS e serviço em segundo plano no celular físico, inclusive economia
   de bateria, tela apagada, rede móvel/Starlink, falta de internet e reabertura.
   Não há aparelho/emulador conectado nesta execução; compilar o APK de testes
   instrumentados não equivale a executá-lo.
2. Validar a chegada de `ANDROID_PHONE` na Cloud e a leitura na multimídia com
   uma captura real. A resposta operacional não comprova envio de GPS.
3. O Core envia seus próprios sensores à Cloud no fluxo existente. Remover o
   gateway do celular não comprova funcionamento físico desse envio ou do NAT
   Starlink. Nenhum firmware foi gravado nesta tarefa.
4. Contrato legado captura contadores ao processar início/fim na Cloud. Não
   garante reconstrução histórica de consumo quando essas ações chegam com
   atraso offline. Referências ausentes continuam nulas no cliente; não se
   declara validado consumo ou saldo de tanque. Correção exige contrato
   versionado com contadores históricos e qualidade/origem por horário.
5. Funções legadas contêm atalhos de autenticação para equipamentos antigos e
   retornam erros detalhados em alguns caminhos. Não foram copiados para o app
   ou alterados silenciosamente. Usar UID novo evita esses atalhos e permite
   revogar o novo acesso pelo cadastro normal. Migração para autenticação por
   usuário, expiração, funções por dispositivo e autorização granular segue
   pendente para uso comercial/múltiplas contas.
6. A credencial atual das APIs de Motorista dá acesso operacional ao caminhão
   vinculado. Código é privado e revogável, sem expiração automática no schema
   atual. Não reutilizar o código da multimídia nem distribuir este código a
   outros motoristas/equipamentos.
7. Concorrência entre vários emissores GPS e avanço de `gps_latest` precisam de
   operação atômica no servidor antes de uma implantação com múltiplos aparelhos.
8. Roteamento, mapas, altimetria, tarifas e previsões mantêm suas dependências
   anteriores. Não foram homologados nem receberam dados simulados nesta tarefa.
   Base legal, retenção e acesso a localização de terceiros exigem definição
   antes do uso comercial.

Esta entrega é candidata a teste, sem declaração de produto pronto ou
homologação física.

## Integração remota realizada após o build

Foi criado um único cadastro `devices` do tipo `DRIVER` para o celular do teste
do proprietário, vinculado ao caminhão já utilizado. Apenas hash do token no
banco; código privado entregue fora do repositório/APK. Não foi criada conta
Auth nem outra credencial de QA. A revogação é `active=false` nesse cadastro.
Não houve mudança de schema, Edge Function ou registros de viagens.

Consulta `snapshot` real com esse acesso retornou HTTP 200 e `ok=true`; todos
os registros retornados pertenciam ao caminhão configurado. A verificação
posterior encontrou zero amostras GPS do novo equipamento: o teste físico
depende da primeira captura real no celular. Esse resultado não equivale à
homologação de GPS, sensores, consumo ou visualização na multimídia.
