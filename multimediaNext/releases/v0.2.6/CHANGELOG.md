# TruckBox Multimídia 0.2.6

`versionCode` 9 · `br.com.truckbox.multimedia.next.gps.consumo.tempo`

- Leitura do traçado e dos pedágios HERE da viagem vinculada pelo canal `multimedia-trip-homolog-v1`.
- Evolução do consumo por distância com amostras locais do Core alinhadas às janelas integradas do PID 183. A aba Viagem mostra os totais disponíveis na Cloud. O gráfico retroativo depende dos intervalos que a API da Multimídia disponibilizar.
- Compatibilidade da fila local de GPS com Android 8.

## Validação

O certificado de assinatura e o pacote coincidem com os APKs arquivados da 0.2.4-tempo-real e da 0.2.5-inteligencia. A instalação por cima foi testada em um emulador Android 8, na sequência 0.2.4 → 0.2.5 → 0.2.6. O teste conferiu a leitura dos tokens criptografados de GPS e Cloud, a chave do Android Keystore, o identificador da instalação, a fila GPS e as preferências. A abertura da 0.2.6 e a navegação entre os instrumentos e a aba Viagem também passaram.

Os 117 testes automatizados da Multimídia passaram. O lint concluiu com zero erros e 15 avisos.

O teste usou dados fictícios em um aparelho isolado. As alterações abrangem a Multimídia.
