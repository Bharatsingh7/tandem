package com.cyberwolf.tandem.status

import android.app.NotificationManager
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.hardware.camera2.CameraManager
import android.location.LocationManager
import android.media.AudioManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.HandlerThread
import android.os.StatFs
import android.provider.Settings
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import com.cyberwolf.tandem.shell.Shell
import org.json.JSONObject

/** Phone state shown on the PC's Home page. Pushed as `status` whenever something changes. */
object Status {
    private lateinit var ctx: Context
    private val thread = HandlerThread("tandem-status").apply { start() }
    val handler = Handler(thread.looper)
    @Volatile var torchOn = false
    private val emitTask = Runnable { if (Link.connected) Link.emit("status", snapshot()) }

    fun changed() {
        handler.removeCallbacks(emitTask)
        handler.postDelayed(emitTask, 400)
    }

    fun register(c: Context) {
        ctx = c
        Router.on("status.get") { snapshot() }
        Router.on("ping") { "pong" }

        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_BATTERY_CHANGED)
            addAction(Intent.ACTION_AIRPLANE_MODE_CHANGED)
            addAction("android.bluetooth.adapter.action.STATE_CHANGED")
            addAction(WifiManager.WIFI_STATE_CHANGED_ACTION)
            addAction("android.net.wifi.WIFI_AP_STATE_CHANGED")
            addAction(AudioManager.RINGER_MODE_CHANGED_ACTION)
            addAction("android.media.VOLUME_CHANGED_ACTION")
            addAction(NotificationManager.ACTION_INTERRUPTION_FILTER_CHANGED)
            addAction(LocationManager.MODE_CHANGED_ACTION)
        }
        var lastBattery = -1
        c.registerReceiver(object : BroadcastReceiver() {
            override fun onReceive(context: Context, i: Intent) {
                if (i.action == "android.net.wifi.WIFI_AP_STATE_CHANGED") apState = i.getIntExtra("wifi_state", 11)
                if (i.action == Intent.ACTION_BATTERY_CHANGED) {
                    val key = i.getIntExtra(BatteryManager.EXTRA_LEVEL, 0) * 10 + i.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0)
                    if (key == lastBattery) return
                    lastBattery = key
                }
                changed()
            }
        }, filter, null, handler, Context.RECEIVER_EXPORTED)

        c.contentResolver.registerContentObserver(Settings.Global.getUriFor("mobile_data"), true,
            object : ContentObserver(handler) { override fun onChange(self: Boolean) = changed() })

        c.getSystemService(CameraManager::class.java).registerTorchCallback(object : CameraManager.TorchCallback() {
            override fun onTorchModeChanged(id: String, enabled: Boolean) { torchOn = enabled; changed() }
        }, handler)

        runCatching {
            c.getSystemService(TelephonyManager::class.java).registerTelephonyCallback(
                { handler.post(it) },
                object : TelephonyCallback(), TelephonyCallback.SignalStrengthsListener {
                    var last = -1
                    override fun onSignalStrengthsChanged(s: android.telephony.SignalStrength) {
                        if (s.level != last) { last = s.level; changed() }
                    }
                })
        }
    }

    /** Last WIFI_AP_STATE_CHANGED value (13 = enabled); the broadcast isn't sticky, so seed it once. */
    @Volatile private var apState = -1

    fun hotspotOn(): Boolean {
        if (apState < 0) apState = initialApState()
        return apState == 13 || apState == 12
    }

    private fun initialApState(): Int = runCatching {
        WifiManager::class.java.getMethod("getWifiApState").invoke(ctx.getSystemService(WifiManager::class.java)) as Int
    }.getOrElse {
        val up = runCatching {
            java.net.NetworkInterface.getNetworkInterfaces().toList()
                .any { (it.name.startsWith("ap") || it.name.startsWith("swlan")) && it.isUp }
        }.getOrDefault(false)
        if (up) 13 else 11
    }

    fun snapshot(): JSONObject {
        val bat = ctx.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val am = ctx.getSystemService(AudioManager::class.java)
        val tm = ctx.getSystemService(TelephonyManager::class.java)
        val nm = ctx.getSystemService(NotificationManager::class.java)
        val cm = ctx.getSystemService(ConnectivityManager::class.java)
        val caps = cm.getNetworkCapabilities(cm.activeNetwork)
        val stat = StatFs(Environment.getDataDirectory().path)
        fun vol(stream: Int) = JSONObject().put("v", am.getStreamVolume(stream)).put("max", am.getStreamMaxVolume(stream))
        return JSONObject().apply {
            put("model", com.cyberwolf.tandem.deviceName())
            put("android", Build.VERSION.RELEASE)
            put("battery", JSONObject().apply {
                val level = bat?.getIntExtra(BatteryManager.EXTRA_LEVEL, 0) ?: 0
                val scale = bat?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
                put("level", level * 100 / scale.coerceAtLeast(1))
                put("charging", bat?.getIntExtra(BatteryManager.EXTRA_STATUS, 0) == BatteryManager.BATTERY_STATUS_CHARGING)
                put("plugged", (bat?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0)
                put("temp", (bat?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0) / 10.0)
            })
            put("toggles", JSONObject().apply {
                put("bluetooth", runCatching { ctx.getSystemService(BluetoothManager::class.java).adapter.isEnabled }
                    .getOrElse { Settings.Global.getInt(ctx.contentResolver, "bluetooth_on", 0) != 0 })
                put("wifi", ctx.getSystemService(WifiManager::class.java).isWifiEnabled)
                put("hotspot", hotspotOn())
                put("data", runCatching { tm.isDataEnabled }.getOrElse { Settings.Global.getInt(ctx.contentResolver, "mobile_data", 0) != 0 })
                put("airplane", Settings.Global.getInt(ctx.contentResolver, Settings.Global.AIRPLANE_MODE_ON, 0) != 0)
                put("dnd", nm.currentInterruptionFilter > NotificationManager.INTERRUPTION_FILTER_ALL)
                put("location", ctx.getSystemService(LocationManager::class.java).isLocationEnabled)
                put("torch", torchOn)
            })
            put("ringer", when (am.ringerMode) {
                AudioManager.RINGER_MODE_SILENT -> "silent"
                AudioManager.RINGER_MODE_VIBRATE -> "vibrate"
                else -> "normal"
            })
            put("volume", JSONObject().put("media", vol(AudioManager.STREAM_MUSIC)).put("ring", vol(AudioManager.STREAM_RING))
                .put("alarm", vol(AudioManager.STREAM_ALARM)).put("call", vol(AudioManager.STREAM_VOICE_CALL)))
            put("network", JSONObject().apply {
                put("carrier", tm.networkOperatorName ?: "")
                put("signal", runCatching { tm.signalStrength?.level ?: 0 }.getOrDefault(0))
                put("type", runCatching { networkType(tm.dataNetworkType) }.getOrDefault(""))
                put("transport", when {
                    caps == null -> "none"
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> "wifi"
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> "cellular"
                    else -> "other"
                })
            })
            put("storage", JSONObject().put("free", stat.availableBytes).put("total", stat.totalBytes))
            put("helper", Shell.version() != null)
            put("smsApp", android.provider.Telephony.Sms.getDefaultSmsPackage(ctx) ?: "")
            put("btName", runCatching { ctx.getSystemService(BluetoothManager::class.java).adapter.name }.getOrNull() ?: "")
            put("permissions", com.cyberwolf.tandem.ui.Perms.summary(ctx))
        }
    }

    private fun networkType(t: Int) = when (t) {
        TelephonyManager.NETWORK_TYPE_NR -> "5G"
        TelephonyManager.NETWORK_TYPE_LTE -> "4G"
        TelephonyManager.NETWORK_TYPE_HSPAP, TelephonyManager.NETWORK_TYPE_HSPA, TelephonyManager.NETWORK_TYPE_UMTS -> "3G"
        TelephonyManager.NETWORK_TYPE_EDGE, TelephonyManager.NETWORK_TYPE_GPRS -> "2G"
        else -> ""
    }
}
