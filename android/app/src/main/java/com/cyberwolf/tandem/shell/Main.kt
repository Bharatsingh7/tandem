package com.cyberwolf.tandem.shell

import android.content.AttributionSource
import android.content.Context
import android.content.ContextWrapper
import android.os.IBinder
import android.os.Looper
import org.json.JSONObject
import java.lang.reflect.Proxy
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit
import java.util.function.Supplier
import kotlin.system.exitProcess

/**
 * Privileged helper, started from adb as the shell user:
 *   CLASSPATH=<apk> app_process /system/bin com.cyberwolf.tandem.shell.Main <token> <version>
 * Listens on 127.0.0.1:[PORT]; every connection must open with the token the app generated.
 * Survives USB unplug, dies on reboot.
 */
object Main {
    const val PORT = 47902
    private const val PKG = "com.cyberwolf.tandem"

    @JvmStatic
    fun main(args: Array<String>) {
        val token = args.getOrNull(0) ?: exitProcess(2)
        val version = args.getOrNull(1) ?: "0"
        // Replace an older helper (e.g. after an app update rotated the APK path).
        runCatching {
            Socket("127.0.0.1", PORT).use { it.getOutputStream().write("quit\n".toByteArray()) }
            Thread.sleep(400)
        }
        val ss = ServerSocket()
        ss.reuseAddress = true
        ss.bind(InetSocketAddress(InetAddress.getByName("127.0.0.1"), PORT))
        run("cmd telecom add-or-remove-call-companion-app $PKG 0; cmd telecom add-or-remove-call-companion-app $PKG 1; " +
            "appops set $PKG MANAGE_ONGOING_CALLS allow")
        Looper.prepareMainLooper()
        Thread {
            while (true) {
                val s = ss.accept()
                Thread { serve(s, token, version) }.start()
            }
        }.start()
        Looper.loop()
    }

    private fun serve(s: Socket, token: String, version: String) = s.use {
        it.soTimeout = 30_000
        val r = it.getInputStream().bufferedReader()
        val w = it.getOutputStream().bufferedWriter()
        val first = r.readLine() ?: return
        if (first == "quit") exitProcess(0)
        if (first != token) return
        val req = JSONObject(r.readLine() ?: return)
        val res = JSONObject().put("ok", true)
        try {
            when (req.getString("cmd")) {
                "ping" -> res.put("version", version)
                "exec" -> exec(req.getString("sh")).let { (code, out) -> res.put("code", code).put("out", out) }
                "hotspot" -> onMain { Tether.set(req.getBoolean("on")) }
                else -> throw IllegalArgumentException("unknown cmd")
            }
        } catch (e: Throwable) {
            res.put("ok", false).put("err", e.cause?.message ?: e.message ?: e.javaClass.name)
        }
        w.write(res.toString())
        w.write("\n")
        w.flush()
    }

    /** TetheringManager creates Handlers, so it must live on the looper thread. */
    private fun <T> onMain(block: () -> T): T {
        val task = java.util.concurrent.FutureTask(block)
        android.os.Handler(Looper.getMainLooper()).post(task)
        return try {
            task.get(20, TimeUnit.SECONDS)
        } catch (e: java.util.concurrent.ExecutionException) {
            throw e.cause ?: e
        }
    }

    // The helper runs as the shell user, so it only runs the exact commands Tandem needs. Even a hijacked
    // app session or a leaked token can't turn it into a general shell.
    private val ALLOWED = listOf(
        Regex("cmd bluetooth_manager (enable|disable)"),
        Regex("cmd wifi set-wifi-enabled (enabled|disabled)"),
        Regex("cmd phone data (enable|disable)"),
        Regex("cmd connectivity airplane-mode (enable|disable)"),
        Regex("cmd location set-location-enabled (true|false)"),
        Regex("cmd notification set_dnd (priority|off)"),
        Regex("input keyevent (223|224)"),
        Regex("settings put system screen_brightness_mode 1"),
        Regex("settings put system screen_brightness_mode 0; settings put system screen_brightness \\d{1,3}"),
        Regex("am start -a android\\.intent\\.action\\.VIEW -d 'https?://[A-Za-z0-9._~:/?#\\[\\]@!$&()*+,;=%-]+'"),
    )

