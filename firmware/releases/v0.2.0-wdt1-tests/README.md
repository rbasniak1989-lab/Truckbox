# TruckBox Rev.B.1 0.2.0-wdt1 — correção de watchdog / TESTES

[**Baixar o ZIP corrigido**](./Truckbox_B1_firmware_0.2.0-wdt1_TESTES.zip?raw=true)

**Para esta correção, atualize somente o Core.** O Link pode continuar na versão
instalada: sua lógica funcional não foi modificada. O Link recompilado está
incluído apenas por completude/alinhamento de identificação do pacote.

Defeito de campo: `task_wdt` informou IDLE CPU0 sem executar e `tb_decode` em
execução contínua. O laço original foi reproduzido em modelo de fila sempre cheia.
Decoder e gravador agora cedem CPU após um lote limitado. O watchdog continua
habilitado; senhas/NVS, formato/ACK e dados confirmados são preservados.

Novo Core: **0.2.0-wdt1**. Compilação ESP-IDF5.5.2/C6 e testes host passaram.
**Ainda precisa confirmar na placa** o desaparecimento do aviso, resposta USB,
Wi-Fi/AP, vazão/perdas e persistência sob o mesmo tráfego.

1. Baixe e extraia o ZIP.
2. Leia [relatório da correção](./WDT_FIX_REPORT.md).
3. Feche o monitor serial (Ctrl+]) antes de usar a COM3 no gravador.
4. Para USB, siga o procedimento físico já validado/documentado para a sua placa,
   depois execute `flash_core.cmd COM3` na pasta extraída. O script não apaga NVS
   nem filesystem. Não improvise ligações/straps.
5. Depois, `monitor.cmd COM3`, comando `setup`/`status`; confira a versão0.2.0-wdt1.
   Teste primeiro com transmissão ativa J1708 desarmada.

Não houve flash nem merge automático. A versão0.2.0 original e a funcional
v0.1.0-b1 continuam arquivadas. A correção Cloud já documentada segue pendente
antes de habilitar o gateway antigo para os registros RAW/DECODED.

Pacote: 3718780 bytes. SHA256:
`9ddf72a8dd63ea6b586dc114eaca73b83d1d2c2ca1c450a1909e2409d87b9444`.
Fonte compilado: `20055389de302aea23f8974453e4f816ebfe1806`.
