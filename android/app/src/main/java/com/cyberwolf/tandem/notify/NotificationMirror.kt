package com.cyberwolf.tandem.notify

import android.app.Notification
import android.app.RemoteInput
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.drawable.Icon
import android.os.Bundle
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import androidx.core.app.NotificationCompat
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import com.cyberwolf.tandem.media.MediaBridge
import com.cyberwolf.tandem.scaledTo
import com.cyberwolf.tandem.toBase64
import com.cyberwolf.tandem.toBitmap
import org.json.JSONArray
import org.json.JSONObject

class NotificationMirror : NotificationListenerService() {
    override fun onListenerConnected() {
        Notifs.service = this
        MediaBridge.listenerReady(this)
    }

    override fun onListenerDisconnected() {
        Notifs.service = null
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) = Notifs.posted(sbn)
    override fun onNotificationRemoved(sbn: StatusBarNotification) = Notifs.removed(sbn)
}

/**
 * Mirrors notifications to the PC. Messaging apps (WhatsApp, Telegram, Google Messages…) expose
 * their recent conversation through MessagingStyle, which the PC turns into chat threads.
 */
object Notifs {
    @Volatile var service: NotificationListenerService? = null
    private lateinit var ctx: Context
    private val labels = HashMap<String, String>()

    fun register(c: Context) {
        ctx = c
        Router.on("notif.list") {
            JSONArray().apply { active().forEach { sbn -> json(sbn)?.let { put(it) } } }
        }
        Router.on("notif.dismiss") { a ->
            val svc = service ?: throw Router.Failure("notification access off")
            if (a.has("key")) svc.cancelNotification(a.getString("key")) else svc.cancelAllNotifications()
            null
        }
        Router.on("notif.action") { a ->
            val sbn = find(a.getString("key"))
            sbn.notification.actions?.getOrNull(a.getInt("index"))?.actionIntent?.send()
                ?: throw Router.Failure("no such action")
            null
        }
        Router.on("notif.open") { a ->
            find(a.getString("key")).notification.contentIntent?.send() ?: throw Router.Failure("nothing to open")
            null
        }
        Router.on("notif.reply") { a -> reply(find(a.getString("key")), a.getString("text")); null }
        Router.on("notif.icon") { a ->
            val pkg = a.getString("pkg")
            JSONObject().put("pkg", pkg).put("png", ctx.packageManager.getApplicationIcon(pkg).toBitmap(96).toBase64(Bitmap.CompressFormat.PNG))
        }
    }

    private fun active(): List<StatusBarNotification> =
        runCatching { service?.activeNotifications?.toList() }.getOrNull().orEmpty()

    private fun find(key: String) = active().firstOrNull { it.key == key } ?: throw Router.Failure("notification is gone")

    fun posted(sbn: StatusBarNotification) {
        if (!Link.connected) return
        json(sbn)?.let { Link.emit("notif.posted", it) }
    }

    fun removed(sbn: StatusBarNotification) {
        if (sbn.packageName == ctx.packageName) return
        Link.emit("notif.removed") { put("key", sbn.key) }
    }

    private fun appLabel(pkg: String) = labels.getOrPut(pkg) {
        runCatching {
            ctx.packageManager.getApplicationLabel(ctx.packageManager.getApplicationInfo(pkg, PackageManager.ApplicationInfoFlags.of(0))).toString()
        }.getOrDefault(pkg)
    }

    private fun iconB64(icon: Icon?, size: Int = 96): String? = runCatching {
        icon?.loadDrawable(ctx)?.toBitmap(size)?.scaledTo(size)?.toBase64(Bitmap.CompressFormat.PNG)
    }.getOrNull()

    fun json(sbn: StatusBarNotification): JSONObject? {
        if (sbn.packageName == ctx.packageName) return null
        val n = sbn.notification
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return null
        val ex = n.extras
        val o = JSONObject()
        o.put("key", sbn.key).put("pkg", sbn.packageName).put("app", appLabel(sbn.packageName))
            .put("time", sbn.postTime).put("ongoing", sbn.isOngoing).put("clearable", sbn.isClearable)
            .put("category", n.category ?: "")
            .put("title", ex.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: "")
            .put("text", ex.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: "")
            .put("big", ex.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString() ?: "")
            .put("sub", ex.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString() ?: "")
            .put("shortcut", n.shortcutId ?: "")
            .put("silent", n.flags and Notification.FLAG_ONLY_ALERT_ONCE != 0 && sbn.isOngoing)
        if (ex.containsKey(Notification.EXTRA_MEDIA_SESSION)) o.put("media", true)
        o.put("progress", if (ex.containsKey(Notification.EXTRA_PROGRESS_MAX) && ex.getInt(Notification.EXTRA_PROGRESS_MAX) > 0)
            JSONObject().put("v", ex.getInt(Notification.EXTRA_PROGRESS)).put("max", ex.getInt(Notification.EXTRA_PROGRESS_MAX)) else JSONObject.NULL)

        val actions = JSONArray()
        n.actions?.forEachIndexed { i, a ->
            actions.put(JSONObject().put("i", i).put("title", a.title?.toString() ?: "")
                .put("reply", a.remoteInputs?.any { it.allowFreeFormInput } == true))
        }
        o.put("actions", actions)

        NotificationCompat.MessagingStyle.extractMessagingStyleFromNotification(n)?.let { ms ->
            val conv = JSONObject()
            conv.put("title", ms.conversationTitle?.toString() ?: o.getString("title"))
            conv.put("group", ms.isGroupConversation)
            val me = ms.user.name?.toString()
            val msgs = JSONArray()
            for (m in ms.messages) {
                val sender = m.person
                msgs.put(JSONObject()
                    .put("text", m.text?.toString() ?: "")
                    .put("time", m.timestamp)
                    .put("sender", sender?.name?.toString() ?: me ?: "")
                    .put("senderKey", sender?.key ?: "")
                    .put("self", sender == null || (sender.key != null && sender.key == ms.user.key))
                    .put("media", m.dataMimeType ?: ""))
            }
            conv.put("messages", msgs)
            o.put("conversation", conv)
        }
        (ex.getParcelable(Notification.EXTRA_LARGE_ICON, Icon::class.java) ?: n.getLargeIcon())?.let { icon ->
            iconB64(icon)?.let { o.put("avatar", it) }
        }
        return o
    }

    private fun reply(sbn: StatusBarNotification, text: String) {
        val action = sbn.notification.actions?.firstOrNull { a -> a.remoteInputs?.any { it.allowFreeFormInput } == true }
            ?: throw Router.Failure("this notification can't be replied to")
        val intent = Intent()
        val results = Bundle()
        for (ri in action.remoteInputs) results.putCharSequence(ri.resultKey, text)
        RemoteInput.addResultsToIntent(action.remoteInputs, intent, results)
        RemoteInput.setResultsSource(intent, RemoteInput.SOURCE_FREE_FORM_INPUT)
        action.actionIntent.send(ctx, 0, intent)
    }
}
