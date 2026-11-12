# Motorista0.6.2 — diagnóstico J1587

APK debug compilado e testes JVM aprovados na [CI](https://github.com/rbasniak1989-lab/Truckbox/actions/runs/43123525255), fonte do aplicativo `b46a10ba619bdfee61b6b465f0d43b8a808bc0ca`. O código do aplicativo desse commit coincide com o da entrega0.2.17; correções posteriores estão no Core/documentação.

Identidade preservada: `br.com.truckbox.driver`, versionCode62. O manifesto identifica SHA256, tamanho e origem. Não foi instalado automaticamente nem testado em telefone real nesta tarefa.

A aba Falhas usa as APIs locais do Core0.2.17 e cache pequeno. O histórico remoto precisa da publicação autorizada de `truckbox-diagnostics`, entregue em fonte e ainda não publicada. Os eventos de início/fim de viagem ficam em uma fila local até o Core confirmar a persistência. O aplicativo não controla o polling normal nem arma TX ao abrir a tela.

A assinatura é debug da CI. Compatibilidade da chave com o aplicativo instalado **não verificada**. Se o Android recusar atualização por assinatura, preservar os dados e usar a chave de assinatura existente para compilar; não desinstalar/apagar os dados como procedimento de atualização. A OTA do Core é independente da instalação deste APK.
