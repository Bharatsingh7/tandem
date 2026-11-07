package com.cyberwolf.tandem.link

import android.os.Build
import android.util.Log
import org.json.JSONObject
import java.net.InetSocketAddress
import java.net.ServerSocket
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLSocket

/** TLS server on [Link.PORT]. Reached directly over Wi-Fi, or via `adb forward` over USB. */
class LinkServer {
    @Volatile private var server: ServerSocket? = null

    fun start() {
        if (server != null) return
        val ss = Identity.sslContext().serverSocketFactory.createServerSocket() as SSLServerSocket
        ss.reuseAddress = true
        ss.needClientAuth = true
        ss.enabledProtocols = arrayOf("TLSv1.3")
        ss.bind(InetSocketAddress(Link.PORT))
        server = ss
        Link.setRunning(true)
        Thread({ acceptLoop(ss) }, "tandem-accept").start()
    }

    fun stop() {
        server?.close()
        server = null
        Link.setRunning(false)
        Link.session?.close()
    }

    private fun acceptLoop(ss: ServerSocket) {
        while (!ss.isClosed) {
            val sock = try { ss.accept() as SSLSocket } catch (e: Exception) { break }
            // Only loopback (USB via adb) and local networks (Wi-Fi, hotspot): never the open internet,
            // which mobile-data IPv6 could otherwise expose us to.
            val addr = sock.inetAddress
            if (!(addr.isLoopbackAddress || addr.isSiteLocalAddress || addr.isLinkLocalAddress ||
                  (addr is java.net.Inet6Address && (addr.address[0].toInt() and 0xfe) == 0xfc))) {
                Log.w(Session.TAG, "refused connection from public address")
                runCatching { sock.close() }
                continue
            }
            if (!preAuth.tryAcquire()) { runCatching { sock.close() }; continue }
            Thread({ handle(sock) }, "tandem-session").start()
        }
    }

    // Caps how many not-yet-identified connections can tie up threads at once.
    private val preAuth = java.util.concurrent.Semaphore(3)

    private fun handle(sock: SSLSocket) {
        var released = false
        try {
            sock.tcpNoDelay = true
            sock.keepAlive = true
            sock.soTimeout = 10_000  // handshake and hello must arrive quickly
            sock.startHandshake()
            val fp = Identity.fingerprint(sock.session.peerCertificates[0])
            val s = Session(sock, fp) { closed ->
                Pairing.cancel(closed)
                Link.detach(closed)
            }
            s.run { hello ->
                sock.soTimeout = 90_000
                released = true
                preAuth.release()
                s.peerName = hello.optString("name", "PC").take(64)
                // A PC that lost its pin asks to pair again even if we still trust it.
                s.trusted = Pairing.isTrusted(fp) && !hello.optBoolean("pair")
                s.send(
                    JSONObject().put("t", "hello").put("v", 1)
                        .put("name", com.cyberwolf.tandem.deviceName())
                        .put("fp", Identity.fingerprint).put("trusted", s.trusted)
                )
                when {
                    s.trusted -> Link.attach(s)
                    Pairing.allow(sock.inetAddress?.hostAddress ?: "") -> Pairing.request(s)
                    else -> {
                        s.event("pair.closed", null)
                        Thread.sleep(300)  // let the writer flush before hanging up
                        s.close()
                    }
                }
            }
        } catch (e: Exception) {
            Log.i(Session.TAG, "handshake failed: ${e.message}")
            runCatching { sock.close() }
        } finally {
            if (!released) preAuth.release()
        }
    }
}
