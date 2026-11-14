package br.com.truckbox.multimedia.next

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import br.com.truckbox.multimedia.next.data.WifiCoreConnectionFactory
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowNetwork
import org.robolectric.shadows.ShadowNetworkInfo
import org.robolectric.shadows.ShadowNetworkCapabilities

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[35])
class WifiCoreNetworkTest {
    @Test fun wifiWithoutInternetIsSelectedWhileCellularRemainsDefault() {
        val context=RuntimeEnvironment.getApplication()
        val cm=context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val shadow=shadowOf(cm)
        shadow.clearAllNetworks()
        val cell=ShadowNetwork.newInstance(101)
        val wifi=ShadowNetwork.newInstance(102)
        val cellInfo=ShadowNetworkInfo.newInstance(android.net.NetworkInfo.DetailedState.CONNECTED,ConnectivityManager.TYPE_MOBILE,0,true,true)
        val wifiInfo=ShadowNetworkInfo.newInstance(android.net.NetworkInfo.DetailedState.CONNECTED,ConnectivityManager.TYPE_WIFI,0,true,true)
        shadow.addNetwork(cell,cellInfo)
        val cellCaps=ShadowNetworkCapabilities.newInstance()
        shadowOf(cellCaps).addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
        shadowOf(cellCaps).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
        shadowOf(cellCaps).addCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
        shadow.setNetworkCapabilities(cell,cellCaps)
        shadow.setActiveNetworkInfo(cellInfo)
        shadow.addNetwork(wifi,wifiInfo)
        val wifiCaps=ShadowNetworkCapabilities.newInstance()
        shadowOf(wifiCaps).addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
        shadow.setNetworkCapabilities(wifi,wifiCaps)
        val defaultBefore=cm.activeNetwork
        assertEquals(wifi,WifiCoreConnectionFactory(context).currentWifi())
        assertEquals(defaultBefore,cm.activeNetwork)
        assertNull(cm.boundNetworkForProcess)
    }

    @Test fun cellularOrVpnIsNotAcceptedAsCoreWifi() {
        val context=RuntimeEnvironment.getApplication()
        val cm=context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val shadow=shadowOf(cm)
        shadow.clearAllNetworks()
        val vpn=ShadowNetwork.newInstance(103)
        shadow.addNetwork(vpn,ShadowNetworkInfo.newInstance(android.net.NetworkInfo.DetailedState.CONNECTED,ConnectivityManager.TYPE_WIFI,0,true,true))
        val caps=ShadowNetworkCapabilities.newInstance()
        shadowOf(caps).addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
        shadowOf(caps).addTransportType(NetworkCapabilities.TRANSPORT_VPN)
        shadow.setNetworkCapabilities(vpn,caps)
        assertNull(WifiCoreConnectionFactory(context).currentWifi())
        shadow.clearAllNetworks()
        assertNull(WifiCoreConnectionFactory(context).currentWifi())
    }
}
