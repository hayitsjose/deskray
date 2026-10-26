package io.github.hayitsjose.deskray.net

import java.io.File
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate

/** Pinned host certificates, keyed by the host's `uniqueid` so a new IP keeps the pairing. */
class HostStore(filesDir: File) {
    private val dir = File(filesDir, "hosts").apply { mkdirs() }

    fun pinnedCert(hostUniqueId: String): X509Certificate? {
        val f = file(hostUniqueId)
        if (!f.exists()) return null
        return f.inputStream().use {
            CertificateFactory.getInstance("X.509").generateCertificate(it) as X509Certificate
        }
    }

    fun pin(hostUniqueId: String, cert: X509Certificate) {
        val tmp = File(dir, "${safe(hostUniqueId)}.tmp")
        tmp.writeBytes(cert.encoded)
        check(tmp.renameTo(file(hostUniqueId))) { "could not save pinned certificate" }
    }

    fun forget(hostUniqueId: String) {
        file(hostUniqueId).delete()
    }

    private fun file(id: String) = File(dir, "${safe(id)}.crt")
    private fun safe(id: String) = id.replace(Regex("[^A-Za-z0-9_-]"), "_")
}
