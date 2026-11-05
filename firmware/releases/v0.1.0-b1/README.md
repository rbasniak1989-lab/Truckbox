# Truckbox Rev B1 — firmware 0.1.0-b1

Pacotes para primeira gravação USB e atualizações posteriores por Wi-Fi.

- `truckbox-b1-core.zip`: somente Core, suficiente para a primeira gravação na COM3.
- `truckbox-b1.zip`: Core, Link, código-fonte, guias e ferramentas.

Os binários são os mesmos já compilados e verificados com ESP-IDF v5.5.2 para ESP32-C6 com flash de 8 MB. Ambos os ZIPs tiveram integridade CRC e SHA256 conferidas. Os testes no computador passaram; a validação física dos periféricos, da atualização Wi-Fi e da recuperação de firmware ainda depende da placa.

## Primeira gravação do Core

1. Baixe e extraia `truckbox-b1-core.zip`. Feche qualquer monitor que esteja usando a COM3.
2. Com alimentação e ligação USB já conferidas, mantenha o jumper BOOT/GPIO9–GND para a gravação. Execute na pasta extraída:

```bat
flash_core.cmd COM3
```

3. Após a gravação terminar com sucesso, desconecte o USB e desligue a fonte. Remova somente o jumper GPIO9–GND; preserve o resistor de pull-up do GPIO8. Religue alimentação e USB, execute `monitor.cmd COM3` e digite `setup` para obter os dados de acesso gerados na própria placa.

Leia `README.md`, `HARDWARE.md`, `OTA.md` e `TEST_RESULTS.md` dentro do pacote antes de prosseguir com Link, modem e armazenamento. Os defeitos de montagem/footprint e o nível de EN ainda precisam da correção física descrita no guia. O firmware não corrige essas ligações.

Não há senhas de Wi-Fi, tokens particulares de nuvem ou chaves privadas pré-configurados nos pacotes.

## Integridade

Consulte `SHA256SUMS.txt`.
