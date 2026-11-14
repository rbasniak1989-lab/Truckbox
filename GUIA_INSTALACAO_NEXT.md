# Instalação — TruckBox Next de homologação

Este incremento é uma base Android nativa para revisão. Não é liberação comercial nem validação de Core/ECU/Cloud em campo.

## APKs e coexistência

- Homologação: `br.com.truckbox.multimedia.next`, versão `0.1.1-homologacao`.
- Demonstração: `br.com.truckbox.multimedia.next.demo`, banner permanente de dados fictícios, sem rede.
- Multimídia antiga: `br.com.truckbox.multimedia`; Motorista: `br.com.truckbox.driver`. Nenhum desses pacotes é substituído ou desinstalado.

Requisito mínimo Android 8 (API 26); alvo SDK 36. Layout de paisagem para 10” ou mais. Validar densidade, escala de fonte, imersivo e controle de brilho no aparelho real. APK debug é assinado com chave local de desenvolvimento, não com chave comercial. Guardar a mesma chave em canal seguro para atualizar uma homologação instalada; outra chave exige remover apenas o pacote Next antes de reinstalar.

## Compilar

Usar JDK 21 completo, Gradle 9.4.1 e SDK 36/build-tools 36.0.0. Configurar `ANDROID_HOME`/`local.properties` local sem commitar caminhos/segredos. O wrapper usa a mesma versão da base. Com a ferramenta disponível:

```bash
./gradlew :multimediaNext:assembleHomologDebug :multimediaNext:assembleDemoDebug
./gradlew :multimediaNext:testHomologDebugUnitTest :multimediaNext:lintHomologDebug
./gradlew :multimediaNext:assembleHomologDebugAndroidTest
node backend-proposal/v1/handler.test.ts
python3 scripts/check_next_secrets.py multimediaNext/build/outputs/apk/homolog/debug/multimediaNext-homolog-debug.apk
```

Node 24 com suporte a TypeScript é usado pelos testes sem banco/rede. Para checar tipos, executar TypeScript 5.9.3 com modo estrito, módulo nodenext e allowImportingTsExtensions conforme CI/registro de testes. O comando Node direto reporta os casos individualmente.

Saídas: `multimediaNext/build/outputs/apk/homolog/debug/` e `.../demo/debug/`. Relatórios JVM em `multimediaNext/build/reports/tests/`; screenshots nativos em `multimediaNext/build/screenshots/homolog/`. Consulte TESTES_REALIZADOS.md para resultados efetivamente obtidos, pois os comandos acima são instruções, não declaração de aprovação.

## Instalar em aparelho de homologação

```bash
adb devices -l
adb install -r multimediaNext/build/outputs/apk/homolog/debug/multimediaNext-homolog-debug.apk
adb shell am start -n br.com.truckbox.multimedia.next/.MainActivity
```

Habilitar instalação de fontes externas ou depuração USB somente conforme administração do aparelho. Não instalar APK demo como se fosse homologação real. Confira nome, applicationId e SHA-256 do artefato antes de instalar. Não inserir token de ingestão nem service_role em nenhum campo do app.

O APK homolog usa FLAG_SECURE para evitar capturas/miniaturas de possíveis dados protegidos. O APK demo permite capturas dos dados fictícios para revisão visual. As capturas automatizadas JVM usam Views de teste, sem dados reais.

O puxador à esquerda abre cinco áreas. Em Configurações, aparência/host Core são preferências locais; permissões são somente leitura. O app consulta GET `/api/sensors?offset=0&limit=8` e pagina somente observações reais. Em firmware que responda 404 nesse recurso, consulta o diagnóstico legado `/api/state`, sem transformar números sem metadados em leitura atual. Nenhum reset, GPS, ACK, configuração ou OTA é enviado.

### Sensores pela rede do Core

1. Conecte o aparelho ao Wi-Fi do Core. Em **Configurações → Conexões**, selecione `192.168.4.1` ou `truckbox.local` e toque **Testar conexão agora**.
2. Confira o diagnóstico do endpoint, código HTTP e quantidade de sinais mapeados. Abra um instrumento para conferir origem/qualidade/captura. Sem observação ou com idade acima de cinco segundos, ele fica **Sem leitura**. J1587 com status `EM_VALIDACAO` permanece **Não verificado**, mesmo com número real recebido.
3. Para sensores e Cloud simultâneos, use Core e multimídia na **mesma rede Starlink de 2,4 GHz**, com comunicação entre clientes. No campo **Core na rede Starlink**, aplique o IP privado LAN/STA mostrado no painel do Core, sem esquema ou porta. O IP de exemplo do AP não é o IP da LAN.
4. A 0.2.23 já prevê STA externa. O AP do Core é de configuração e não fornece internet por NAT; permanecer só nele não dá acesso à Cloud. Não há necessidade de atualizar firmware apenas para usar esse suporte já existente.

Contrato conferido na branch `fix/revb1-v0223-polling-fuel`, commit `bd723891cdaad2b23466c8311d13409b3d4a36c0`. Isso não comprova qual versão está efetivamente gravada nem disponibilidade dos 13 sensores no caminhão. PIDs com cadência longa podem aparecer desatualizados; o app não aumenta polling do barramento.

Cloud autenticada real, DTC, mapa/rota/GPS, pedágios, jornada e saldo de tanque persistido aguardam as integrações listadas em CONTRATOS_V1.md. Financeiro/Jornada começam negados; não existe toggle local de liberação. A 0.1.1 é o incremento local Core/Home; a preparação do novo canal Cloud é trabalho separado.

## Instrumentação e rollback

Com aparelho conectado e autorizado, executar `./gradlew :multimediaNext:connectedHomologDebugAndroidTest`. Complementar com roteiro físico T01–T50: instalar lado a lado, toque, noite 18–6/São Paulo, movimento/sem sinal, reinício e Core/Motorista/Cloud operando com a multimídia desligada. Não usar testes JVM como substituto desse roteiro.

Rollback deste incremento: desinstalar **somente** `br.com.truckbox.multimedia.next` (e `.demo` se instalado). Apps antigos/filas Cloud não são apagados. Nenhuma migração ou deploy de produção acompanha o APK.
