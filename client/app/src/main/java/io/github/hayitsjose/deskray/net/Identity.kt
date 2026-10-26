package io.github.hayitsjose.deskray.net

import android.util.Base64
import org.bouncycastle.asn1.x500.X500Name
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder
import java.io.File
import java.math.BigInteger
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.SecureRandom
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.spec.PKCS8EncodedKeySpec
import java.util.Calendar
import java.util.Date

/**
 * This client's long-lived identity: an RSA key, a self-signed certificate the
 * host pins during pairing, and the `uniqueid` sent with every request.
 * Created once and kept in app storage; losing it means re-pairing every host.
 */
class Identity private constructor(
    val uniqueId: String,
    val privateKey: PrivateKey,
    val certificate: X509Certificate,
) {
    val pemCertificate: ByteArray by lazy {
        val b64 = Base64.encodeToString(certificate.encoded, Base64.NO_WRAP).chunked(64).joinToString("\n")
        "-----BEGIN CERTIFICATE-----\n$b64\n-----END CERTIFICATE-----\n".toByteArray()
    }

    companion object {
        fun loadOrCreate(filesDir: File): Identity {
            val dir = File(filesDir, "identity").apply { mkdirs() }
            val keyFile = File(dir, "client.key")
            val certFile = File(dir, "client.crt")
            val idFile = File(dir, "uniqueid")
            if (keyFile.exists() && certFile.exists() && idFile.exists()) {
                val key = KeyFactory.getInstance("RSA").generatePrivate(PKCS8EncodedKeySpec(keyFile.readBytes()))
                val cert = certFile.inputStream().use {
                    CertificateFactory.getInstance("X.509").generateCertificate(it) as X509Certificate
                }
                return Identity(idFile.readText().trim(), key, cert)
            }
            return create(keyFile, certFile, idFile)
        }

        private fun create(keyFile: File, certFile: File, idFile: File): Identity {
            val random = SecureRandom()
            val keyPair = KeyPairGenerator.getInstance("RSA").apply { initialize(2048, random) }.generateKeyPair()
            val name = X500Name("CN=deskray")
            val notAfter = Calendar.getInstance().apply { add(Calendar.YEAR, 20) }.time
            val holder = JcaX509v3CertificateBuilder(name, BigInteger(64, random).abs(), Date(), notAfter, name, keyPair.public)
                .build(JcaContentSignerBuilder("SHA256withRSA").build(keyPair.private))
            val cert = JcaX509CertificateConverter().getCertificate(holder)
            val uniqueId = ByteArray(8).also(random::nextBytes).toHex()

            // Write the id last: loadOrCreate only trusts a complete set.
            keyFile.writeBytes(keyPair.private.encoded)
            certFile.writeBytes(cert.encoded)
            idFile.writeText(uniqueId)
            return Identity(uniqueId, keyPair.private, cert)
        }
    }
}

private val HEX = "0123456789ABCDEF".toCharArray()

fun ByteArray.toHex(): String {
    val out = CharArray(size * 2)
    forEachIndexed { i, b ->
        val v = b.toInt() and 0xFF
        out[i * 2] = HEX[v ushr 4]
        out[i * 2 + 1] = HEX[v and 0x0F]
    }
    return String(out)
}

fun String.hexToBytes(): ByteArray {
    require(length % 2 == 0) { "odd-length hex string" }
    return ByteArray(length / 2) { i -> substring(i * 2, i * 2 + 2).toInt(16).toByte() }
}
