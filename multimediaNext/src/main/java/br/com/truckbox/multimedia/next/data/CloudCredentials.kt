package br.com.truckbox.multimedia.next.data

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.nio.ByteBuffer
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.Signature
import java.util.Base64
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Só chave pública sai do Keystore; o identificador é derivado da chave, não de um ID livre Android. */
class AndroidDeviceSigner : DeviceSigner {
    private val store=KeyStore.getInstance("AndroidKeyStore").apply {load(null)}
    private val alias="truckbox.next.device.rsa.v1"
    init {
        if(!store.containsAlias(alias)) KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA,"AndroidKeyStore").apply {
            initialize(KeyGenParameterSpec.Builder(alias,KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                .setKeySize(2048).setDigests(KeyProperties.DIGEST_SHA256).setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1).build())
        }.generateKeyPair()
    }
    override val publicKeySpki: String get()=Base64.getEncoder().encodeToString(store.getCertificate(alias).publicKey.encoded)
    override val deviceId: String by lazy {
        val digest=MessageDigest.getInstance("SHA-256").digest(store.getCertificate(alias).publicKey.encoded)
        digest[6]=((digest[6].toInt() and 0x0f) or 0x80).toByte() // UUIDv8, derivado de SHA256 da chave pública.
        digest[8]=((digest[8].toInt() and 0x3f) or 0x80).toByte()
        val bytes=ByteBuffer.wrap(digest)
        UUID(bytes.long,bytes.long).toString()
    }
    override fun sign(bytes: ByteArray): ByteArray=Signature.getInstance("SHA256withRSA").run {
        initSign(store.getKey(alias,null) as java.security.PrivateKey);update(bytes);sign()
    }
}

data class StoredRefresh(val generation: Long,val token: String)
interface RefreshTokenStore {
    fun generation(): Long
    fun read(): StoredRefresh?
    fun invalidate(): Long
    fun writeIfCurrent(generation: Long,token: String): Boolean
}

/** Apenas refresh token cifrado. Não persiste access token, política, viagem, dinheiro ou jornada. */
class EncryptedRefreshTokenStore(context: Context,realm: String): RefreshTokenStore {
    private val preferences=context.getSharedPreferences("multimedia_next_cloud_secure_v1",Context.MODE_PRIVATE)
    private val aad=MessageDigest.getInstance("SHA-256").digest(realm.toByteArray(Charsets.UTF_8))
    private val keyStore=KeyStore.getInstance("AndroidKeyStore").apply {load(null)}
    private val alias="truckbox.next.refresh.aes.v1"
    private fun key(): SecretKey {
        if(!keyStore.containsAlias(alias)) KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder(alias,KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).setKeySize(256).build())
        }.generateKey()
        return keyStore.getKey(alias,null) as SecretKey
    }
    @Synchronized override fun generation()=preferences.getLong("generation",0)
    @Synchronized override fun invalidate(): Long {
        val next=Math.addExact(generation(),1)
        check(preferences.edit().putLong("generation",next).remove("refresh").commit())
        return next
    }
    @Synchronized override fun read(): StoredRefresh? {
        val encoded=preferences.getString("refresh",null) ?: return null
        return try {
            val data=Base64.getDecoder().decode(encoded);require(data.size in 29..8192)
            val cipher=Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE,key(),GCMParameterSpec(128,data.copyOfRange(0,12)));cipher.updateAAD(aad)
            StoredRefresh(generation(),String(cipher.doFinal(data.copyOfRange(12,data.size)),Charsets.UTF_8))
        } catch (_: Exception) {invalidate();null}
    }
    @Synchronized override fun writeIfCurrent(generation: Long,token: String): Boolean {
        if(this.generation()!=generation)return false
        require(token.length in 8..4096)
        val cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,key());cipher.updateAAD(aad)
        val encrypted=cipher.iv+cipher.doFinal(token.toByteArray(Charsets.UTF_8))
        return preferences.edit().putString("refresh",Base64.getEncoder().encodeToString(encrypted)).commit()
    }
}
