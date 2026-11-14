# 0.2.20 — candidata de correção OPEN/EACCES

- Usa atributos FAT reais para reconhecer arquivos somente leitura; mantém o arquivo e continua em novo segmento.
- Cria novos segmentos exclusivamente e mantém append sem truncamento nos arquivos próprios existentes.
- Preserva arquivos anteriores à atualização e arquivos somente leitura, inclusive após ACK. Mantém a reciclagem normal de novos segmentos somente com ACK confirmado.
- Separa releitura de recuperação com escrita durável; não conta montagem/scan como sucesso completo.
- Registra caminho, flags, FRESULT original, atributos FAT e resultados de recuperação; códigos não medidos ficam nulos.
- Limita falhas consecutivas, retém a fila/item em voo e oferece retry administrativo somente do armazenamento.
- Desabilita formatação automática também em partição vazia.
- Preserva código/configuração de J1708, requisições automáticas, contadores, Wi-Fi, Cloud e partições.
- Build e testes de software passaram. Causa específica da recusa na placa ainda depende do FRESULT/atributos que a0.2.19 não registrava; diretório cheio não é corrigido apagando arquivos.

Detalhes, evidências e limites: [FLASH_FIX_v0.2.20.md](FLASH_FIX_v0.2.20.md).
