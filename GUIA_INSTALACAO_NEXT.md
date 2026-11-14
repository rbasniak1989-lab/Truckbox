# Instalação — TruckBox Next de homologação

Para a viagem de teste sem cadastro de contas, começar por **GUIA_TESTE_VIAGEM_020.md**: a API temporária já está publicada, o vínculo proprietário autoriza Financeiro/Jornada e a ativação usa código privado. O procedimento Supabase Auth abaixo é o caminho completo futuro, ainda sem provisionamento. O APK usa URL/chave **publicável** em `multimediaNext/cloud-public.properties`; isso não concede autorização sem credencial/prova do aparelho.

Este incremento é uma base Android nativa para revisão. Não é liberação comercial nem validação de Core/ECU/Cloud em campo.

## APKs e coexistência

- Homologação: `br.com.truckbox.multimedia.next`, versão `0.2.0-homologacao`.
- Demonstração: `br.com.truckbox.multimedia.next.demo`, banner permanente de dados fictícios, sem rede.
- Multimídia antiga: `br.com.truckbox.multimedia`; Motorista: `br.com.truckbox.driver`. Nenhum desses pacotes é substituído ou desinstalado.

Requisito mínimo Android 8 (API 26); alvo SDK 36. Layout de paisagem para 10” ou mais. Validar densidade, escala de fonte, imersivo e controle de brilho no aparelho real. APK debug é assinado com chave local de desenvolvimento, não com chave comercial. Guardar a mesma chave em canal seguro para atualizar uma homologação instalada; outra chave exige remover apenas o pacote Next antes de reinstalar.

## Compilar

Usar JDK 21 completo, Gradle 9.4.1 e SDK 36/build-tools 36.0.0. Configurar `ANDROID_HOME`/`local.properties` local sem commitar caminhos/segredos. O wrapper usa a mesma versão da base. Com a ferramenta disponível:

```bash
./gradlew :multimediaNext:assembleHomologDebug :multimediaNext:assembleDemoDebug
./gradlew :multimediaNext:testHomologDebugUnitTest :multimediaNext:lintHomologDebug
./gradlew :multimediaNext:assembleHomologDebugAndroidTest
npm ci --prefix backend-proposal/v1 --no-audit --no-fund
npm test --prefix backend-proposal/v1
npm run typecheck --prefix backend-proposal/v1
python3 scripts/check_next_secrets.py multimediaNext/build/outputs/apk/homolog/debug/multimediaNext-homolog-debug.apk
```

Node 24 com TypeScript executa testes de regras, RSA e Postgres WASM isolado. Não conecta à produção. Para configurar o APK com o projeto revisado, criar arquivo local de propriedades `url=https://<projeto>.supabase.co` e `publishableKey=sb_publishable_...`, passar `-Ptruckbox.cloud.config=/caminho/arquivo.properties` ao Gradle e manter esse arquivo fora do Git. Nunca usar chave secret/service_role/JWT legado de ingestão. Sem configuração, o app informa servidor não configurado; não adivinha um projeto.

Saídas: `multimediaNext/build/outputs/apk/homolog/debug/` e `.../demo/debug/`. Relatórios JVM em `multimediaNext/build/reports/tests/`; screenshots nativos em `multimediaNext/build/screenshots/homolog/`. Consulte TESTES_REALIZADOS.md para resultados efetivamente obtidos, pois os comandos acima são instruções, não declaração de aprovação.

## Instalar em aparelho de homologação

```bash
adb devices -l
adb install -r multimediaNext/build/outputs/apk/homolog/debug/multimediaNext-homolog-debug.apk
adb shell am start -n br.com.truckbox.multimedia.next/.MainActivity
```

Habilitar instalação de fontes externas ou depuração USB somente conforme administração do aparelho. Não instalar APK demo como se fosse homologação real. Confira nome, applicationId e SHA-256 do artefato antes de instalar. Não inserir token de ingestão nem service_role em nenhum campo do app.

O APK homolog usa FLAG_SECURE para evitar capturas/miniaturas de possíveis dados protegidos. O APK demo permite capturas dos dados fictícios para revisão visual. As capturas automatizadas JVM usam Views de teste, sem dados reais.

