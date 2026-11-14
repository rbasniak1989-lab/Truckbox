# Core Rev.B.1 0.2.22 — candidata, manutenção com um clique

[Aplicação OTA](truckbox_core_0.2.22_ota.bin) · [ZIP completo](Truckbox_Core_B1_0.2.22_MANUTENCAO_SIMPLES.zip)

[Guia e testes](../../espidf/MAINTENANCE_v0.2.22.md) · [Diff](SOURCE_CHANGES.diff) · [Verificação](PACKAGE_VERIFICATION.json)

Depois da OTA manual, abra a página novamente/Ctrl+F5 e entre como administrador.
Clique **Recriar armazenamento**. Sem frase, caixas ou diálogo, e sem trava de
velocidade/RPM para esta operação. O clique prepara e executa internamente.
Instalação e login não iniciam a limpeza. Baixar cópia permanece opcional.

Preserva NVS, Wi-Fi, identidade, vínculo, viagens, combustível e partições.
Limite de storage, autenticação, exclusão OTA, nonce/UID/boot e prova de escrita
permanecem. Proteções/rotinas de transmissão J1708 não mudam.
Nenhuma OTA ou limpeza executada pelo agente; avaliação física pendente.

Build ESP-IDF 5.5.2/C6/8 MB, suíte host e navegador: PASS.
Aplicação: 1.568.496 bytes. RAM estática: igual à 0.2.21. Sem novas filas/tarefas.
Fontes compilados identificados em SOURCE_PROVENANCE.json.

SHA-256 OTA: `d962279a5545fea0413d52951021d10c26a08d356bd72f054e932a336b594432`.
SHA-256 ZIP: `72fdf490f7462583d0f4619c0788f0a1f8f8e80cb46f7dc121095ae3991fc5a1`.
SHA256SUMS refere-se ao conteúdo completo extraído do ZIP.

Rollback de aplicação não restaura arquivos eliminados. Arquivos apenas em RAM
não têm preservação garantida durante a OTA; contadores já salvos na NVS são mantidos.
