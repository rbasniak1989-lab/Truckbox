# TruckBox Core Rev.B.1 — candidata 0.2.24.1 (MID 172 / eco)

**NÃO INSTALADA.** Branch experimental derivada das fontes congeladas da 0.2.24
(commit `a9cd6bddfa6c1d176a7f8f9a8b46855dcd7ab3aa`). Não altera o Link,
a Cloud, o formato da partição, a base de viagens ou as configurações existentes.

## Incidente observado (16/11/2026)
- Após rollback 0.2.26 → 0.2.24, a recepção passiva J1708 funcionou.
- Boots 920 e 921: `LOCAL_MID_CONFLICT`, 0 requisições; o bloqueio foi
  reproduzido sem TX do próprio Core no boot novo.
- Código 0.2.24 grava `tb_trip/jguard` na NVS ao ver quadro checksum-válido
  do MID local que não foi reconhecido como o eco pendente.
- **Não há quadro bruto da ocorrência original que prove que foi um eco**.
  Pode haver MID realmente ocupado. Esta candidata não desativa esse detector.

## Correções
1. Um quadro de eco já comprovado byte a byte pelo UART pode aguardar até
   2 segundos pela montagem da moldura. O mesmo eco só é consumido uma vez;
   qualquer quadro diferente ou posterior ainda passa pelo detector normal.
2. No painel local com senha administrativa, opção "Recuperar bloqueio MID
   antigo (sem transmitir)". Para habilitar, exige:
   - veículo realmente parado, com velocidade válida e recente;
   - autorização explícita; sem OTA, TX ativo ou requisições armadas;
   - bloqueio `jguard` íntegro e **originado em boot anterior**;
   - no boot atual não pode ter aparecido nenhum quadro checksum-válido
     vindo do MID bloqueado, nem qualquer TX local;
   - pelo menos 60 segundos, 3.000 quadros válidos e 3 janelas
     saudáveis de recepção passiva.
3. Antes de apagar **somente** `tb_trip/jguard`, salva o registro original
   na NVS `tb_trip/jghist`; faz commit e registra o evento na telemetria.
4. Polling fica persistentemente **DESATIVADO** depois da recuperação:
   exige segunda ação manual "Reabilitar polling automático persistente" e
   os intertravamentos normais da rede continuam valendo.
5. Se o MID de fato está presente na rede, não permite liberar o hold.
   Nessa situação investigar com NEXIQ/analisador externo. Nunca escolher
   outro MID ao acaso.

## Testes e publicação
GitHub Actions da branch compila ESP-IDF 5.5.2, executa os host tests
incluindo elegibilidade/rejeições de recuperação e consumo único de eco,
confere aplicação ESP32-C6 (`truckbox_core`) e versão `0.2.24.1`,
publica **imagem OTA TEST ONLY** e SHA256 apenas se tudo passar.

**Antes de instalar:** verificar sucesso do CI e SHA256 do arquivo; usar OTA
Core com caminhão estacionado, alimentação confiável, backup da configuração
e telemetria já sincronizada. Não usar erase_flash nem substituir bootloader,
partições ou firmware Link. Testar primeiro parado; retorno manual usando a
imagem 0.2.24 auditada que já foi preservada.

O ensaio de estabilidade em rodagem **não está validado**. Esta correção
atua na distinção de eco e na limpeza controlada do MID hold legado; não
afirma resolver as quedas RX_SILENCE da versão 0.2.26.
