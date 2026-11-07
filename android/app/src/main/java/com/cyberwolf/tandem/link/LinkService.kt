package com.cyberwolf.tandem.link

import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import com.cyberwolf.tandem.MainActivity
import com.cyberwolf.tandem.R
import com.cyberwolf.tandem.TandemApp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/** Keeps the link server alive and advertises it as `_tandem._tcp`. */
class LinkService : Service() {
    private val server = LinkServer()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var nsdListener: NsdManager.RegistrationListener? = null

    override fun onBind(intent: Intent?) = null

    override fun onCreate() {
        super.onCreate()
        startForeground(1, notification(null), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        server.start()
        advertise()
        scope.launch {
            Link.state.collect { st ->
                getSystemService(NotificationManager::class.java).notify(1, notification(st.peer))
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int) = START_STICKY

    override fun onDestroy() {
        scope.cancel()
        nsdListener?.let { runCatching { getSystemService(NsdManager::class.java).unregisterService(it) } }
        server.stop()
        super.onDestroy()
    }

    private fun advertise() {
        val info = NsdServiceInfo().apply {
            serviceName = "Tandem ${Build.MODEL}"
            serviceType = "_tandem._tcp"
            port = Link.PORT
            setAttribute("fp", Identity.fingerprint.take(16))
        }
        val l = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(i: NsdServiceInfo) {}
            override fun onRegistrationFailed(i: NsdServiceInfo, e: Int) {}
            override fun onServiceUnregistered(i: NsdServiceInfo) {}
            override fun onUnregistrationFailed(i: NsdServiceInfo, e: Int) {}
        }
        getSystemService(NsdManager::class.java).registerService(info, NsdManager.PROTOCOL_DNS_SD, l)
        nsdListener = l
    }

    private fun notification(peer: String?): Notification =
        Notification.Builder(this, TandemApp.CH_LINK)
            .setSmallIcon(R.drawable.ic_stat_tandem)
            .setContentTitle(if (peer != null) "Linked to $peer" else "Waiting for PC")
            .setContentIntent(PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE))
            .setOngoing(true)
            .build()

    companion object {
        fun start(ctx: Context) = ctx.startForegroundService(Intent(ctx, LinkService::class.java))
    }
}
