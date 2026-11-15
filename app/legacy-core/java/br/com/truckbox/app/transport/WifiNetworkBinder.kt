package br.com.truckbox.app.transport

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import java.net.HttpURLConnection
import java.net.Socket
import java.net.URL

/**
 * Seleciona a rede Wi-Fi somente para conexoes LOCAIS com o TruckBox Core.
 *
 * IMPORTANTE: nunca usa bindProcessToNetwork(). A Cloud deve continuar livre para
 * usar a rota default do Android (TruckBox-V1 com NAPT, celular, Ethernet, etc.).
 */
class WifiNetworkBinder(context: Context) {
    private val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    fun currentWifiNetwork(): Network? = cm.allNetworks.firstOrNull { network ->
        cm.getNetworkCapabilities(network)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
    }

    fun hasWifi(): Boolean = currentWifiNetwork() != null

    fun createWifiSocket(network: Network = currentWifiNetwork() ?: throw IllegalStateException("Wi-Fi nao disponivel")): Socket =
        network.socketFactory.createSocket()

    fun openWifiHttp(url: URL, network: Network = currentWifiNetwork() ?: throw IllegalStateException("Wi-Fi nao disponivel")): HttpURLConnection =
        network.openConnection(url) as HttpURLConnection

    // Compatibilidade com chamadas antigas. Nao prende mais o processo inteiro ao Wi-Fi.
    fun bindIfAvailable(): Boolean = hasWifi()

    fun unbind() {
        // Intencionalmente vazio: desde v0.5.2 o processo nunca e globalmente bindado.
    }
}
