# Validação da candidata 0.6.5

Data: 15/11/2026. JDK 21, Gradle 9.4.1, AGP 9.2.1, Android SDK/build tools 36.

## Verificado nesta execução

- `:app:testDebugUnitTest`: **40 testes, zero falhas, zero ignorados**.
  Android 13 para persistência, datas PostgreSQL, transporte e permissão;
  Android 15/Robolectric com renderização nativa para Compose.
- `:app:assembleCloudTest` e `:app:assembleDebug`: concluídos.
- `:app:lintDebug`: **zero erros; 23 avisos**, principalmente recomendações
  KTX/atualizações e ícone. Nenhuma regra de lint foi desativada para passar.
- `:app:assembleDebugAndroidTest`: APK de teste instrumentado compilado.
  `adb devices -l` sem aparelho/emulador: **não executado em Android físico**.
- Telas de conteúdo Compose renderizadas em 412×915 e 320×640 dp, com rolagem
  para as informações e controles. Capturas em
  `app/build/screenshots/motorista/`; sem GPS/viagem fictícios na interface.
- Scanner dos fontes ativos e DEX/assets: sem clientes locais do Core, IP
  local/rotas Core, código privado de ativação ou chave server/service-role.
- Certificado e integridade do APK verificados com `apksigner`.
- Manifesto final: HTTP sem TLS desativado, backup/transferência de dados
  desativados, serviço GPS não exportado com tipo `location`.
- Quinze arquivos legados movidos para `app/legacy-core` foram comparados
  byte a byte com a base; preservados integralmente e fora do APK.
- Consulta real `truckbox-app/snapshot`: HTTP 200, `ok=true`, escopo do
  caminhão conferido. Uma credencial DRIVER nova, sem alterar Core/tablet.
  Nenhuma escrita artificial de GPS ou viagens feita para validar a entrega.

## Casos cobertos

Captura recente/antiga/futura, localização simulada, coordenadas e precisão
inválidas, ausência de velocidade/altitude, precisão aproximada preservada,
fila SQLite após reabrir, duplicação, falha de rede, confirmação de outro
caminhão/horário, credencial recusada, pausa, limite sem descartar histórico,
reenvio da posição recente antes do histórico, início/fim/próximo início de
viagem offline, falha parcial de sincronização, resposta atrasada, binding
explícito de ações anteriores à configuração, outro perfil, migração dos
tokens, falha de armazenamento da configuração, redirecionamento HTTP e
respostas inválidas/grandes. Nenhum teste usa a Cloud de produção para criar
GPS ou registros operacionais.

## APK para o teste do proprietário

- Arquivo: `TruckBox-Motorista-0.6.5-Cloud-GPS-TESTE.apk`
- Pacote: `br.com.truckbox.driver.cloud`, Android 8+, versionCode 65.
- Rótulo: **TruckBox Motorista Cloud**; instala ao lado do anterior.
- Tamanho: 19.202.493 bytes.
- SHA-256: `11bb07339ead0c6e08c522dbf5d18f5da22512a2f69ec613e234ee6c3da3f12c`.
- Certificado SHA-256: `13e9becd63c35a6a76615edff8540cfe6c6d73629ee47a04ce0b1d79479e0a6e`.

O APK padrão `br.com.truckbox.driver` e seu APK de testes instrumentados ficam
nos outputs técnicos do build. Não orientar instalação sobre um Motorista
com assinatura diferente ou desinstalação para contornar esse conflito.
APKs de CI usam a chave do respectivo build; conferir certificado antes de
usar como atualização de uma instalação existente.

## Reproduzir

```sh
gradle --no-daemon --max-workers=1 :app:testDebugUnitTest :app:lintDebug :app:assembleCloudTest :app:assembleDebugAndroidTest
python3 scripts/check_driver_cloud_apk.py app/build/outputs/apk/cloudTest/app-cloudTest.apk
```

O workflow `Motorista Cloud GPS` executa esses checks em PR e guarda APK,
relatórios e capturas. Testes instrumentados exigem instalação compatível e
aparelho/emulador conectado; `gradle :app:connectedDebugAndroidTest`.

## Ainda depende de campo

GPS real chegando à Cloud/multimídia; tela apagada e política de bateria do
celular; troca de rede, retomada após encerramento do processo e fila offline
no aparelho; envio independente dos sensores pelo Core; internet da Starlink
pela multimídia. Consumo, jornada, mapas/tarifas e multiusuário mantêm as
limitações registradas na auditoria. Não há declaração de produto pronto.
