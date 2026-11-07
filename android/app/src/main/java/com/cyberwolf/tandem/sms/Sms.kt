package com.cyberwolf.tandem.sms

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.os.Handler
import android.os.HandlerThread
import android.provider.Telephony
import android.telephony.SmsManager
import android.telephony.SubscriptionManager
import com.cyberwolf.tandem.contacts.Lookup
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicInteger

/** SMS threads read from the provider, sent through SmsManager (the system logs sent messages for us). */
object Sms {
    private lateinit var ctx: Context
    private val handler = Handler(HandlerThread("tandem-sms").apply { start() }.looper)
    private val seq = AtomicInteger(1)
    private const val SENT = "com.cyberwolf.tandem.SMS_SENT"
    private val changed = Runnable { Link.emit("sms.changed", null) }

    fun register(c: Context) {
        ctx = c
        Router.on("sms.threads") { a -> threads(a.optInt("limit", 60)) }
        Router.on("sms.messages") { a -> messages(a.getLong("thread"), a.optInt("limit", 200)) }
        Router.on("sms.send") { a -> send(a.getString("address"), a.getString("body"), a.optInt("sub", -1)) }

        c.registerReceiver(object : BroadcastReceiver() {
            override fun onReceive(context: Context, i: Intent) {
                Link.emit("sms.sent") {
                    put("ref", i.getIntExtra("ref", 0))
                    put("ok", resultCode == android.app.Activity.RESULT_OK)
                }
            }
        }, IntentFilter(SENT), Context.RECEIVER_NOT_EXPORTED)

        runCatching {
            c.contentResolver.registerContentObserver(Telephony.Sms.CONTENT_URI, true, object : ContentObserver(handler) {
                override fun onChange(selfChange: Boolean) {
                    handler.removeCallbacks(changed)
                    handler.postDelayed(changed, 700)
                }
            })
        }
    }

    private fun threads(limit: Int): JSONArray {
        val out = JSONArray()
        val seen = HashMap<Long, JSONObject>()
        ctx.contentResolver.query(
            Telephony.Sms.CONTENT_URI,
            arrayOf(Telephony.Sms.THREAD_ID, Telephony.Sms.ADDRESS, Telephony.Sms.BODY, Telephony.Sms.DATE, Telephony.Sms.TYPE, Telephony.Sms.READ),
            null, null, "${Telephony.Sms.DATE} DESC LIMIT 6000"
        )?.use { c ->
            while (c.moveToNext()) {
                val tid = c.getLong(0)
                val existing = seen[tid]
                if (existing != null) {
                    if (c.getInt(5) == 0 && c.getInt(4) == Telephony.Sms.MESSAGE_TYPE_INBOX) existing.put("unread", existing.getInt("unread") + 1)
                    continue
                }
                if (seen.size >= limit) continue
                val addr = c.getString(1) ?: ""
                val t = JSONObject().put("thread", tid).put("address", addr).put("name", Lookup.name(ctx, addr) ?: "")
                    .put("snippet", c.getString(2) ?: "").put("date", c.getLong(3)).put("type", c.getInt(4))
                    .put("unread", if (c.getInt(5) == 0 && c.getInt(4) == Telephony.Sms.MESSAGE_TYPE_INBOX) 1 else 0)
                seen[tid] = t
                out.put(t)
            }
        }
        return out
    }

    private fun messages(thread: Long, limit: Int): JSONArray {
        val rows = ArrayList<JSONObject>()
        ctx.contentResolver.query(
            Telephony.Sms.CONTENT_URI,
            arrayOf(Telephony.Sms._ID, Telephony.Sms.ADDRESS, Telephony.Sms.BODY, Telephony.Sms.DATE, Telephony.Sms.TYPE, Telephony.Sms.STATUS),
            "${Telephony.Sms.THREAD_ID} = ?", arrayOf(thread.toString()), "${Telephony.Sms.DATE} DESC LIMIT $limit"
        )?.use { c ->
            while (c.moveToNext()) {
                rows += JSONObject().put("id", c.getLong(0)).put("address", c.getString(1) ?: "").put("body", c.getString(2) ?: "")
                    .put("date", c.getLong(3)).put("type", c.getInt(4)).put("status", c.getInt(5))
            }
        }
        return JSONArray(rows.asReversed())
    }

    private fun send(address: String, body: String, sub: Int): JSONObject {
        val sm = if (sub >= 0) ctx.getSystemService(SmsManager::class.java).createForSubscriptionId(sub)
        else ctx.getSystemService(SmsManager::class.java).createForSubscriptionId(SubscriptionManager.getDefaultSmsSubscriptionId())
        val ref = seq.getAndIncrement()
        val parts = sm.divideMessage(body)
        val sent = ArrayList<PendingIntent>()
        parts.forEachIndexed { i, _ ->
            sent += PendingIntent.getBroadcast(ctx, ref * 100 + i,
                Intent(SENT).setPackage(ctx.packageName).putExtra("ref", ref), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        }
        sm.sendMultipartTextMessage(address, null, parts, sent, null)
        return JSONObject().put("ref", ref)
    }

    fun received(address: String, body: String, date: Long) {
        Link.emit("sms.received") {
            put("address", address)
            put("name", Lookup.name(ctx, address) ?: "")
            put("body", body)
            put("date", date)
        }
    }
}

class SmsReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val msgs = Telephony.Sms.Intents.getMessagesFromIntent(intent) ?: return
        if (msgs.isEmpty()) return
        Sms.received(msgs[0].displayOriginatingAddress ?: "", msgs.joinToString("") { it.displayMessageBody ?: "" }, msgs[0].timestampMillis)
    }
}
