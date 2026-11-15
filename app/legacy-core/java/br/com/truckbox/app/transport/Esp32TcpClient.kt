package br.com.truckbox.app.transport

import android.content.Context
import br.com.truckbox.app.preferences.TruckBoxPreferences
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.InetSocketAddress
import java.net.Socket
import kotlin.concurrent.thread

/**
 * TCP RAW do TruckBox Core.
 *
 * Rede principal V0.4: ESP32 e multimídia na mesma Starlink. O host preferido é
 * truckbox.local (mDNS). 192.168.4.1 continua como fallback quando a multimídia
 * estiver conectada diretamente ao AP TruckBox-V1.
 */
class Esp32TcpClient(
    context: Context,
    private val port: Int = 35000,
    private val onLine: (String) -> Unit,
    private val onStatus: (wifiBound: Boolean, tcpConnected: Boolean, error: String?) -> Unit,
) {
    private val binder = WifiNetworkBinder(context)
    private val prefs = TruckBoxPreferences(context)
    @Volatile private var running = false
    @Volatile private var socket: Socket? = null

    fun start() {
        if (running) return
        running = true
        thread(name = "TruckBox-Core-TCP", isDaemon = true) {
            while (running) {
                val wifiNetwork = try { binder.currentWifiNetwork() } catch (_: Throwable) { null }
                if (wifiNetwork == null) {
                    onStatus(false, false, "Wi-Fi não disponível")
                    sleepQuiet(1000)
                    continue
                }
                val wifiBound = true

                val hosts = linkedSetOf(
                    prefs.coreHost,
                    "truckbox.local",
                    "192.168.4.1",
                ).filter { it.isNotBlank() }

                var connectedThisRound = false
                var lastError: String? = null
                for (host in hosts) {
                    if (!running) break
                    try {
                        val s = binder.createWifiSocket(wifiNetwork)
                        socket = s
                        s.tcpNoDelay = true
                        s.keepAlive = true
                        s.connect(InetSocketAddress(host, port), 1800)
                        connectedThisRound = true
                        onStatus(true, true, null)
                        BufferedReader(InputStreamReader(s.getInputStream()), 32 * 1024).use { reader ->
                            while (running) {
                                val line = reader.readLine() ?: break
                                onLine(line)
                            }
                        }
                        if (running) onStatus(true, false, "Stream TCP encerrado")
                        break
                    } catch (t: Throwable) {
                        lastError = "$host: ${t.message ?: t.javaClass.simpleName}"
                    } finally {
                        try { socket?.close() } catch (_: Throwable) { }
                        socket = null
                    }
                }

                if (running && !connectedThisRound) {
                    onStatus(true, false, lastError ?: "TruckBox Core não encontrado")
                    sleepQuiet(1200)
                }
            }
        }
    }

    fun stop() {
        running = false
        try { socket?.close() } catch (_: Throwable) { }
        socket = null
        try { binder.unbind() } catch (_: Throwable) { }
    }

    private fun sleepQuiet(ms: Long) {
        try { Thread.sleep(ms) } catch (_: InterruptedException) { }
    }
}
