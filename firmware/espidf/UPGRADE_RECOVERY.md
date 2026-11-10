Para a candidata **0.2.12**, consultar [guia e relatório atuais](REV_B1_0.2.12_CANDIDATA.md), incluindo a sincronização obrigatória antes de rollback manual. O conteúdo abaixo registra as versões anteriores.

# Atualização e recuperação manual

Esta é a **VERSÃO DE TESTES0.2.3**. Não houve gravação automática nem merge.
Preserve o pacote funcional v0.1.0-b1 e leia HARDWARE.md e PENDING.md antes do ensaio.
A ingestão v25 já foi implantada; o caminho Wi-Fi do Link e suas credenciais
estão descritos em WIFI_LINK_CLOUD_v0.2.2.md. J1708 ativo começa desarmado.

## Atualização por Wi-Fi

1. Guarde configuração e backlog pendente; confirme alimentação estável e o estado do watchdog descrito na base. Use apenas a imagem da função correta.
2. No console USB, `setup` obtém os dados locais gerados na placa; `status` mostra erros, filas, mídia e captura. Não publique senhas/tokens.
3. Na página de OTA existente, envie `truckbox_core_0.2.3_ota.bin` para o Core. O Link 0.2.2 pode permanecer instalado. Em uma instalação nova, há também Link 0.2.3 no pacote. Leia PEER_FIX_v0.2.3.md e WIFI_LINK_CLOUD_v0.2.2.md.
4. Não espere GPS para validar aquisição. Confira `gps_status=NO_FIX`, ausência de lat/lon, contadores RX e registros `seq` persistentes. A ausência de fix não deve impedir boot nem gravação.
5. Confirme no `status` a mídia real e contadores de confirmação/erros; `store_queue_accepted` não significa gravação no cartão. Teste recuperação do backlog sem Android/internet e depois de reboot.

A atualização OTA preserva NVS e partições de armazenamento. A tabela permanece a
mesma: aplicação começa em0x20000, slots de3MiB e rollback IDF. Falha de boot de
imagem pendente usa o mecanismo já existente. Inicializar serviços não comprova
periféricos físicos: teste em bancada antes de aceitar esta versão.

## Windows / recuperação USB

Instale Python3 e esptool5.x:

```bat
py -m pip install "esptool>=5,<6"
flash_core.cmd COM3
```

Substitua COM3 pela porta realmente identificada. `flash_link.cmd` só deve ser
usado no módulo Link correto. Os scripts verificam SHA256, papel da imagem,
ESP32-C6 e flash8MB; usam offsets do manifesto e não executam erase-flash.
Preservam NVS e filesystem. A gravação USB regrava dados iniciais OTA e a aplicação
no slot0; não deve ser tratada como atualização OTA nem garantir preservação da
imagem anterior nesse slot. Siga somente o procedimento físico já documentado em
HARDWARE.md/README da release funcional; não improvise ligações.

Se a versão de testes falhar, use a imagem OTA anterior da mesma função quando a página ainda
estiver acessível, ou o **pacote original v0.1.0-b1** em recuperação USB manual.
Mantenha a mesma tabela de partições; nunca formate SD nem apague NVS como tentativa
rotineira de recuperação. Antes de reinstalar, copie os logs para investigação.

## Integridade

SHA256 do ZIP funcional original `truckbox-b1.zip`:
`43ec5b71d8c3346e0a6d4667c182080d8a900ea55bc526c9b1acf896fe0da1a5`.
Foi comparado com a branch funcional e permaneceu idêntico. Os hashes das novas
imagens e arquivos estão em `manifest.json` e `SHA256SUMS.txt` do pacote novo.
