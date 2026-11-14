package br.com.truckbox.multimedia.next.domain

data class CloudUiState(
    val configured: Boolean=false,
    val authenticated: Boolean=false,
    val busy: Boolean=false,
    val deviceId: String?=null,
    val message: String="Servidor Cloud ainda não configurado nesta compilação",
)
