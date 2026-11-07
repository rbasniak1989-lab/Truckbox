# Core 0.2.3 — descoberta do Link em HTTP dual stack — TESTES

Atualize somente o Core para corrigir a descoberta do endereço do Link.
O Link 0.2.2 continua compatível; não precisa ser regravado para esta correção.
O pacote completo também inclui Link 0.2.3 recompilado, sem mudança funcional
no componente Link em relação à 0.2.2.

## Falha encontrada e evidência

Diagnóstico físico recebido: Core 0.2.2 em 192.168.1.251, Link 0.2.2 em
192.168.1.92. Ambos estavam conectados ao roteador, com relógio sincronizado.
Link enviou 187 mensagens de status ao Core sem falha, mas não recebeu lotes
para enviar à nuvem. Core conservava `network.link_host=192.168.4.2` e registrava
`link_http:0 ESP_ERR_HTTP_CONNECT no_valid_ack`.

A configuração efetiva do ESP-IDF habilita `CONFIG_LWIP_IPV6=y`. O HTTPD de
ESP-IDF 5.5.2 cria o servidor com `socket(PF_INET6, SOCK_STREAM, 0)` nesse caso.
lwIP, em sockets desse tipo, devolve clientes IPv4 como IPv6 mapeado
`::ffff:a.b.c.d` em `getpeername`. A 0.2.2 usava `sockaddr_in` e exigia AF_INET,
descartando silenciosamente o endereço do Link apesar de aceitar seu status.

Referências do SDK: `components/esp_http_server/src/httpd_main.c`, função
`httpd_server_init`; `components/lwip/lwip/src/api/sockets.c`, `lwip_getaddrname`.

## Correção

- Usa `sockaddr_storage` e valida o tamanho devolvido pelo socket.
- Aceita IPv4 nativo e extrai o IPv4 de um endereço IPv6 mapeado.
- Rejeita IPv6 nativo neste cache IPv4; não fabrica nem trunca endereço.
- Aprende o endereço somente após autenticação da mensagem do Link e validação
  do JSON, mantendo a verificação de sub-rede local e a expiração do cache.
- O endereço continua vindo do socket; um campo `host` no JSON não o escolhe.
- Configurações, chaves, pinos, filas, persistência e ACK permanecem preservados.

## Testes

Teste nativo cria servidores TCP locais reais e conecta clientes IPv4/IPv6:
reproduz a condição AF_INET6 que a 0.2.2 rejeitava, verifica aprendizagem do IPv4
mapeado e do IPv4 nativo e rejeita IPv6 nativo/descritor inválido. Usa o código C
de produção e ASan/UBSan; não se limita a testar uma cópia do parser de IP.
Continuam os testes de endereço próprio/externo, mudança DHCP e expiração.

Suites completas host e builds ESP-IDF de Core/Link foram executados novamente.
Evidências em `test_evidence/v023_*`. A ingestão Supabase v25 permanece ativa;
não houve nova implantação de backend nem alteração de token nesta correção.
Ainda falta validar a descoberta e o envio após instalar o Core na placa.

## Atualizar e conferir

1. Na página do Core, selecione **Core, neste dispositivo** e envie
   `truckbox_core_0.2.3_ota.bin`. Não envie bootloader, ZIP ou imagem do Link.
2. Aguarde o reinício e aproximadamente um minuto para reconexão/tentativas.
3. Core deve mostrar versão 0.2.3 e `network.link_host=192.168.1.92`, enquanto
   esse continuar sendo o IP DHCP do Link. Se o IP mudar, deve aprender o novo.
4. Verifique ACKs no Core e `uplink.http_status`/`successful_acks` no Link.
   HTTP 200 e ACK crescente comprovam o envio; estar no roteador não basta.
5. Confira também o recebimento real no Supabase pelo UID do Core. Esta entrega
   não declara recebimento antes dessa confirmação física.

No diagnóstico recebido, J1708 já tinha 24.269 frames; CAN permanecia com zero.
A flash tinha 1.793 registros pendentes e 28.499 recusas por lotação no boot.
Os registros recusados não foram gravados e não podem ser recuperados pela
nuvem. Não houve limpeza automática: os registros persistidos foram preservados.
Após o primeiro envio, a vazão sustentada ainda precisa ser medida: a flash sem
SD é finita e a correção de endereço não garante captura sem perdas em carga.

Não houve merge nem gravação automática. Versões anteriores permanecem arquivadas.
