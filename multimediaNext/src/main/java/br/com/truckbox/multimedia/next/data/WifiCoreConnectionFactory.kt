package br.com.truckbox.multimedia.next.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import br.com.truckbox.multimedia.next.domain.validCoreHost
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

fun interface CoreConnectionFactory {
    fun open(url: URL): HttpURLConnection
}

class CoreWifiUnavailable : IOException("Core Wi-Fi unavailable")

/** O Wi-Fi local pode não ter internet. Não exige VALIDATED/INTERNET e não altera a rota global. */
class WifiCoreConnectionFactory(context: Context) : CoreConnectionFactory {
    private val manager = context.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    internal fun currentWifi(): Network? = manager.allNetworks.firstOrNull { network ->
        manager.getNetworkCapabilities(network)?.let {
            it.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) && !it.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
        } == true
    }

    override fun open(url: URL): HttpURLConnection {
        require(url.protocol == "http" && validCoreHost(url.host) && url.userInfo==null && url.port==-1)
        return (currentWifi() ?: throw CoreWifiUnavailable()).openConnection(url) as HttpURLConnection
    }
}
