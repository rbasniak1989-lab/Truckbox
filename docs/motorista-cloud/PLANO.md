# Incrementos revisáveis

1. **Auditar o acoplamento e os contratos existentes.** Evidências e limitações
   em `AUDITORIA.md`; consultar backend sem alterar schema/firmware.
2. **Isolar o aplicativo.** `cloudTest` com applicationId
   `br.com.truckbox.driver.cloud`, mantendo a instalação anterior; destinos
   HTTPS fixos e clientes Core fora do APK.
3. **GPS real e fila durável.** Validar captura, preservar horário/identidade,
   SQLite, confirmação exata, reenvio limitado e pausa explícita. Testar queda
   de rede, duplicação, reinício do processo e outro equipamento.
4. **Operações Cloud.** Uma fila ordenada de ações tipadas e imutáveis; confirmar
   registro/caminhão; preservar alterações locais; referências sem leitura
   permanecem nulas. Testar início/fim/próximo início offline.
5. **Interface e configuração.** Status separados para GPS e operações, código
   privado do celular, proteção Keystore, permissões de localização e controle
   de início/pausa. Capturar telas nativas em 320 e 412 dp.
6. **Entrega compilada.** Testes JVM/Compose, lint, APK de homologação e APK de
   testes instrumentados, verificação do APK, guia e changelog. Executar
   testes instrumentados somente quando houver Android físico/emulador.
7. **Vínculo e campo.** Credencial única do celular para o mesmo caminhão do
   teste autorizado; não criar conta/credencial adicional de QA. Verificar
   snapshot real sem escrever viagens ou GPS artificiais. Depois confirmar
   ponta a ponta com localização real do celular.

Não executar migrações destrutivas, merge ou atualização de firmware como parte
destes incrementos. Novas APIs para histórico de contadores, múltiplas contas e
expiração devem vir em proposta separada, reversível e revisada.
