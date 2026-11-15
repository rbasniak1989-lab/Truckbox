package br.com.truckbox.app.gps

import br.com.truckbox.app.cloud.DriverCloudException
import br.com.truckbox.app.gateway.GatewayCloudClient
import br.com.truckbox.app.preferences.DriverCredentials

/** Sends a recent fix first, then history. Never deletes without a matching Cloud ACK. */
class DriverGpsSync(private val outbox: DriverGpsOutbox, private val cloud: GatewayCloudClient) {
    fun drain(credentials: DriverCredentials, keepSending: () -> Boolean, accepted: (QueuedGps) -> Unit = {}): Int {
        require(credentials.configured)
        val start = android.os.SystemClock.elapsedRealtime()
        var sent = 0
        for (i in 0 until 8) {
            if (!keepSending()) break
            val item = outbox.next(credentials.scope, newest = i == 0) ?: break
            if (!cloud.uploadGps(item.sample, credentials)) throw DriverCloudException(502)
            check(outbox.acknowledge(item))
            sent++
            accepted(item)
            if (android.os.SystemClock.elapsedRealtime() - start > 5000) break
        }
        return sent
    }
}
