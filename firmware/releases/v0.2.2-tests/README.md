# TruckBox Rev.B.1 v0.2.2 — VERSÃO DE TESTES

Core captura J1939/J1708; Link envia por HTTPS pela internet do Wi-Fi.
Atualize os dois módulos. O ZIP contém fontes, aplicações OTA, imagens USB,
bootloaders, partições, scripts Windows, hashes e relatórios.

Leia WIFI_LINK_CLOUD_v0.2.2.md antes de alterar a rede do Link.
Core e Link precisam estar na mesma LAN de 2,4 GHz. O modo privado antigo
continua disponível com o SSID externo do Link vazio.

A ingestão v25 já foi implantada e testada por HTTPS/banco sem GPS.
O cadastro do Core foi preparado com credencial individual entregue em privado.
Nenhum token é incluído neste pacote público.

Compilação ESP-IDF 5.5.2 e suites host passaram. O envio físico Core → Link →
Wi-Fi → nuvem, vazão, reconexão e OTA na placa precisam ser ensaiados.
A flash sem SD é finita; não substitui armazenamento offline ampliado.

Nenhum merge ou gravação automática. Releases anteriores preservadas.
