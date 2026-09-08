# TruckBox Rev.A PCB

Branch de desenvolvimento: `hardware-reva-pcb`.

Objetivo: PCB Rev.A para prototipo TruckBox, 100 x 65 mm, 4 camadas, 2 x ESP32-C6-WROOM-1-N8, duas CAN fisicamente read-only, GNSS, FRAM, microSD e alimentacao automotiva 24 V -> 3,3 V.

## Regra de liberacao

Nao enviar arquivos a JLCPCB enquanto o workflow `TruckBox PCB CI` nao gerar DRC sem violacoes bloqueantes e o pacote final nao tiver sido inspecionado no visualizador da JLCPCB.

## Fluxo

1. `generate_board.py` gera a placa KiCad versionada.
2. GitHub Actions instala KiCad CLI.
3. DRC e estatisticas da placa sao executados.
4. Quando o layout estiver congelado, a Action passa a gerar Gerber/Drill/position files.
5. BOM e CPL da JLCPCB sao congelados apenas apos os footprints e orientacoes finais.

A Rev.A e prototipo: montar primeiro lote minimo e validar bancada + 48-72 h no FH antes de replicar.
