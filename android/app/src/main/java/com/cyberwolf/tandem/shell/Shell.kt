package com.cyberwolf.tandem.shell

import com.cyberwolf.tandem.Prefs
import org.json.JSONObject
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket

/** App-side client for the shell helper. Throws when the helper isn't running. */
object Shell {
    fun call(req: JSONObject, timeoutMs: Int = 25_000): JSONObject = Socket().use { s ->
        s.connect(InetSocketAddress("127.0.0.1", Main.PORT), 800)
        s.soTimeout = timeoutMs
        val w = s.getOutputStream().bufferedWriter()
        w.write(Prefs.shellToken + "\n" + req + "\n")
        w.flush()
        val line = s.getInputStream().bufferedReader().readLine() ?: throw IOException("helper closed the connection")
        JSONObject(line).also { if (!it.optBoolean("ok")) throw IOException(it.optString("err", "helper error")) }
    }

    fun exec(sh: String): String = call(JSONObject().put("cmd", "exec").put("sh", sh)).optString("out")

    /** Helper's version string, or null when it isn't running. */
    fun version(): String? = runCatching { call(JSONObject().put("cmd", "ping"), 2_000).optString("version") }.getOrNull()
}
