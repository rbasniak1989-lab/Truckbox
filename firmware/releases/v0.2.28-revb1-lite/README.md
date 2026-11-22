# Core Lite 0.2.28 para o Volvo FH 540

O BIN OTA desta versão destina-se ao **Core `tb-c6-d40592b9190c`** com o perfil J1708 de MID 172. A atualização reaproveita a validação física dessa unidade informada pelo proprietário. Na primeira inicialização em que consegue gravar a política, o Core salva a confirmação e habilita o automático. O agendador observa passivamente o barramento antes do primeiro pedido, respeita a arbitragem e bloqueia a transmissão ao detectar um MID concorrente.

## Instalação

1. Com o caminhão estacionado e a alimentação estável, abra o [painel do Core](http://192.168.1.251/#ota-title).
2. Entre como administrador. Em **Atualização por Wi‑Fi**, escolha **Core, neste dispositivo**, selecione `truckbox_core_0.2.28-lite_j1708_ota.bin` e clique em **Validar e atualizar**.
3. Após a reinicialização, aguarde o painel voltar. Com a rede J1708 ativa, o Core observa o tráfego e inicia as requisições quando a recepção estiver saudável. A indicação **J1708 ativa** e o contador **Pedidos enviados** mostram o resultado.

O comando **Parar requisições** grava a desativação. O marcador da migração faz essa escolha valer nos reinícios e nas reinstalações seguintes. Se precisar voltar ao 0.2.27-lite com as requisições desligadas, use **Parar requisições** antes de instalar o [BIN anterior do Core](../v0.2.27-revb1-lite/truckbox_core_0.2.27-lite_ota.bin). A política fica na NVS durante a troca de imagem OTA.

## Evidência do MID 172

No boot 602, o firmware antigo classificou `AC80218231` como conflito cerca de 21,6 ms após transmitir esse mesmo quadro. O replay reproduz a separação correta desse eco e confirma o bloqueio diante de um quadro independente com MID 172. As reinicializações e os incidentes sem quadro completo ainda precisam de diagnóstico próprio. [Registro do boot 602](../../espidf/test_evidence/v0225_cloud_readonly_audit.json) · [Auditoria dos PIDs estendidos](../../espidf/AUDITORIA_v0.2.18.md)

O painel atualizado mostra os contadores do diagnóstico compacto e o motivo de uma solicitação recusada. O arquivamento opcional usa a velocidade reportada pelo Core Lite e continua condicionado ao estado da rede e ao MID observado no boot.

## Reprodução e integridade

Código de compilação no commit `2d27e97ff0278de30a49d3ae90192a1036ff0df9`, com ESP-IDF 5.5.2 e destino `esp32c6`.

```sh
cd firmware/espidf
idf.py -B build-core-lite-228 -DTRUCKBOX_ROLE=core -DTRUCKBOX_REVB1_LITE=ON \
  -DTRUCKBOX_REVB1_CONFIRMED_CORE_UID=tb-c6-d40592b9190c -DPROJECT_VER=0.2.28-lite build
sh tests/run_all.sh
```

Ao receber o BIN e o manifesto, confira com `sha256sum -c SHA256SUMS.txt`. O resultado de bancada em C e JavaScript consta em `TESTES.txt`. A aplicação desta imagem e a contagem de pedidos no Volvo dependem da instalação pelo painel local.
