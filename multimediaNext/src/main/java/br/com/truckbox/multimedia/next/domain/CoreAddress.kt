package br.com.truckbox.multimedia.next.domain

/** Host sem porta/esquema/credenciais. HTTP somente para o Core na LAN privada. */
fun validCoreHost(host: String): Boolean {
    if(host=="truckbox.local") return true
    val parts=host.split('.')
    if(parts.size!=4 || parts.any { it.isEmpty() || it.length>3 || it.any { c->c !in '0'..'9' } || (it.length>1 && it.startsWith('0')) }) return false
    val ip=parts.map { it.toIntOrNull() ?: return false }
    if(ip.any {it !in 0..255} || ip.last() !in 1..254) return false
    return ip[0]==10 || (ip[0]==172 && ip[1] in 16..31) || (ip[0]==192 && ip[1]==168)
}
