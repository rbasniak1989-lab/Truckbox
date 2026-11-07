# Atualização por Wi‑Fi

Após a primeira gravação USB e o vínculo, a página do Core permite atualizar **Core ou Link** pela rede local. Use o arquivo de aplicação correspondente a cada módulo.

## Atualizar

1. Conecte notebook e Core à mesma rede do celular/roteador, ou acesse diretamente o AP do Core.
2. Abra `http://truckbox.local/`, o IP LAN mostrado no USB ou `http://192.168.4.1/` no AP.
3. Entre com a chave de administração obtida pelo comando USB `setup`.
4. Em **Atualização por Wi‑Fi**, selecione o destino:
   - **Core, neste dispositivo**: `truckbox_core_<versão>_ota.bin`;
   - **Link, pela rede interna**: `truckbox_link_<versão>_ota.bin`.
5. Selecione o `.bin`, envie e aguarde validação e reinício.
6. Aguarde pelo menos 15 segundos após o boot e consulte novamente o estado.

Envie a **aplicação OTA**. Não envie ZIP, bootloader, tabela de partições ou pacote USB concatenado. O receptor também confere a função da imagem.

Na atualização do Link, o Core encaminha o arquivo pela rede local. O Link precisa estar alimentado e vinculado. No modo privado, o endereço é `192.168.4.2`; no modo Wi-Fi externo da 0.2.2, é o IP LAN aprendido por mensagem autenticada (`network.link_host`). O Core permanece com sua aplicação; o módulo atualizado reinicia.

Atualize um módulo por vez. Versões futuras devem manter compatibilidade de protocolo com a anterior e prever a ordem de atualização. Não há troca atômica dos dois firmwares como uma única operação.

## Verificações e partições

Antes de gravar, o receptor verifica ESP32-C6, indicação de flash de 8 MB, descritor e nome `truckbox_core` ou `truckbox_link`.

A aplicação deve caber no slot de **3 MiB** e conter SHA-256 anexado pelo SDK. O SDK valida a imagem completa ao fim da recepção. A escrita ocorre no slot inativo; a seleção de boot muda apenas após a validação.

| Partição | Offset | Tamanho |
|---|---:|---:|
| OTA 0 | `0x20000` | `0x300000` — 3 MiB |
| OTA 1 | `0x320000` | `0x300000` — 3 MiB |
| Seleção OTA | `0xf000` | `0x2000` |
| Armazenamento | `0x620000` | `0x1e0000` |

Há limite de duração de recepção e uma OTA por vez. Uma transferência interrompida antes da seleção de boot mantém a aplicação atual selecionada. Após selecionar a nova imagem, outra OTA fica bloqueada até o reinício.

## Confirmação e retorno automático

No primeiro boot após OTA, o programa inicializa NVS/configuração, rede, serviços e HTTP. Após 15 segundos, verifica a memória livre e confirma a imagem.

Se a nova aplicação falhar na inicialização, reiniciar ou sofrer reset antes da confirmação, o rollback permite retornar à imagem anterior válida.

Ausência de roteador, SIM, sinal celular, fix GPS, tráfego CAN/J1708 ou cartão é um estado de diagnóstico; não é, por si só, falha do firmware. Uma mídia ausente não deve impedir configuração e atualização.

Na **primeira gravação USB**, não existe imagem anterior válida para recuperar. A recuperação continua sendo pelo bootloader USB.

OTA preserva NVS e registros. Futuras mudanças de formato persistente devem ler dados anteriores e evitar migração destrutiva antes da confirmação; caso contrário, a imagem antiga poderia não conseguir voltar.

## Resolver falhas

| Situação | Próximo passo |
|---|---|
| Imagem incompatível | Confira Core/Link, flash de 8 MB e se é `*_ota.bin` deste projeto. |
| Upload interrompido | Reconecte à página, confira versão/partição e tente novamente após o módulo responder. |
| Página some após salvar Wi‑Fi | Reconecte à rede escolhida; consulte IP no USB ou use o AP de configuração. |
| `truckbox.local` não resolve | Use o IP LAN mostrado no USB/diagnóstico. |
| Link indisponível | Confira alimentação e chave de vínculo; no modo externo confira mesma LAN, `core_host` e `network.link_host`; no privado confira SSID/senha e `192.168.4.2`. |
| Nova versão volta à anterior | Veja o USB: o novo boot não foi confirmado. |
| Nenhuma aplicação responde | Entre no bootloader pelo BOOT e faça manutenção USB; confira EN e alimentação. |

O estado informa versão e partição em execução. Pode-se testar OTA com o mesmo `.bin`: a versão permanece igual, mas o slot deve alternar após validação e reinício.

## Credenciais e transporte

A página usa HTTP local, chave Bearer e WPA2 do AP. O vínculo com o Link tem chave separada. As chaves ficam em memória no navegador durante a sessão; consultas públicas não devolvem senhas ou tokens.

SHA-256 e o SDK verificam integridade e compatibilidade. **Não representam assinatura de firmware, Secure Boot, flash encryption ou prova criptográfica da origem.** Use uma rede local confiável e mantenha a porta HTTP fora da Internet.

O HTTPS para a nuvem é outro caminho: verifica certificados e depende de relógio válido. A placa não precisa de chave administrativa do Supabase.

## Gerar uma próxima versão

Com ESP-IDF v5.5.2 ativado:

```sh
idf.py -B build-core -DTRUCKBOX_ROLE=core -DPROJECT_VER=0.1.1-b1 build
idf.py -B build-link -DTRUCKBOX_ROLE=link -DPROJECT_VER=0.1.1-b1 build
python tools/build_release.py
```

O empacotador exige os dois builds, versões iguais, ESP32-C6 e flash de 8 MB. Copia binários reais, calcula hashes e gera manifesto, imagens OTA e ZIP com fontes/scripts. Não substitui uma compilação ausente por binário de exemplo.

Uma mudança de esquema, protocolo ou pinagem exige testar atualização, retorno, credenciais preservadas e interrupção de energia. Esse ciclo físico ainda faz parte do comissionamento da B1.
