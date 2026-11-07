package com.cyberwolf.tandem.media

import android.content.ComponentName
import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import com.cyberwolf.tandem.notify.NotificationMirror
import com.cyberwolf.tandem.scaledTo
import com.cyberwolf.tandem.toBase64
import org.json.JSONObject

/** Now-playing from whichever app holds the most relevant media session; the PC exposes it over MPRIS. */
object MediaBridge {
    private lateinit var ctx: Context
    private val main = Handler(Looper.getMainLooper())
    private var controller: MediaController? = null
    private var lastArtKey: String? = null
    private var lastArt: String? = null
    private val emit = Runnable { Link.emit("media", snapshot()) }

    private val cb = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadata?) = changed()
        override fun onPlaybackStateChanged(state: PlaybackState?) = changed()
        override fun onSessionDestroyed() {
            refresh()
        }
    }

    fun register(c: Context) {
        ctx = c
        Router.on("media.get") { onMain { snapshot() } }
        Router.on("media.cmd") { a ->
            onMain {
                val t = controller?.transportControls ?: throw Router.Failure("nothing playing")
                when (a.getString("cmd")) {
                    "play" -> t.play()
                    "pause" -> t.pause()
                    "toggle" -> if (controller?.playbackState?.state == PlaybackState.STATE_PLAYING) t.pause() else t.play()
                    "next" -> t.skipToNext()
                    "prev" -> t.skipToPrevious()
                    "stop" -> t.stop()
                    "seek" -> t.seekTo(a.getLong("pos"))
                }
            }
            null
        }
    }

    private fun <T> onMain(block: () -> T): T {
        val task = java.util.concurrent.FutureTask(block)
        main.post(task)
        return task.get(5, java.util.concurrent.TimeUnit.SECONDS)
    }

    fun listenerReady(svc: NotificationMirror) {
        main.post {
            val msm = ctx.getSystemService(MediaSessionManager::class.java)
            val comp = ComponentName(ctx, NotificationMirror::class.java)
            runCatching { msm.addOnActiveSessionsChangedListener({ _ -> refresh() }, comp, main) }
            refresh()
        }
    }

    private fun refresh() {
        main.post { pickController() }
    }

    private fun pickController() {
        val list = runCatching {
            ctx.getSystemService(MediaSessionManager::class.java).getActiveSessions(ComponentName(ctx, NotificationMirror::class.java))
        }.getOrDefault(emptyList())
        val best = list.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING } ?: list.firstOrNull()
        if (best?.sessionToken != controller?.sessionToken) {
            controller?.unregisterCallback(cb)
            controller = best
            best?.registerCallback(cb, main)
        }
        changed()
    }

    private fun changed() {
        main.removeCallbacks(emit)
        main.postDelayed(emit, 250)
    }

    private fun snapshot(): JSONObject {
        val c = controller ?: return JSONObject().put("active", false)
        val md = c.metadata
        val ps = c.playbackState
        val o = JSONObject().put("active", true).put("pkg", c.packageName)
            .put("app", runCatching { ctx.packageManager.getApplicationLabel(ctx.packageManager.getApplicationInfo(c.packageName, 0)).toString() }.getOrDefault(c.packageName))
            .put("title", md?.getString(MediaMetadata.METADATA_KEY_TITLE) ?: md?.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE) ?: "")
            .put("artist", md?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: md?.getString(MediaMetadata.METADATA_KEY_DISPLAY_SUBTITLE) ?: "")
            .put("album", md?.getString(MediaMetadata.METADATA_KEY_ALBUM) ?: "")
            .put("duration", md?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0)
            .put("state", when (ps?.state) {
                PlaybackState.STATE_PLAYING -> "playing"
                PlaybackState.STATE_PAUSED -> "paused"
                PlaybackState.STATE_BUFFERING -> "buffering"
                else -> "stopped"
            })
            .put("position", ps?.position ?: 0).put("updated", ps?.lastPositionUpdateTime ?: 0).put("speed", ps?.playbackSpeed ?: 1f)
            .put("canNext", (ps?.actions ?: 0) and PlaybackState.ACTION_SKIP_TO_NEXT != 0L)
            .put("canPrev", (ps?.actions ?: 0) and PlaybackState.ACTION_SKIP_TO_PREVIOUS != 0L)
            .put("canSeek", (ps?.actions ?: 0) and PlaybackState.ACTION_SEEK_TO != 0L)
        val key = "${c.packageName}|${o.getString("title")}|${o.getString("album")}"
        if (key != lastArtKey) {
            lastArtKey = key
            val bmp: Bitmap? = md?.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART) ?: md?.getBitmap(MediaMetadata.METADATA_KEY_ART)
                ?: md?.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON)
            lastArt = bmp?.let { runCatching { it.scaledTo(400).toBase64() }.getOrNull() }
        }
        o.put("art", lastArt ?: JSONObject.NULL).put("artKey", key.hashCode())
        // SystemClock-relative timestamps are meaningless on the PC; send the position "now".
        if (ps != null && ps.state == PlaybackState.STATE_PLAYING && ps.lastPositionUpdateTime > 0) {
            val elapsed = android.os.SystemClock.elapsedRealtime() - ps.lastPositionUpdateTime
            o.put("position", ps.position + (elapsed * ps.playbackSpeed).toLong())
        }
        return o
    }
}
