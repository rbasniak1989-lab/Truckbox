package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.domain.validCoreHost
import org.junit.Assert.*
import org.junit.Test

class CoreAddressTest {
    @Test fun acceptsOnlyCoreMdnsOrLiteralPrivateLanAddresses() {
        listOf("truckbox.local","192.168.4.1","192.168.1.250","10.2.3.4","172.16.1.1","172.31.255.254").forEach { assertTrue(it,validCoreHost(it)) }
        listOf("example.com","8.8.8.8","127.0.0.1","169.254.169.254","172.32.1.1","192.168.1.0","192.168.1.255","192.168.1.256","192.168.001.1","http://192.168.4.1","192.168.4.1:80","user@192.168.4.1","192.168.4.1/path","truckbox.local.evil").forEach { assertFalse(it,validCoreHost(it)) }
    }
}
