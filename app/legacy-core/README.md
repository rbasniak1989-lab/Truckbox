# Fontes antigas do Motorista que acessavam o Core

Este diretório não é um source set Android e não participa do APK. Os arquivos
foram movidos sem alterações para preservar a referência e permitir revisão.
Inclui os clientes HTTP/TCP locais e as telas antigas que não eram usadas pelo
`MainActivity` operacional da v0.6.4.

A v0.6.5 usa `app/src/main` para Cloud e GPS do celular. Para rollback, use o
commit base `06a9309d104367a49c88167d8f2b70bd7c1ffd1e`, com a assinatura original
e o backup dos dados locais. Não adicione esta pasta ao APK novo.
