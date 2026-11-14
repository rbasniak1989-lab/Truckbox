# Core 0.2.22 candidata — manutenção com um clique

Alteração incremental sobre 0.2.21, commit `1fd7f1b`, conforme as instruções
do operador: um botão, sem confirmação adicional e sem trava de velocidade
para a manutenção do armazenamento. Nenhuma ação foi executada no dispositivo.

## Uso

Enviar manualmente **`truckbox_core_0.2.22_ota.bin`** para o Core pelo painel OTA.
Depois do reinício, abrir a página novamente ou usar Ctrl+F5 para carregar a
interface nova. Entrar com a chave de administrador já cadastrada.

Em **Coleta e armazenamento**, clicar **Recriar armazenamento**. Esse clique
é a autorização: a página prepara e executa a operação internamente. Não há
frase, caixas de aceite, botão de preparo separado ou diálogo de confirmação.
**Baixar cópia** continua disponível como opção independente.

O botão não depende de velocidade, RPM, GPS, internet ou de diagnóstico completo.
O operador autorizou expressamente a perda dos arquivos locais e o uso em
movimento. Nenhuma declaração fictícia de veículo parado é enviada ou registrada.
Instalar a BIN, abrir a página e entrar como administrador não iniciam limpeza.

## Escopo preservado

- Somente `storage:/tbflash`, offset 0x620000 e tamanho 0x1e0000, como na 0.2.21.
  Formato de registros, CRC, ACK, reserva de sequências e partições não mudam.
- NVS, identidade, Wi-Fi, vínculo, viagens e combustível não são apagados. A
  marca de proteção dos arquivos descartados é reiniciada somente na operação.
- Permanecem autenticação, exclusão de OTA, validação da imagem confirmada,
  alvo/UID/boot, nonce de uso único, mutex, prova de escrita/sync/readback e pausa
  por falha. A frase/aceite do protocolo legado ficam internos ao clique explícito.
- Não há repetição automática se a resposta se perder. Duplo clique durante
  execução não dispara uma segunda operação. Sem tentativa de formatação no boot.
- Retirada a dependência de velocidade apenas do armazenamento/backup. Rotinas
  J1708, PIDs, MIDs, cadências, arbitragem, proteções e política persistente são
  idênticos à 0.2.21. Pausa de TX durante IO e retomada saudável seguem a proteção
  de exclusão já existente; recepção passiva é preservada.
- Não há OTA automática ou alterações em Cloud, Link, microSD, FRAM ou modem.

## Correções do painel

O painel anterior só habilitava manutenção quando o diagnóstico completo
continha `storage.maintenance.available=true`. Respostas parciais/OOM ou mutex
ocupado omitem esse campo e podiam bloquear os controles mesmo com o recurso
instalado. A interface nova usa login/função do módulo e deixa a validação ao
servidor. Busca identidade leve em `/api/health` antes de operar e mantém o
preparo/nonce inteiramente dentro do handler do clique.

Erros do endpoint de manutenção passam a ser JSON legível pelo painel, com
mensagem real de autorização, operação ocupada ou falha. Não ficam escondidos
como erro genérico de resposta HTML inválida.

## Verificação

- Build ESP-IDF 5.5.2/ESP32-C6/8 MB: PASS. Aplicação **1.568.496 bytes**, cabe no
  slot de 3.145.728. RAM estática **210.436 bytes**, igual à 0.2.21; sem novas filas.
- Suíte completa host, ASan/UBSan e 24 testes Python: PASS. Inclui FatFs real,
  falhas IO, preservação, transações, reboot, ACK e regressões J1708/J1939/trip.
- Store/handler: preparo e formatação sem condição de parada, mantendo todos
  os outros guards; falhas, nonce expirado/replay, IDs/ACK e fila: PASS.
- Backup: leitura limitada à partição, erros/OOM/mutex e sem dependência de
  velocidade: PASS.
- Chromium/página real: diagnóstico parcial, um clique sem formulário/modal,
  dupla ativação, identidade atual, preparo recusado/expirado, erro sem retry,
  backup, logout e isolamento Core/Link: PASS. Regressão geral do painel: PASS.
- Simulação offline de 2 horas com/sem UTC: 3.789 registros, 14 reinícios,
  zero drops por capacidade e sincronização sem duplicação: PASS.
- 39 arquivos de captura, requisições, configuração, rede, protocolos, viagens
  e partições conferidos byte a byte contra a 0.2.21.

Evidências em `test_evidence/v0222_*`. Não houve ensaio físico, medição de heap
ou execução desta manutenção em movimento no caminhão. Permissão funcional
não é comprovação de ausência de interferência elétrica; proteções TX não mudam.

## Retorno

Rollback de aplicação usa o processo OTA existente, com layout/NVS preservados.
Retornar à 0.2.21 traz de volta a interface e a trava de parada daquela versão.
Rollback não restaura arquivos eliminados. A OTA reinicia; dados apenas em RAM
na versão em campo não têm preservação garantida. Esse comportamento não mudou.
