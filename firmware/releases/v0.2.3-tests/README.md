# Core 0.2.3 — correção do endereço do Link — TESTES

Atualize somente o Core com truckbox_core_0.2.3_ota.bin.
Link 0.2.2 e configurações existentes permanecem compatíveis.

Corrige a descoberta do IPv4 do Link em sockets HTTP IPv6/IPv4 mapeados.
A 0.2.2 recebia o status do Link, mas continuava enviando lotes ao endereço
privado antigo 192.168.4.2. Leia PEER_FIX_v0.2.3.md.

Compilação ESP-IDF 5.5.2, testes host completos e regressão com sockets
TCP reais passaram. Falta confirmar o envio após a atualização física.

O ZIP completo contém fontes, imagens e scripts Windows. Seu Link 0.2.2
não precisa receber a imagem Link 0.2.3 incluída para uma instalação nova.
Nenhum token privado está incluído. Sem merge ou gravação automática.
