package io.github.hayitsjose.deskray.net

import android.util.Log
import java.io.ByteArrayInputStream
import java.security.MessageDigest
import java.security.SecureRandom
import java.security.Signature
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import javax.crypto.Cipher
import javax.crypto.spec.SecretKeySpec

/**
 * PIN pairing with a GameStream/Sunshine host. A port of moonlight-android's
 * PairingManager (GPL-3.0): both sides prove knowledge of the PIN through
 * AES-encrypted challenges, then exchange signed secrets so each pins the
 * other's certificate.
 */
class Pairing(private val http: NvHttp, private val identity: Identity) {
    enum class Result { PAIRED, PIN_WRONG, ALREADY_IN_PROGRESS, FAILED }

    /** The host certificate to pin, set once [pair] returns PAIRED. */
    var serverCert: X509Certificate? = null
        private set

    private val random = SecureRandom()

    fun pair(serverInfo: NvResponse, pin: String): Result {
        val major = serverInfo.require("appversion").substringBefore('.').toInt()
        val hash = if (major >= 7) "SHA-256" else "SHA-1"
        val hashLen = if (major >= 7) 32 else 20
        Log.i(TAG, "pairing with server generation $major ($hash)")

        val salt = randomBytes(16)
        val aesKey = sha(hash, salt + pin.toByteArray()).copyOf(16)

        // 1. Blocks until the user types the PIN on the host.
        val certResp = http.pairCommand(
            "phrase=getservercert&salt=${salt.toHex()}&clientcert=${identity.pemCertificate.toHex()}",
            NvHttp.NO_READ_TIMEOUT,
        )
        if (certResp["paired"] != "1") return Result.FAILED
        val plainCert = certResp["plaincert"]
        if (plainCert.isNullOrEmpty()) {
            // Another client is mid-pairing with this host.
            http.unpair()
            return Result.ALREADY_IN_PROGRESS
        }
        val cert = CertificateFactory.getInstance("X.509")
            .generateCertificate(ByteArrayInputStream(plainCert.hexToBytes())) as X509Certificate
        http.serverCert = cert

        // 2. Our challenge, encrypted with the PIN-derived key.
        val clientChallenge = randomBytes(16)
        val challengeResp = http.pairCommand("clientchallenge=${aes(true, clientChallenge, aesKey).toHex()}")
        if (challengeResp["paired"] != "1") return fail()
        val decrypted = aes(false, challengeResp.require("challengeresponse").hexToBytes(), aesKey)
        val serverResponse = decrypted.copyOfRange(0, hashLen)
        val serverChallenge = decrypted.copyOfRange(hashLen, hashLen + 16)

        // 3. Answer the host's challenge with a secret only we know so far.
        val clientSecret = randomBytes(16)
        val answer = sha(hash, serverChallenge + identity.certificate.signature + clientSecret)
        val secretResp = http.pairCommand("serverchallengeresp=${aes(true, answer, aesKey).toHex()}")
        if (secretResp["paired"] != "1") return fail()

        // 4. The host's signed secret: the signature proves it holds the cert's
        //    key; the hash proves it knew the PIN.
        val pairingSecret = secretResp.require("pairingsecret").hexToBytes()
        val serverSecret = pairingSecret.copyOfRange(0, 16)
        val serverSignature = pairingSecret.copyOfRange(16, pairingSecret.size)
        if (!verify(serverSecret, serverSignature, cert)) {
            Log.w(TAG, "host signature check failed: possible man in the middle")
            return fail()
        }
        if (!sha(hash, clientChallenge + cert.signature + serverSecret).contentEquals(serverResponse)) {
            Log.w(TAG, "host challenge hash mismatch: wrong PIN")
            http.unpair()
            return Result.PIN_WRONG
        }

        // 5. Our signed secret, then the first mutual-TLS request.
        val signed = clientSecret + sign(clientSecret)
        if (http.pairCommand("clientpairingsecret=${signed.toHex()}")["paired"] != "1") return fail()
        if (http.pairChallenge()["paired"] != "1") return fail()

        serverCert = cert
        return Result.PAIRED
    }

    private fun fail(): Result {
        http.unpair()
        return Result.FAILED
    }

    private fun randomBytes(n: Int) = ByteArray(n).also(random::nextBytes)

    private fun sha(algorithm: String, data: ByteArray): ByteArray =
        MessageDigest.getInstance(algorithm).digest(data)

    /** AES-128-ECB over zero-padded whole blocks, as the protocol expects. */
    private fun aes(encrypt: Boolean, data: ByteArray, key: ByteArray): ByteArray {
        val padded = data.copyOf((data.size + 15) and 15.inv())
        val cipher = Cipher.getInstance("AES/ECB/NoPadding")
        cipher.init(if (encrypt) Cipher.ENCRYPT_MODE else Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"))
        return cipher.doFinal(padded)
    }

    private fun sign(data: ByteArray): ByteArray =
        Signature.getInstance("SHA256withRSA").run {
            initSign(identity.privateKey)
            update(data)
            sign()
        }

    private fun verify(data: ByteArray, signature: ByteArray, cert: X509Certificate): Boolean =
        Signature.getInstance(if (cert.publicKey.algorithm == "EC") "SHA256withECDSA" else "SHA256withRSA").run {
            initVerify(cert.publicKey)
            update(data)
            verify(signature)
        }

    companion object {
        private const val TAG = "deskray"

        fun generatePin(): String {
            val r = SecureRandom()
            return (1..4).joinToString("") { r.nextInt(10).toString() }
        }
    }
}
