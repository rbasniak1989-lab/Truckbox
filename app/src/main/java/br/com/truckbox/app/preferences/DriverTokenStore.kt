package br.com.truckbox.app.preferences

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

interface DriverTokens {fun read(name: String): String?;fun write(name: String,value: String)}
class DriverTokenStore(context: Context): DriverTokens {
    private val prefs=context.getSharedPreferences("truckbox_tokens_v065",Context.MODE_PRIVATE)
    private val packageName=context.packageName
    private val alias="truckbox.driver.tokens.aes.v1"
    private fun key(): SecretKey {
        val store=KeyStore.getInstance("AndroidKeyStore").apply {load(null)}
        if(!store.containsAlias(alias))KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder(alias,KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).setKeySize(256).build())
        }.generateKey()
        return store.getKey(alias,null) as SecretKey
    }
    @Synchronized override fun read(name: String): String? {
        val encoded=prefs.getString(name,null) ?: return null
        val bytes=Base64.getDecoder().decode(encoded);require(bytes.size in 28..8192)
        val c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.DECRYPT_MODE,key(),GCMParameterSpec(128,bytes.copyOfRange(0,12)))
        c.updateAAD((packageName+":"+name).toByteArray(Charsets.UTF_8))
        return String(c.doFinal(bytes.copyOfRange(12,bytes.size)),Charsets.UTF_8)
    }
    @Synchronized override fun write(name: String,value: String) {
        require(value.length<=4096)
        val c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.ENCRYPT_MODE,key())
        c.updateAAD((packageName+":"+name).toByteArray(Charsets.UTF_8))
        check(prefs.edit().putString(name,Base64.getEncoder().encodeToString(c.iv+c.doFinal(value.toByteArray(Charsets.UTF_8)))).commit())
    }
}
