# Compilação ESP-IDF 5.5.2

Projeto extraído da release funcional v0.1.0-b1; alvo ESP32-C6, flash 8 MB.
As dependências de componentes permanecem fixadas em `dependencies.lock`
(`mdns` 1.8.2 e `esp_modem` 1.4.0).

## Ferramentas usadas no ambiente de compilação

| Ferramenta | Versão |
| --- | --- |
| ESP-IDF | v5.5.2, commit `30aaf64524299d3bde422ca9a2848090d1bc5d0f` |
| Compilador | riscv32-esp-elf GCC 14.2.0, esp-14.2.0_20251107 |
| Python | 3.12.14 |
| CMake | 3.30.2 |
| Ninja | 1.13.2 |

O ESP-IDF instala os pacotes Python segundo seu arquivo de restrições da
linha 5.5. O build pode baixar os componentes fixados no lock; acesso à
internet é necessário na primeira preparação.

## Linux / macOS

Instale as dependências do ESP-IDF para seu sistema, incluindo Git,
Python com suporte a venv, CMake e Ninja. Em um diretório de ferramentas
gravável:

```sh
git clone --branch v5.5.2 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git esp-idf-v5.5.2
cd esp-idf-v5.5.2
# Opcional: indique um diretório gravável para as ferramentas.
export IDF_TOOLS_PATH=/caminho/gravavel/idf-tools
./install.sh esp32c6
. ./export.sh
cd /caminho/do/projeto/firmware/espidf
./tools/build_v020.sh all
```

O script exige `ESP-IDF v5.5.2` e gera `build-core/` e `build-link/`.
Pode-se usar `core` ou `link` em vez de `all`. Ele não executa flash.
Para diretórios pessoais somente leitura, configure também
`IDF_COMPONENT_CACHE_PATH`, `XDG_CACHE_HOME` e `CCACHE_DIR` para diretórios
graváveis antes de chamar `idf.py`.

## Windows

Instale o ESP-IDF **5.5.2** pelo instalador oficial da Espressif e abra o
terminal ESP-IDF correspondente. Execute na pasta do projeto:

```bat
idf.py --version
idf.py -B build-core -DTRUCKBOX_ROLE=core build
idf.py -B build-link -DTRUCKBOX_ROLE=link build
```

Não reutilize uma pasta de build de outra versão do ESP-IDF ou de outro
papel Core/Link. As imagens de aplicação, bootloader, tabela de partições
e dados OTA ficam na pasta de build de cada papel; use os offsets
registrados em `flasher_args.json`. A geração do pacote utiliza
`python tools/build_release.py` depois de ambos os builds concluírem.

Os procedimentos acima compilam e empacotam. A gravação deve ser uma
ação manual e separada, seguindo as instruções de atualização e recuperação.
