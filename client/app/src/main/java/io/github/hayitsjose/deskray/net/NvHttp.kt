package io.github.hayitsjose.deskray.net

import android.util.Xml
import org.xmlpull.v1.XmlPullParser
import java.io.IOException
import java.io.StringReader
import java.net.HttpURLConnection
import java.net.Socket
import java.net.URL
import java.security.Principal
import java.security.PrivateKey
import java.security.SecureRandom
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.UUID
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.X509KeyManager
import javax.net.ssl.X509TrustManager

class HostException(message: String) : IOException(message)

/** A parsed host response: the first text of each tag, plus repeated <App> entries. */
class NvResponse(val raw: String) {
    private val fields = HashMap<String, String>()
    val apps = ArrayList<Map<String, String>>()

    init {
        val p = Xml.newPullParser().apply { setInput(StringReader(raw)) }
        var app: HashMap<String, String>? = null
        var tag: String? = null
        while (p.next() != XmlPullParser.END_DOCUMENT) {
            when (p.eventType) {
                XmlPullParser.START_TAG -> {
                    tag = p.name
                    if (tag == "root") {
                        val status = p.getAttributeValue(null, "status_code")
                        if (status != null && status != "200") {
                            throw HostException("host returned $status: ${p.getAttributeValue(null, "status_message")}")
                        }
                    }
                    if (tag == "App") app = HashMap()
                }
                XmlPullParser.TEXT -> {
                    val t = tag ?: continue
                    (app ?: fields).putIfAbsent(t, p.text)
                }
                XmlPullParser.END_TAG -> {
                    if (p.name == "App") app?.let { apps.add(it) }.also { app = null }
                    tag = null
                }
            }
        }
    }

    operator fun get(tag: String): String? = fields[tag]
    fun require(tag: String): String = fields[tag] ?: throw HostException("response is missing <$tag>")
}

/**
 * The GameStream-style HTTP(S) API that Sunshine serves: plain HTTP on 47989
 * for server info and pairing, mutual TLS on 47984 once paired.
 */
class NvHttp(address: String, private val identity: Identity) {
    private val host = if (':' in address) "[$address]" else address
    private val httpBase = "http://$host:47989"
    private var httpsPort = 47984

    /** The host certificate we pinned at pairing; HTTPS is refused without it. */
    var serverCert: X509Certificate? = null

    fun serverInfoHttp(): NvResponse =
        NvResponse(get(httpBase, "serverinfo", "", READ_TIMEOUT_MS)).also { r ->
            r["HttpsPort"]?.toIntOrNull()?.let { httpsPort = it }
        }

    fun serverInfoHttps(): NvResponse = NvResponse(get(httpsBase(), "serverinfo", "", READ_TIMEOUT_MS))

    fun pairCommand(args: String, readTimeoutMs: Int = READ_TIMEOUT_MS): NvResponse =
        NvResponse(get(httpBase, "pair", "devicename=deskray&updateState=1&$args", readTimeoutMs))

    fun pairChallenge(): NvResponse =
        NvResponse(get(httpsBase(), "pair", "devicename=deskray&updateState=1&phrase=pairchallenge", READ_TIMEOUT_MS))

    fun unpair() {
        runCatching { get(httpBase, "unpair", "", READ_TIMEOUT_MS) }
    }

    fun https(path: String, query: String, readTimeoutMs: Int = READ_TIMEOUT_MS): NvResponse =
        NvResponse(get(httpsBase(), path, query, readTimeoutMs))

    private fun httpsBase() = "https://$host:$httpsPort"

    private fun get(base: String, path: String, query: String, readTimeoutMs: Int): String {
        val sep = if (query.isEmpty()) "" else "&"
        val url = URL("$base/$path?$query${sep}uniqueid=${identity.uniqueId}&uuid=${UUID.randomUUID()}")
        val conn = url.openConnection() as HttpURLConnection
        if (conn is HttpsURLConnection) {
            // A fresh context per request: a resumed TLS session skips the client
            // certificate, and Sunshine answers that with an internal_error alert.
            conn.sslSocketFactory = buildSslFactory()
            conn.setHostnameVerifier { _, _ -> true }  // identity is the pinned cert, not the name
        }
        conn.connectTimeout = CONNECT_TIMEOUT_MS
        conn.readTimeout = readTimeoutMs
        // Sunshine serves HTTPS on one thread and blocks on an idle keep-alive
        // socket, so a pooled connection starves every later request.
        conn.setRequestProperty("Connection", "close")
        try {
            val code = conn.responseCode
            val body = (if (code in 200..299) conn.inputStream else conn.errorStream)
                ?.bufferedReader()?.use { it.readText() } ?: ""
            if (code !in 200..299) throw HostException("HTTP $code from /$path")
            return body
        } finally {
            conn.disconnect()
        }
    }

    private fun buildSslFactory(): SSLSocketFactory {
        val pinned = serverCert ?: throw HostException("not paired: no pinned host certificate")
        // Always offer our client cert: the host's CertificateRequest names no CA
        // we'd match, so the default key manager would send nothing.
        val keyManager = object : X509KeyManager {
            override fun getClientAliases(keyType: String?, issuers: Array<out Principal>?) = arrayOf(ALIAS)
            override fun chooseClientAlias(keyType: Array<out String>?, issuers: Array<out Principal>?, socket: Socket?) = ALIAS
            override fun getServerAliases(keyType: String?, issuers: Array<out Principal>?) = null
            override fun chooseServerAlias(keyType: String?, issuers: Array<out Principal>?, socket: Socket?) = null
            override fun getCertificateChain(alias: String?) = arrayOf(identity.certificate)
            override fun getPrivateKey(alias: String?): PrivateKey = identity.privateKey
        }
        val trustManager = object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) =
                throw CertificateException("client mode only")
            override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {
                if (chain.isNullOrEmpty() || !chain[0].encoded.contentEquals(pinned.encoded)) {
                    throw CertificateException("host certificate does not match the one pinned at pairing")
                }
            }
            override fun getAcceptedIssuers() = arrayOf<X509Certificate>()
        }
        return SSLContext.getInstance("TLS").apply {
            init(arrayOf(keyManager), arrayOf(trustManager), SecureRandom())
        }.socketFactory
    }

    companion object {
        private const val ALIAS = "deskray-client"
        const val CONNECT_TIMEOUT_MS = 5_000
        const val READ_TIMEOUT_MS = 10_000
        const val NO_READ_TIMEOUT = 0
    }
}
