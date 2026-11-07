package com.cyberwolf.tandem.files

import android.app.Activity
import android.app.Notification
import android.app.NotificationManager
import android.content.ContentUris
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.util.Size
import android.widget.Toast
import com.cyberwolf.tandem.R
import com.cyberwolf.tandem.TandemApp
import com.cyberwolf.tandem.base64ToBytes
import com.cyberwolf.tandem.bytesToBase64
import com.cyberwolf.tandem.clip.Clip
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import com.cyberwolf.tandem.toBase64
import org.json.JSONArray
import org.json.JSONObject
import java.io.OutputStream
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/**
 * Photos browsing plus file transfer both ways, chunked as base64 JSON frames.
 * PC -> phone lands in Download/Tandem; phone -> PC is offered through the share sheet.
 */
object Files {
    const val CHUNK = 192 * 1024
    private lateinit var ctx: Context
    private val seq = AtomicInteger(1)

    private class Incoming(val uri: Uri, val out: OutputStream, val name: String, val size: Long) { var got = 0L }
    private val incoming = ConcurrentHashMap<Int, Incoming>()

    fun register(c: Context) {
        ctx = c
        Router.on("photos.list") { a -> photos(a.optInt("limit", 120), a.optInt("offset", 0)) }
        Router.on("photos.thumb") { a ->
            val uri = mediaUri(a.getLong("id"), a.optBoolean("video"))
            val s = a.optInt("size", 256)
            JSONObject().put("id", a.getLong("id")).put("jpeg", ctx.contentResolver.loadThumbnail(uri, Size(s, s), null).toBase64(quality = 80))
        }
        Router.on("photos.read") { a ->
            val uri = mediaUri(a.getLong("id"), a.optBoolean("video"))
            val off = a.optLong("offset", 0)
            ctx.contentResolver.openInputStream(uri)!!.use { s ->
                s.skip(off)
                val buf = ByteArray(CHUNK)
                var n = 0
                while (n < CHUNK) { val r = s.read(buf, n, CHUNK - n); if (r < 0) break; n += r }
                JSONObject().put("data", bytesToBase64(buf.copyOf(n))).put("eof", n < CHUNK)
            }
        }
        Router.on("photos.delete") { a ->
            ctx.contentResolver.delete(mediaUri(a.getLong("id"), a.optBoolean("video")), null, null)
            null
        }
        Router.on("file.begin") { a -> begin(a.getString("name"), a.optLong("size"), a.optString("mime", "application/octet-stream")) }
        Router.on("file.chunk") { a ->
            val t = incoming[a.getInt("tid")] ?: throw Router.Failure("unknown transfer")
            val b = base64ToBytes(a.getString("data"))
            t.out.write(b)
            t.got += b.size
            null
        }
        Router.on("file.end") { a -> end(a.getInt("tid"), a.optBoolean("cancel")) }
    }

    private fun mediaUri(id: Long, video: Boolean) =
        ContentUris.withAppendedId(if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI, id)

    private fun photos(limit: Int, offset: Int): JSONArray {
        val arr = JSONArray()
        val uri = MediaStore.Files.getContentUri("external")
        val args = Bundle().apply {
            putString(android.content.ContentResolver.QUERY_ARG_SQL_SELECTION, "${MediaStore.Files.FileColumns.MEDIA_TYPE} IN (?, ?)")
            putStringArray(android.content.ContentResolver.QUERY_ARG_SQL_SELECTION_ARGS,
                arrayOf(MediaStore.Files.FileColumns.MEDIA_TYPE_IMAGE.toString(), MediaStore.Files.FileColumns.MEDIA_TYPE_VIDEO.toString()))
            putStringArray(android.content.ContentResolver.QUERY_ARG_SORT_COLUMNS, arrayOf(MediaStore.MediaColumns.DATE_ADDED))
            putInt(android.content.ContentResolver.QUERY_ARG_SORT_DIRECTION, android.content.ContentResolver.QUERY_SORT_DIRECTION_DESCENDING)
            putInt(android.content.ContentResolver.QUERY_ARG_LIMIT, limit)
            putInt(android.content.ContentResolver.QUERY_ARG_OFFSET, offset)
        }
        ctx.contentResolver.query(uri, arrayOf(MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME, MediaStore.MediaColumns.DATE_ADDED,
            MediaStore.MediaColumns.SIZE, MediaStore.MediaColumns.MIME_TYPE, MediaStore.Files.FileColumns.MEDIA_TYPE,
            MediaStore.MediaColumns.WIDTH, MediaStore.MediaColumns.HEIGHT, MediaStore.MediaColumns.DURATION), args, null)?.use { c ->
            while (c.moveToNext()) {
                arr.put(JSONObject().put("id", c.getLong(0)).put("name", c.getString(1) ?: "").put("date", c.getLong(2) * 1000)
                    .put("size", c.getLong(3)).put("mime", c.getString(4) ?: "")
                    .put("video", c.getInt(5) == MediaStore.Files.FileColumns.MEDIA_TYPE_VIDEO)
                    .put("w", c.getInt(6)).put("h", c.getInt(7)).put("duration", c.getLong(8)))
            }
        }
        return arr
    }

