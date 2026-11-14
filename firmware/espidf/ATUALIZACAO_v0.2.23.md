# Atualização e rollback — Core Rev.B.1 0.2.23 candidata

Esta entrega contém somente nova aplicação do **Core**. O Link instalado permanece na versão atual. Nenhuma OTA foi aplicada pelo desenvolvimento. O pacote inclui fonte, símbolos ELF/map, manifesto/hashes, script Windows e arquivos de recuperação; as versões antigas continuam preservadas.

## Antes da aprovação

Leia `RELATORIO_v0.2.23.md`, especialmente a dependência da Cloud e os limites da evidência elétrica. O contrato financeiro da Cloud ainda exige revisão/implantação separada: a aplicação Core já calcula o total, mas a Cloud atual pode sobrescrevê-lo em suas próprias projeções. Não executar os scripts SQL contra produção como parte da OTA.

## OTA manual, quando autorizada

1. Use conexão estável ao Core, abra seu painel e autentique como administrador.
2. Na atualização OTA selecione **somente** `truckbox_core_0.2.23_ota.bin`, verificando o SHA256 em `manifest.json`/`SHA256SUMS.txt`.
3. Não enviar ZIP, fonte, bootloader, partições, NVS ou imagem de armazenamento no formulário OTA. A tabela de partições permanece igual.
4. Após o boot, `/api/health` e `/api/state` devem informar versão0.2.23, UID esperado, memória e identificação do boot. Use `/api/state?compact=1` se necessário; estatísticas são paginadas.
5. A política desejada é migrada uma vez para habilitada. O polling aguarda interface e rede saudável; não exige o botão de ensaio. Confira `automatic.enabled`, `persisted`, `state`, `reason`, timestamps efetivos UART/resposta e contadores novos. `RUNNING` não substitui a conferência de progresso.
6. Um conflito independente ou estado de segurança inválido continua bloqueando TX. Wi-Fi/Android não limpam esse bloqueio. Não usar mudança aleatória de MID para contorná-lo.

O procedimento de gravação é uma ação separada da aprovação deste pacote; execute a atualização em uma parada, com alimentação estável. Isso não altera a autorização já existente de manutenção de armazenamento em movimento. Não é necessário formatar a flash para instalar a candidata.

## Rollback

O bootloader e os dois slots OTA existentes permanecem intactos. Se o boot de teste falhar nos critérios já existentes do firmware, o rollback automático continua disponível. Para retorno deliberado, faça OTA manual com a aplicação **Core0.2.22** preservada em `firmware/releases/v0.2.22-simple-maintenance/`, usando o mesmo painel e conferindo seu manifesto. Não gravar partições ou apagar NVS.

O checkpoint v4 é adicional. A candidata também mantém as chaves `trip3a/b` e `trip` para a versão anterior conservar os saldos. Ao voltar novamente à candidata, ela detecta checkpoint legado avançado e importa seu saldo; referências antigas incompatíveis não são ressuscitadas. O firmware0.2.22 continua contendo os defeitos descritos no relatório, inclusive a possibilidade de gravar polling OFF após um conflito; seu retorno não corrige essas falhas.

Se a aplicação não servir HTTP, utilize o script Windows e os arquivos do pacote somente com acesso físico autorizado. As ferramentas validam papel/chip/flash/hashes e não incluem partições NVS/armazenamento na lista de gravação. Não executar `erase_flash`. O acesso ao dump retido é somente leitura e separado do volume de telemetria.

## Reprodução do software

```sh
. /caminho/esp-idf-v5.5.2/export.sh
bash firmware/espidf/tools/build_v020.sh core
sh firmware/espidf/tests/run_all.sh
```

O ZIP de fontes mantém os diretórios irmãos `firmware/espidf`, `firmware/j1708_17pid` e `firmware/j1939`. Consulte `SOURCE_PROVENANCE.json` para o commit exato, configuração e versão de toolchain usados. Não substitua as dependências travadas nem a partição instalada para reproduzir esta entrega.
