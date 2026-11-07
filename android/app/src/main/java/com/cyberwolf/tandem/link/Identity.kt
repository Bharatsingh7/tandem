package com.cyberwolf.tandem.link

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.math.BigInteger
import java.nio.ByteBuffer
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.Certificate
import java.security.cert.X509Certificate
import java.util.Date
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.X509TrustManager
import javax.security.auth.x500.X500Principal

/**
 * The phone's TLS identity: an EC key in AndroidKeyStore with a self-signed cert.
 * Peers are authenticated by pinning certificate fingerprints, not by a CA.
 */
object Identity {
    private const val ALIAS = "tandem-link"

    private fun keyStore(): KeyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }

    fun ensure() {
        val ks = keyStore()
        if (ks.containsAlias(ALIAS)) return
        val kpg = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore")
        kpg.initialize(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                .setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_SHA384, KeyProperties.DIGEST_NONE)
                .setCertificateSubject(X500Principal("CN=Tandem Phone"))
                .setCertificateSerialNumber(BigInteger.valueOf(System.currentTimeMillis()))
                .setCertificateNotBefore(Date(System.currentTimeMillis() - 86_400_000L))
                .setCertificateNotAfter(Date(4_102_444_800_000L))
                .build()
        )
        kpg.generateKeyPair()
    }

    val fingerprint: String by lazy { fingerprint(keyStore().getCertificate(ALIAS)) }

    fun fingerprint(c: Certificate): String =
        MessageDigest.getInstance("SHA-256").digest(c.encoded).joinToString("") { "%02x".format(it) }

    /** Six digits both screens show during pairing; must match desktop/tandem/link.py:pair_code. */
    fun pairCode(phoneFp: String, pcFp: String): String {
        val d = MessageDigest.getInstance("SHA-256").digest((phoneFp + pcFp).toByteArray())
        val n = ByteBuffer.wrap(d, 0, 4).int.toLong() and 0xffffffffL
        return "%06d".format(n % 1_000_000)
    }

    fun sslContext(): SSLContext {
        val kmf = KeyManagerFactory.getInstance("X509")
        kmf.init(keyStore(), null)
        // Any client cert is accepted at the TLS layer; LinkServer then checks it against the pinned set.
        val acceptAll = object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
            override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
            override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
        }
        return SSLContext.getInstance("TLS").apply { init(kmf.keyManagers, arrayOf(acceptAll), null) }
    }
}