    /** Commands arriving over the socket: allowlisted only. */
    fun exec(sh: String): Pair<Int, String> {
        if (ALLOWED.none { it.matches(sh) }) throw SecurityException("command not allowed")
        return run(sh)
    }

    /** The helper's own fixed setup commands, never anything received. */
    private fun run(sh: String): Pair<Int, String> {
        val p = ProcessBuilder("/system/bin/sh", "-c", sh).redirectErrorStream(true).start()
        val out = p.inputStream.bufferedReader().readText()
        if (!p.waitFor(20, TimeUnit.SECONDS)) p.destroyForcibly()
        return p.exitValue() to out.trim()
    }
}

/** Wi-Fi hotspot through the hidden TetheringManager, presented as com.android.shell (holds TETHER_PRIVILEGED). */
private object Tether {
    private const val TETHERING_WIFI = 0
    private const val SHELL = "com.android.shell"

    private class ShellContext(base: Context) : ContextWrapper(base) {
        override fun getPackageName() = SHELL
        override fun getOpPackageName() = SHELL
        override fun getAttributionTag(): String? = null
        override fun getAttributionSource(): AttributionSource =
            AttributionSource.Builder(2000).setPackageName(SHELL).build()
        override fun getApplicationContext() = this
    }

    private val ctx: Context by lazy {
        val at = Class.forName("android.app.ActivityThread")
        val thread = at.getDeclaredConstructor().apply { isAccessible = true }.newInstance()
        at.getDeclaredField("sCurrentActivityThread").apply { isAccessible = true }.set(null, thread)
        ShellContext(at.getDeclaredMethod("getSystemContext").apply { isAccessible = true }.invoke(thread) as Context)
    }

    private val manager: Any by lazy {
        val sm = Class.forName("android.os.ServiceManager").getMethod("getService", String::class.java)
        val supplier = Supplier { sm.invoke(null, "tethering") as IBinder }
        Class.forName("android.net.TetheringManager")
            .getDeclaredConstructor(Context::class.java, Supplier::class.java)
            .apply { isAccessible = true }
            .newInstance(ctx, supplier)
    }

    fun set(on: Boolean) {
        val cls = manager.javaClass
        if (!on) {
            cls.getMethod("stopTethering", Int::class.javaPrimitiveType).invoke(manager, TETHERING_WIFI)
            return
        }
        val reqCls = Class.forName("android.net.TetheringManager\$TetheringRequest")
        val builder = Class.forName("android.net.TetheringManager\$TetheringRequest\$Builder")
        val req = builder.getMethod("build").invoke(builder.getConstructor(Int::class.javaPrimitiveType).newInstance(TETHERING_WIFI))
        val cbCls = Class.forName("android.net.TetheringManager\$StartTetheringCallback")
        val done = CountDownLatch(1)
        var error: Int? = null
        val cb = Proxy.newProxyInstance(cbCls.classLoader, arrayOf(cbCls)) { _, m, a ->
            when (m.name) {
                "onTetheringStarted" -> done.countDown()
                "onTetheringFailed" -> { error = a?.get(0) as? Int; done.countDown() }
            }
            null
        }
        // The callback arrives on a binder thread, so waiting here on the looper is safe.
        cls.getMethod("startTethering", reqCls, Executor::class.java, cbCls).invoke(manager, req, Executor { it.run() }, cb)
        if (!done.await(15, TimeUnit.SECONDS)) throw IllegalStateException("hotspot start timed out")
        // 18 = TETHER_ERROR_DUPLICATE_REQUEST: the hotspot is already up.
        error?.takeIf { it != 18 }?.let { throw IllegalStateException("tethering error $it") }
    }
}
