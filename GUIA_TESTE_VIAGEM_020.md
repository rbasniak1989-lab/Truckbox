# Teste de viagem — Next 0.2.0 e Core 0.2.24

**Estado da entrega:** APK 0.2.0 compilado, 88 testes Android/JVM/Compose aprovados, lint sem erros; candidata Core 0.2.24 compilada e testada em CI, ainda sem validação física completa. API `multimedia-next-v1` atualizada à v2 para horários compatíveis com Android 13. Um único vínculo proprietário, Financeiro/Jornada autorizados, válido até **17/11/2026 19h29 Brasília**. O proprietário confirmou a ativação real; ACK da política v1 registrado no banco. Mesmo APK/código; sem nova instalação.

## Uso após liberação da API

1. Instalar o APK **homolog**, pacote `br.com.truckbox.multimedia.next`; não remover Motorista nem multimídia antiga. Se houver conflito de assinatura entre APK local e GitHub, remover somente Next antes de instalar.
2. O Core precisa compartilhar a Starlink para a multimídia permanecer somente no AP dele. A **0.2.23 não faz esse compartilhamento**. A candidata **0.2.24** e seu roteiro estão no [PR 27](https://github.com/rbasniak1989-lab/Truckbox/pull/27). Revisar imagem/hash/placa e backup antes de instalar pelo mecanismo existente. O APK não faz OTA. Nunca apagar NVS/partições para atualizar rede.
3. Core STA na Starlink **2,4 GHz**; multimídia somente no Wi-Fi do Core. Renovar a conexão Wi-Fi/DHCP após atualização. Em Configurações escolher `192.168.4.1` e **Testar conexão agora**. Conferir sensores, origem/idade e acesso HTTPS público. NAPT anunciado não comprova Internet.
4. Em **Configurações → Acesso à Cloud**, colar o código privado e tocar **Ativar teste de viagem**. Sem e-mail/senha/cadastro nesta modalidade. A Cloud vincula o código ao **primeiro aparelho com chave RSA e Core real autorizado**, por no máximo 72 horas; o código não está no APK nem no Git. O vínculo não pode ser usado em outro aparelho/Core/veículo.
5. Financeiro e Jornada são autorizados remotamente para esse vínculo, conforme pedido do proprietário. Permissão não gera dados: enquanto não houver fonte verificada de valores/jornada/consumo, aparecem indisponíveis. Não inferir saldo de tanque, descanso ou responsabilidade de despesa.
6. **GPS vem exclusivamente do TruckBox Motorista → Cloud → multimídia.** O Motorista precisa coletar/enviar posições com suas permissões e conectividade usuais. A multimídia não usa GPS próprio ou do Core/Link como substituto. A última captura encontrada era de 30/10: conferir data/origem antes de considerá-la atual. Mapa/rota dependem de fornecedor/licença.

Instrumentos guardam o último número real com idade; não piscam para zero porque outro sensor respondeu. Sem valor anterior: Sem leitura. Velocidade/vazão/cálculos exigem amostras atuais. Ao cair Internet, sensores locais continuam e dados protegidos são removidos no erro/expiração. Reabrir o app restaura somente a credencial cifrada, conferindo vínculo novamente na Cloud.

Encerrar o acesso temporário revoga o código no servidor; nova ativação exige novo código. Expiração/revogação não é alterável por toggle local. Nada desta entrega comprova hardware/viagem real ou liberação comercial.

## Publicação e limites

Nova função **`multimedia-next-v1`**, sem substituir função existente; `verify_jwt=false` no gateway para aceitar código temporário não JWT. A função exige código secreto de 256 bits com expiração, prova RSA do aparelho, nonce, relógio e escopo de veículo; usa `service_role` somente no servidor para RPC de acesso privado. Esse segredo de servidor nunca vai ao APK. A RPC tem EXECUTE negado a PUBLIC/anon/authenticated. Sem prova/vínculo válido, nenhum dado.

O deploy foi inicialmente rejeitado pela revisão automática. O proprietário aprovou explicitamente essa configuração, e a nova função foi publicada pelo mesmo mecanismo autorizado. Uma credencial adicional de QA foi rejeitada; essa etapa foi removida. Há **um único vínculo**, ainda não ativado por automação, reservado ao aparelho do proprietário. Nenhum usuário Auth, API antiga ou dado operacional foi alterado.

Verificações hospedadas: sem autenticação → 401; código válido e assinatura RSA válida com Core errado → 403; RPC sem EXECUTE para anon/authenticated, liberada apenas ao servidor. A tentativa rejeitada com Core errado não vinculou a chave de teste. A ativação positiva do aparelho físico e o trajeto completo com Starlink/Motorista ainda não foram executados.

Fontes, migração, testes e plano de reversão em REVISAO_CORE_CLOUD_020.md e `backend-proposal/v1/cloud/field-test*`. O plano completo de contas permanece separado; a migração de 12 tabelas não foi aplicada.
