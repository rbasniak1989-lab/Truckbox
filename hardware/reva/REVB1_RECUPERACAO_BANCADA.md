# TruckBox Rev.B.1 — recuperação da placa fabricada (bloqueio de liberação)

Data de abertura: 2026-11-07. **Estado: hardware não aprovado; nenhum reparo físico executado ou verificado por este documento.**

## Evidência disponível

- `firmware/espidf/HARDWARE.md`: `TP1=3,3 V`, `TP5/CORE_EN=1,6 V`, continuidade de TP5 a U1/3. O nível de liberação de reset exige aproximadamente 2,48 V (segundo a conferência). O bootloader USB chegou a responder: não prova inicialização confiável sob carga.
- `hardware/reva/generate_board.py`, `fp_sot23_3`: posiciona pad 1 em `(-1.05,+0.95)` e pad 2 em `(-1.05,-0.95)`. Para o SOT-23 conforme orientação adotada no gerador, a pinagem física ficou invertida 1↔2 em **Q1/Q2/Q3/Q4**. **Não resolver por rotação do componente.** Verificar os arquivos de fabricação efetivamente enviados antes de prescrever trilhas/cortes.
- Mesmo arquivo, `fp_sot23_6`: posições 1↔3 e 4↔6 espelhadas em **U8/TPL5010**.
- J5/nano-SIM: segundo `HARDWARE.md`, contato C5/GND sem net nos arquivos revisados; confirmar continuidade até terra **na placa** antes de concluir ausência elétrica.
- Q1: chave de `3V3_LINK`. Q2: chave de `3V3_SD`. Q3/Q4: ACC/watchdog. U8: watchdog. O circuito de watchdog também requer revisão por causa de sua alimentação comutada e interação com CORE_EN.
- O `DRC` e o `validate_revb_dfm.py` do projeto original não validam a correspondência **datasheet ↔ pino físico ↔ pad KiCad ↔ net ↔ rotação da montagem**. Passar DRC não valida o hardware.

## Regra para este lote

**NÃO conectar ao caminhão nem aplicar 24 V sem preparação. Não usar GPIO16/17 nem GPIO1 como saídas. Não fazer pontes, girar CIs ou cortar trilhas por dedução de imagem.**

O protótipo deve ser medido em bancada por técnico com fonte de corrente limitada, multímetro e recursos SMD. Desenergizar antes de medir continuidade/resistência. Registrar fotos das duas faces, identificação do pino 1 e os arquivos JLC usados na fabricação (Gerber, BOM, CPL/posições). Descartar alimentação se houver curto confirmado.

## Sequência de recuperação em bancada

1. **Estado original:** medir a resistência `3V3_MAIN→GND`, `3V3_SD→GND`, `3V3_LINK→GND`, `CORE_EN→GND` com a placa desligada. Identificar referências reais na serigrafia. Fotografar antes de intervir.
2. **Fonte/reset:** com alimentação controlada, registrar tensão e corrente de entrada, `TP1`, `TP2/GND`, `TP5/CORE_EN`, pino físico 3 de U1 e `WDT_3V3`. Conferir se `CORE_EN` continua em 1,6 V. Não presumir que a origem seja U8.
3. **Isolamento U8 (somente técnico SMD):** com a placa sem energia, retirada temporária do U8 pode isolar sua influência sobre `CORE_EN`, preservando R13 e C19, como prevê HARDWARE.md. Repetir as medições com limitação de corrente. Se EN subir e o Core iniciar de maneira estável, isso *localiza a influência do ramo U8*, mas não valida o restante da placa. Se EN continuar baixo, investigar outras cargas e a rede R13/C19/CORE_EN. Não interligar EN diretamente a 3V3 para mascarar defeito.
4. **Q1 / Link:** confirmar por continuidade a rede no pad de cada terminal físico, comparando datasheet AO3401A: 1 Gate, 2 Source, 3 Drain. Saída prevista `3V3_LINK`; entrada `3V3_MAIN`; comando `LINK_PWR_N/GPIO16`. Para teste de bancada, uma alimentação permanente do Link só pode ser considerada **após isolamento do MOSFET errado e aferição de ausência de curto**, com ligação de trilhas explicitamente identificadas e corrente limitada. Não unir pads por posição visual.
5. **Q2 / microSD:** mesmo procedimento; `SD_PWR_N/GPIO17` deve ir ao Gate 1, `3V3_MAIN` ao Source 2, `3V3_SD` ao Drain 3. O microSD pode receber alimentação parasita pelos sinais SPI; não interpretar isso como alimentação correta. Somente depois de corrigir/isolar Q2, confirmar 3,3 V estáveis sob acesso ao cartão, montagem FAT, escrita, leitura, remoção e reinício.
6. **Q3/Q4 e ACC:** conferir funções físicas dos transistores, redes de comando e reset antes de ativar qualquer lógica do ACC/watchdog. Uma correção em U8 não corrige Q3/Q4. Se for necessário desativar o watchdog para a bancada, rotular o protótipo **sem watchdog externo** e sem validação automotiva.
7. **SIM:** com a placa desligada verificar J5 contato C5 ao GND por continuidade. Se ausente, definir reparo após verificar o footprint físico e o conector instalado. Não habilitar modem antes de conferir `3V3_LINK`, alimentação GPS, `LTE_3V8`, GND SIM.
8. **Firmware:** manter os controles de `GPIO16/17` desativados até correção validada; `GPIO1` sempre entrada até consertar watchdog. Preferir a correção de escalonamento `v0.2.0-wdt1` para testes de tráfego, mas instalá-la apenas após boot/reset estável e manter backup/rollback.
9. **Aceite funcional:** por pelo menos diversos ciclos de energização/desenergização, verificar Core USB/OTA/console, Link Wi-Fi, recepção CAN/J1708 em bancada e depois com barramentos reais em modo somente leitura, microSD gravando e recuperando logs após reinício, FRAM, GPS e LTE. Registrar tensões, correntes, vídeo/logs, CRC/ACK e versão exata do firmware. Só então liberar teste no caminhão.

## Correção de projeto para nova fabricação (NÃO executada)

- Reconstruir a PCB a partir **dos arquivos finais efetivamente fabricados**; a fonte geradora no GitHub, sozinha, não comprova identidade dos Gerbers do pedido.
- Corrigir geometria física dos pads SOT-23 (Q1–Q4) e SOT-23-6 (U8), **refazer o roteamento conforme as nets físicas** e validar símbolos/land patterns de **todos os componentes**, não apenas os identificados.
- Confirmar conexões J5/C5, pinos de alimentação/ENABLE, watchdog em domínio comutado, polaridade C67/C68 e montagem de C1/C2, Q50/R70, D3/D4, J3.
- Estabelecer verificação que bloqueia a fabricação quando não houver tabela aprovada de pad físico ↔ net ↔ componente ↔ CPL ↔ datasheet. Reexecutar KiCad DRC, ERC, montagem virtual e ensaios elétricos antes de repetir lote.

## FONTES

- `firmware/espidf/HARDWARE.md` e `firmware/espidf/PENDING.md`
- `hardware/reva/generate_board.py`, `hardware/reva/apply_layout_sd_power_ctrl.py`, `hardware/reva/generate_revb_jlc_package.py`
- AO3401A fabricante: https://www.aosmd.com/sites/default/files/res/datasheets/AO3401A.pdf
- TPL5010-Q1 fabricante: https://www.ti.com/lit/gpn/TPL5010-Q1

**Sem as medições pós-isolamento e o pacote final da fabricação, não há como certificar que o protótipo funcionará nem prescrever fios/pads exatos com segurança.**
