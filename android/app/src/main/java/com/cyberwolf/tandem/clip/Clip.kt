package com.cyberwolf.tandem.clip

import android.app.Activity
import android.app.PendingIntent
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.service.quicksettings.TileService
import android.widget.Toast
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router

/**
 * Clipboard sync. PC -> phone is silent. Android only lets the focused app read the clipboard,
 * so phone -> PC happens through the quick-settings tile, the share sheet, or while Tandem is open.
 */
object Clip {
    private lateinit var ctx: Context
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var lastFromPc: String? = null

    fun register(c: Context) {
        ctx = c
        Router.on("clip.set") { a ->
            val text = a.getString("text")
            lastFromPc = text
            main.post {
                ctx.getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText("From PC", text))
            }
            null
        }
        main.post {
            ctx.getSystemService(ClipboardManager::class.java).addPrimaryClipChangedListener { readAndSend(ctx) }
        }
    }

    /** Works only while one of our windows has focus. */
    fun readAndSend(c: Context): Boolean {
        val clip = c.getSystemService(ClipboardManager::class.java).primaryClip ?: return false
        val text = clip.getItemAt(0)?.coerceToText(c)?.toString().orEmpty()
        if (text.isEmpty() || text == lastFromPc) return false
        lastFromPc = text
        Link.emit("clip") { put("text", text) }
        return Link.connected
    }

    fun sendText(text: String) {
        lastFromPc = text
        Link.emit("clip") { put("text", text) }
    }
}

/** Invisible activity: gains focus, reads the clipboard, sends it, closes. */
class ClipActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus) return
        val ok = Clip.readAndSend(this)
        Toast.makeText(this, if (ok) "Clipboard sent to PC" else if (!Link.connected) "PC not connected" else "Nothing new to send", Toast.LENGTH_SHORT).show()
        finish()
    }
}

class ClipTile : TileService() {
    override fun onClick() {
        val pi = PendingIntent.getActivity(this, 0, Intent(this, ClipActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE)
        startActivityAndCollapse(pi)
    }
}