    private fun begin(name: String, size: Long, mime: String): JSONObject {
        val safe = name.replace('/', '_').ifBlank { "file" }
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, safe)
            put(MediaStore.Downloads.MIME_TYPE, mime)
            put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/Tandem")
            put(MediaStore.Downloads.IS_PENDING, 1)
        }
        val uri = ctx.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: throw Router.Failure("cannot create file")
        val tid = seq.getAndIncrement()
        incoming[tid] = Incoming(uri, ctx.contentResolver.openOutputStream(uri)!!, safe, size)
        return JSONObject().put("tid", tid)
    }

    private fun end(tid: Int, cancel: Boolean): Any? {
        val t = incoming.remove(tid) ?: throw Router.Failure("unknown transfer")
        t.out.close()
        if (cancel) {
            ctx.contentResolver.delete(t.uri, null, null)
            return null
        }
        ctx.contentResolver.update(t.uri, ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }, null, null)
        notifyDone("Received ${t.name}", "Saved to Download/Tandem", t.uri)
        return JSONObject().put("path", "Download/Tandem/${t.name}")
    }

    private fun notifyDone(title: String, text: String, uri: Uri?) {
        val b = Notification.Builder(ctx, TandemApp.CH_FILES).setSmallIcon(R.drawable.ic_stat_tandem)
            .setContentTitle(title).setContentText(text).setStyle(Notification.BigTextStyle().bigText(text)).setAutoCancel(true)
        if (uri != null) {
            val view = Intent(Intent.ACTION_VIEW).setDataAndType(uri, ctx.contentResolver.getType(uri)).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            b.setContentIntent(android.app.PendingIntent.getActivity(ctx, seq.get(), view, android.app.PendingIntent.FLAG_IMMUTABLE))
        }
        ctx.getSystemService(NotificationManager::class.java).notify(1000 + seq.getAndIncrement(), b.build())
    }

    /** Streams [uri] to the PC as file.offer / file.chunk / file.done events. */
    fun sendToPc(c: Context, uri: Uri): Boolean {
        var name = "file"
        var size = -1L
        c.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
            if (it.moveToFirst()) { name = it.getString(0) ?: name; size = if (it.isNull(1)) -1 else it.getLong(1) }
        }
        val tid = seq.getAndIncrement()
        Link.emit("file.offer") { put("tid", tid); put("name", name); put("size", size); put("mime", c.contentResolver.getType(uri) ?: "") }
        c.contentResolver.openInputStream(uri)?.use { s ->
            val buf = ByteArray(CHUNK)
            while (true) {
                var n = 0
                while (n < CHUNK) { val r = s.read(buf, n, CHUNK - n); if (r < 0) break; n += r }
                if (n > 0) Link.emit("file.chunk") { put("tid", tid); put("data", bytesToBase64(buf.copyOf(n))) }
                if (n < CHUNK) break
                if (!Link.connected) return false
                Thread.sleep(2) // let the writer drain so a big file doesn't balloon the queue
            }
        } ?: return false
        Link.emit("file.done") { put("tid", tid) }
        return true
    }
}

/** Share-sheet target: "Send to PC". Text goes to the PC clipboard, files to ~/Downloads/Tandem. */
class ShareActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!Link.connected) {
            Toast.makeText(this, "PC not connected", Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        val uris = ArrayList<Uri>()
        if (intent.action == Intent.ACTION_SEND_MULTIPLE) {
            intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)?.let { uris += it }
        } else {
            intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)?.let { uris += it }
        }
        val text = intent.getStringExtra(Intent.EXTRA_TEXT)
        if (uris.isEmpty() && text != null) {
            Clip.sendText(text)
            Toast.makeText(this, "Sent to PC clipboard", Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        Toast.makeText(this, "Sending ${uris.size} file${if (uris.size == 1) "" else "s"} to PC…", Toast.LENGTH_SHORT).show()
        // Stay alive while streaming: the URI grant belongs to this activity.
        Thread {
            val ok = uris.all { runCatching { Files.sendToPc(this, it) }.getOrDefault(false) }
            runOnUiThread {
                Toast.makeText(this, if (ok) "Sent to PC" else "Transfer failed", Toast.LENGTH_SHORT).show()
                finish()
            }
        }.start()
    }
}
