package com.cyberwolf.tandem.link

import android.util.Log
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.util.concurrent.Executors
import javax.net.ssl.SSLSocket

/** One connected PC. Requests run on a small pool so a slow query never blocks the reader. */
class Session(private val socket: SSLSocket, val peerFp: String, val onClose: (Session) -> Unit) {
    private val inp = DataInputStream(BufferedInputStream(socket.inputStream))
    private val out = DataOutputStream(BufferedOutputStream(socket.outputStream, 64 * 1024))
    private val pool = Executors.newFixedThreadPool(4)
    // Writes go through one thread so callers on the main thread never touch the network.
    private val writer = Executors.newSingleThreadExecutor()

    @Volatile var trusted = false
    @Volatile var peerName = "PC"
    @Volatile var closed = false

    fun send(obj: JSONObject) {
        if (closed) return
        runCatching {
            writer.execute {
                try {
                    Frames.write(out, obj)
                } catch (e: Exception) {
                    close()
                }
            }
        }
    }

    fun event(name: String, data: Any?) =
        send(JSONObject().put("t", "ev").put("e", name).put("d", data ?: JSONObject.NULL))

    fun run(hello: (JSONObject) -> Unit) {
        try {
            hello(Frames.read(inp, 64 shl 10))  // unauthenticated peers get a small first frame
            while (!closed) {
                val f = Frames.read(inp)
                if (f.optString("t") != "req") continue
                val id = f.optLong("id")
                val method = f.optString("m")
                val args = f.optJSONObject("a") ?: JSONObject()
                pool.execute { answer(id, method, args) }
            }
        } catch (e: Exception) {
            if (!closed) Log.i(TAG, "session ended: ${e.message}")
        } finally {
            close()
        }
    }

    private fun answer(id: Long, method: String, args: JSONObject) {
        val res = JSONObject().put("t", "res").put("id", id)
        try {
            if (!trusted && method != "ping") throw Router.Failure("unpaired")
            res.put("ok", true).put("r", Router.dispatch(method, args) ?: JSONObject.NULL)
        } catch (e: Exception) {
            Log.w(TAG, "$method failed", e)
            res.put("ok", false).put("err", e.message ?: e.javaClass.simpleName)
        }
        send(res)
    }

    fun close() {
        if (closed) return
        closed = true
        runCatching { socket.close() }
        pool.shutdownNow()
        writer.shutdown()
        onClose(this)
    }

    companion object { const val TAG = "TandemSession" }
}
