# Core Rev.B.1 0.2.9 — VERSÃO DE TESTES

Atualizar somente o Core; manter o Link instalado.

- Aplicação OTA: truckbox_core_0.2.9_ota.bin.
- Pacote completo: TruckBox_CORE_0.2.9_TESTES.zip, com fontes, binários de recuperação,
  bootloader, partições, hashes e script Windows.
- Fontes separados: truckbox_firmware_source.zip.

Pelo painel do Core, faça OTA manual usando somente o arquivo de aplicação.
Confira a versão 0.2.9 após o reinício. Não use o binário do Core no Link.

Ainda parado, informe MID livre e confirme a instalação elétrica; execute o teste
J1708 começando no PID 26. Após três ecos completos e uma resposta candidata do
teste atual, o painel libera Ativar durante a viagem. A promoção também funciona
após concluir o teste, dentro do prazo de 60 segundos.

O modo de viagem mantém os 17 PIDs ativos em movimento, uma consulta por vez e
intervalo mínimo de 1,2 segundo, por até 12 horas. Reboot desarma. Parar e OTA
interrompem; três falhas consecutivas exigem investigação e reinício.

Não foi realizado teste físico desta versão: a placa estava desligada.
Leia J1708_FIX_v0.2.9.md e ACTIVE_J1708.md dentro do pacote.
Não houve merge, gravação automática, limpeza dos dados nem alteração da 0.1.0-b1.

USB manual de recuperação: extraia o pacote e use flash_core.cmd COM3 conforme
as conexões de bootloader já documentadas. O script valida chip, 8 MB e hashes,
preserva NVS/storage e não executa erase_flash. Prefira OTA para a atualização.
