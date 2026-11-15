# Motorista Cloud / GPS 0.6.5 — teste

## Instalar sem perder o aplicativo anterior

Instale `TruckBox-Motorista-0.6.5-Cloud-GPS-TESTE.apk` no celular Android 8 ou
superior. O novo ícone chama **TruckBox Motorista Cloud**, pacote
`br.com.truckbox.driver.cloud`. Instala ao lado do Motorista anterior; mantenha
o anterior e seus registros. As assinaturas dos APKs anteriores não são
uniformes, portanto não desinstale o aplicativo antigo para forçar atualização.

O pacote de teste começa com configuração e dados locais próprios. Depois da
configuração, consulta os registros existentes na Cloud. Registros que só
existem offline no aplicativo antigo não são transferidos automaticamente.

## Configurar o celular

1. Conecte o celular à internet, por dados móveis ou Wi-Fi com internet.
2. Abra **TruckBox Motorista Cloud → Config → Código privado do Motorista**.
3. Cole o código do arquivo privado fornecido separadamente e toque em
   **Salvar acesso**. O código da multimídia pertence à central e não serve
   para este aplicativo.
4. Deixe **Compartilhar GPS com a Cloud** ligado. Permita localização precisa
   e notificações. Se necessário, toque em **Iniciar GPS / Revisar permissões**.
5. Em **Status**, confira **Resposta recebida da Cloud** para operações. Espere
   uma posição real do celular e confira **Última posição confirmada na Cloud**
   e **Último envio confirmado**. Sem posição, não há envio de coordenadas.

Código salvo confirma armazenamento no celular. Só a resposta de cada API
confirma seu acesso efetivo. Não envie seu código/token em mensagens, prints
ou arquivos públicos. O token atual é revogável pelo cadastro do equipamento;
não possui expiração automática no contrato legado.

## Testar na viagem

- Abra o aplicativo antes de iniciar e mantenha a notificação do serviço GPS
  ativa. Evite **Forçar parada**. Ajuste a economia de bateria do fabricante
  se ela encerrar o serviço; o comportamento precisa de validação no aparelho.
- Confirme uma posição enviada, apague a tela e confira mais tarde o horário
  de captura confirmado. O celular não precisa entrar no Wi-Fi do Core.
- Para testar offline com o veículo parado, desligue a internet mantendo o
  GPS disponível. A fila de posições deve crescer; ao voltar a internet, o
  ponto mais recente é enviado primeiro e depois o histórico.
- Pause o GPS em Config para interromper coleta e reenvio. Posições já
  pendentes ficam preservadas e podem ser enviadas quando você reativar.
- Viagens e lançamentos feitos neste APK ficam na fila local até confirmação.
  Verifique operações pendentes e dados na Cloud antes de alternar entre apps.
  Evite iniciar/encerrar a mesma viagem nos dois aplicativos simultaneamente.

O fluxo é **celular/GPS → Cloud → multimídia**. Sensores continuam sendo lidos
do **Core → multimídia**. Para a multimídia acessar a Cloud pelo Wi-Fi do Core,
o compartilhamento Starlink/NAT do firmware precisa funcionar independentemente
deste APK do celular.

## Quando não atualizar

- **Sem confirmação da Cloud:** conferir internet, código do celular e se o
  equipamento está ativo. Não é necessário configurar IP do Core.
- **Aguardando posição:** conferir localização do Android e permissão, iniciar
  o serviço e aguardar em local com recepção. Localizações simuladas não são
  usadas. Localização aproximada mantém a precisão informada pelo Android.
- **Posições aguardando envio:** fila persistente; não limpe os dados do app.
- **Código não salvo:** conferir o arquivo do celular. Nenhum token precisa
  ser embutido em um novo APK.

Testes em computador não comprovam GPS físico, localização com tela apagada,
entrega à multimídia, consumo, sensores ou ponte Starlink. Registrar esses
resultados antes de homologar.
