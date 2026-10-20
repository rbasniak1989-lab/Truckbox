# TruckBox Motorista 0.6.5 — testes de fila offline

A melhoria ainda exige validacao em caminhão real antes de uso como fonte unica de dados.

## Cenários críticos

1. **Internet desligada, Wi-Fi local funcionando:** desconectar a internet da Starlink sem desligar o Wi-Fi. O ESP32 continua acessivel e sua fila deve diminuir, enquanto a fila Android aumenta. Nao pode aparecer ACK sem o lote salvo.
2. **Internet de volta:** aguardar o envio da fila Android ate zero. Conferir na Supabase que todos os `seq` foram recebidos, na ordem correta e sem lacunas inesperadas.
3. **Matar o app/forcar reinicio:** com fila no Android e ainda sem internet, forcar parada e reabrir o Motorista. A contagem local deve se manter; nenhum registro pode desaparecer.
4. **Falha no ACK local:** interromper o Wi-Fi depois de gravar um lote. O ESP32 deve repetir o mesmo lote; o SQLite deve ignorar os itens identicos e nao duplicar na Cloud.
5. **UID errado:** trocar o UID configurado pelo de outro ESP32. O app nao pode confirmar amostras do dispositivo errado; ao restaurar UID correto, a fila deve continuar.
6. **Armazenamento insuficiente/512 MiB pendentes:** verificar que o Motorista bloqueia o ACK do ESP32 e avisa claramente, sem apagar a fila.
7. **ACK Cloud parcial/falha 401/500:** os dados persistidos permanecem no Android, com novas tentativas em 5–60 segundos; apos restaurar a Cloud, todos seguem sincronizados.
8. **Troca de motorista / troca de dispositivo:** a fila do dispositivo anterior nao pode ser enviada com credenciais do novo dispositivo nem apagada.
9. **Rota:** simular rota durante viagem ativa e conferir que essa funcionalidade segue funcionando.
10. **Recuperacao antiga:** arquivos salvos pelo aplicativo TruckBox Recovery (separado) nao sao importados automaticamente pelo Motorista v0.6.5.

## Pacote BETA separado (seguro para testar)

O GitHub Actions gera tambem `br.com.truckbox.driver.beta`, exibido como **TruckBox Motorista BETA**.
Ele pode ser instalado sem desinstalar o aplicativo normal e usa dados privados separados.

**Nao deixe os dois gateways ativos ao mesmo tempo.** Antes do teste, desligar `Gateway habilitado`
em Configuracoes do Motorista antigo e abrir o BETA. Se o antigo permanecer executando
em segundo plano, ambos podem competir pela fila/ACK do mesmo ESP32. No fim do teste,
desligar o gateway do BETA antes de voltar ao aplicativo principal.

O APK debug principal v0.6.5 tem assinatura gerada pela CI diferente da build v0.6.4.
**Nao desinstalar o antigo para instalar o novo** enquanto existirem dados pendentes.

## Checklist para instalar

- Instalar como atualizacao da v0.6.4 apenas se a assinatura Android for compativel; nunca desinstalar o app anterior com dados pendentes.
- Carregar bateria e permitir servico em segundo plano/execucao sem restricao de bateria.
- Validar acesso local a `truckbox.local` ou IP reservado do ESP32 pela Starlink.
- Conferir UID e token Cloud.
- Exigir que o teste de desligamento de internet e o retorno da sincronizacao estejam concluídos antes de liberar para uso sem acompanhamento.
