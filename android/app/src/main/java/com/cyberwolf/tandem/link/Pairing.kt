package com.cyberwolf.tandem.link

import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.cyberwolf.tandem.MainActivity
import com.cyberwolf.tandem.Prefs
import com.cyberwolf.tandem.R
import com.cyberwolf.tandem.TandemApp

/** Trust-on-confirmation: an unknown PC is accepted only after the user compares the code and taps Accept. */
object Pairing {
    private const val NOTIF_ID = 2
    @Volatile private var pending: Session? = null
    // Network pairing is refused unless the user opened a window on the phone (or the PC did over adb).
    @Volatile private var windowUntil = 0L
    private val lastTry = java.util.concurrent.ConcurrentHashMap<String, Long>()
    const val WINDOW_MS = 120_000L

    fun openWindow() { windowUntil = android.os.SystemClock.elapsedRealtime() + WINDOW_MS }
    fun closeWindow() { windowUntil = 0 }
    fun windowOpen() = android.os.SystemClock.elapsedRealtime() < windowUntil

    /** One pairing attempt per address every 5 s, and only while the window is open. */
    fun allow(addr: String): Boolean {
        if (!windowOpen()) return false
        val now = android.os.SystemClock.elapsedRealtime()
        val prev = lastTry.put(addr, now)
        return prev == null || now - prev > 5_000
    }

    fun isTrusted(fp: String) = Prefs.trusted.containsKey(fp)

    fun request(s: Session) {
        pending?.takeIf { it !== s }?.close()
        pending = s
        val code = Identity.pairCode(Identity.fingerprint, s.peerFp)
        Link.setPairing(Link.PairRequest(s.peerName, code))
        val ctx = TandemApp.app
        fun action(accept: Boolean) = PendingIntent.getBroadcast(
            ctx, if (accept) 1 else 0,
            Intent(ctx, PairReceiver::class.java).putExtra("accept", accept),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val n = Notification.Builder(ctx, TandemApp.CH_PAIR)
            .setSmallIcon(R.drawable.ic_stat_tandem)
            .setContentTitle("Pair with ${s.peerName}?")
            .setContentText("Open Tandem to compare the code before accepting")
            .setContentIntent(PendingIntent.getActivity(ctx, 0, Intent(ctx, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE))
            .addAction(Notification.Action.Builder(null, "Reject", action(false)).build())
            .setCategory(Notification.CATEGORY_RECOMMENDATION)
            .setAutoCancel(true)
            .build()
        ctx.getSystemService(NotificationManager::class.java).notify(NOTIF_ID, n)
    }

    fun answer(accept: Boolean) {
        val s = pending ?: return
        pending = null
        Link.setPairing(null)
        TandemApp.app.getSystemService(NotificationManager::class.java).cancel(NOTIF_ID)
        if (!accept || s.closed) {
            s.event("pair.rejected", null)
            s.close()
            return
        }
        closeWindow()
        Prefs.trusted = Prefs.trusted + (s.peerFp to s.peerName)
        s.trusted = true
        Link.attach(s)
        s.event("pair.accepted", null)
    }

    fun cancel(s: Session) {
        if (pending === s) {
            pending = null
            Link.setPairing(null)
            TandemApp.app.getSystemService(NotificationManager::class.java).cancel(NOTIF_ID)
        }
    }

    fun forget(fp: String) {
        Prefs.trusted = Prefs.trusted - fp
        Link.session?.takeIf { it.peerFp == fp }?.close()
    }
}

class PairReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) = Pairing.answer(intent.getBooleanExtra("accept", false))
}

/** Lets `adb shell am broadcast` open the pairing window; DUMP is held by the shell user, not by apps. */
class PairWindowReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) { Pairing.openWindow() }
}