O puxador à esquerda abre cinco áreas. Em Configurações, aparência/host Core são preferências locais; permissões são somente leitura. O app confere `/api/health`/identidade e consulta GET `/api/sensors?offset=0&limit=8`, paginando observações reais. Em firmware sem esses recursos e sem vínculo Cloud exigindo UID, há fallback de diagnóstico `/api/state`, sem transformar números sem metadados em leitura atual. Nenhum reset, GPS, ACK de telemetria, configuração ou OTA é enviado ao Core. ACK de política é exclusivo da Cloud.

### Sensores pela rede do Core

1. Conecte o aparelho ao Wi-Fi do Core. Em **Configurações → Conexões**, selecione `192.168.4.1` ou `truckbox.local` e toque **Testar conexão agora**.
2. Confira diagnóstico HTTP e sinais mapeados. Abra um instrumento para origem/qualidade/captura. Se já houve observação utilizável, a próxima ausência/NA conserva o último valor com **Última · idade**. **Sem leitura** é para sinal que ainda não teve valor utilizável. J1587 `EM_VALIDACAO` permanece **Não verificado**; número antigo/provisório não autoriza cálculo/ação. O cache de instrumentos é volátil e não fornece saldo de tanque.
3. Para a topologia solicitada, o **Core entra na Starlink de 2,4 GHz**, e a **multimídia permanece somente no AP do Core**. O Core precisa da candidata 0.2.24 de compartilhamento, depois de revisão e instalação explícita pelo fluxo existente. Confira o [PR 27](https://github.com/rbasniak1989-lab/Truckbox/pull/27) e seu roteiro; o APK não instala firmware.
4. A 0.2.23 tem STA externa, mas não compartilha Internet pelo AP. Atualizar somente o APK não resolve essa limitação. Após eventual atualização revisada do Core, renove a conexão Wi-Fi/DHCP da multimídia; confira gateway/DNS, HTTPS público e só então Cloud autenticada. “Encaminhamento ativo” não comprova Internet/Cloud.

Contrato conferido na branch `fix/revb1-v0223-polling-fuel`, commit `bd723891cdaad2b23466c8311d13409b3d4a36c0`. Isso não comprova qual versão está efetivamente gravada nem disponibilidade dos 13 sensores no caminhão. PIDs com cadência longa podem aparecer desatualizados; o app não aumenta polling do barramento.

### Cloud, viagem e GPS

A 0.2.0 contém o cliente do novo canal; API/migração/vínculo ainda precisam publicação revisada. Em **Configurações → Acesso à Cloud**, o login usa conta **Supabase Auth do aplicativo**, diferente do login de GitHub ou do painel Supabase. A conta correspondente ao e-mail informado não foi encontrada; nenhuma senha é solicitada em chat. Preparar a conta pelo fluxo seguro de Auth, autenticar no app e aprovar remotamente o identificador público do aparelho com usuário/veículo/Core corretos. Solicitação do aparelho sozinha não libera acesso.

Sem API publicada, o app informa que aguarda publicação. Sem vínculo aprovado, informa que aguarda aprovação. Viagem e GPS vêm do veículo autorizado na Cloud, com horários reais; posição antiga não é apresentada como localização atual. O registro encontrado em 14/11 tinha captura de 30/10. O mapa depende de fornecedor/licença e segue indisponível. A multimídia não coleta GPS próprio nem transmite posições.

Financeiro/Jornada começam negados, são independentes e sem toggle local. Sessão/erro/expiração remove conteúdo protegido; a lease é de até 20 s, reduzida pelo tempo de rede. O app persiste somente refresh token cifrado, nunca valores financeiros, jornada, política ou viagem. DTC, pedágios, IA, jornada legal, contadores verificados e ledger de tanque persistido continuam pendentes em CONTRATOS_V1.md.

## Instrumentação e rollback

Com aparelho conectado e autorizado, executar `./gradlew :multimediaNext:connectedHomologDebugAndroidTest`. Complementar com roteiro físico T01–T50: instalar lado a lado, toque, noite 18–6/São Paulo, movimento/sem sinal, reinício e Core/Motorista/Cloud operando com a multimídia desligada. Não usar testes JVM como substituto desse roteiro.

Rollback deste incremento: desinstalar **somente** `br.com.truckbox.multimedia.next` (e `.demo` se instalado). Apps antigos/filas Cloud não são apagados. Nenhuma migração ou deploy de produção acompanha o APK.
