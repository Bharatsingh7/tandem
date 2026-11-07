package com.cyberwolf.tandem.shell

import android.app.NotificationManager
import android.content.Context
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.Ringtone
import android.media.RingtoneManager
import com.cyberwolf.tandem.BuildConfigCompat
import com.cyberwolf.tandem.Prefs
import com.cyberwolf.tandem.link.Router
import com.cyberwolf.tandem.status.Status
import org.json.JSONObject

/** Quick settings driven from the PC. App APIs where Android allows it, the shell helper otherwise. */
object Toggles {
    private lateinit var ctx: Context
    private var ringtone: Ringtone? = null
    private var savedAlarmVolume = -1

    fun register(c: Context) {
        ctx = c
        Router.on("shell.info") {
            JSONObject().put("apk", c.applicationInfo.sourceDir).put("token", Prefs.shellToken)
                .put("version", BuildConfigCompat.versionCode(c).toString()).put("running", Shell.version())
        }
        Router.on("toggle.set") { a -> set(a.getString("name"), a.getBoolean("on")); Status.snapshot() }
        Router.on("ringer.set") { a -> ringer(a.getString("mode")); null }
        Router.on("volume.set") { a ->
            val stream = when (a.getString("stream")) {
                "ring" -> AudioManager.STREAM_RING
                "alarm" -> AudioManager.STREAM_ALARM
                "call" -> AudioManager.STREAM_VOICE_CALL
                else -> AudioManager.STREAM_MUSIC
            }
            c.getSystemService(AudioManager::class.java).setStreamVolume(stream, a.getInt("v"), 0)
            Status.changed()
            null
        }
        Router.on("brightness.set") { a ->
            if (a.optBoolean("auto")) Shell.exec("settings put system screen_brightness_mode 1")
            else Shell.exec("settings put system screen_brightness_mode 0; settings put system screen_brightness ${a.getInt("v").coerceIn(1, 255)}")
            null
        }
        Router.on("bt.pair") { a ->
            val bm = c.getSystemService(android.bluetooth.BluetoothManager::class.java)
            val adapter = bm.adapter ?: throw Router.Failure("no Bluetooth")
            if (!adapter.isEnabled) throw Router.Failure("Bluetooth is off")
            val dev = adapter.getRemoteDevice(a.getString("address").uppercase())
            JSONObject().put("bonded", dev.bondState == android.bluetooth.BluetoothDevice.BOND_BONDED)
                .put("started", dev.bondState != android.bluetooth.BluetoothDevice.BOND_BONDED && dev.createBond())
        }
        Router.on("bt.bonded") { a ->
            val adapter = c.getSystemService(android.bluetooth.BluetoothManager::class.java).adapter
            val dev = adapter?.bondedDevices?.firstOrNull { it.address.equals(a.getString("address"), true) }
            JSONObject().put("bonded", dev != null).put("name", runCatching { adapter?.name }.getOrNull() ?: "")
        }
        Router.on("phone.ring") { a -> ring(a.optBoolean("on", true)); null }
        Router.on("phone.lock") { Shell.exec("input keyevent 223"); null }
        Router.on("phone.wake") { Shell.exec("input keyevent 224"); null }
        Router.on("phone.openUrl") { a ->
            val url = a.getString("url")
            require(url.startsWith("http://") || url.startsWith("https://")) { "only web links" }
            Shell.exec("am start -a android.intent.action.VIEW -d '${url.replace("'", "%27")}'")
            null
        }
    }

    fun set(name: String, on: Boolean) {
        when (name) {
            "bluetooth" -> Shell.exec("cmd bluetooth_manager ${if (on) "enable" else "disable"}")
            "wifi" -> Shell.exec("cmd wifi set-wifi-enabled ${if (on) "enabled" else "disabled"}")
            "hotspot" -> Shell.call(JSONObject().put("cmd", "hotspot").put("on", on))
            "data" -> Shell.exec("cmd phone data ${if (on) "enable" else "disable"}")
            "airplane" -> Shell.exec("cmd connectivity airplane-mode ${if (on) "enable" else "disable"}")
            "location" -> Shell.exec("cmd location set-location-enabled $on")
            "dnd" -> {
                val nm = ctx.getSystemService(NotificationManager::class.java)
                if (nm.isNotificationPolicyAccessGranted) {
                    nm.setInterruptionFilter(if (on) NotificationManager.INTERRUPTION_FILTER_PRIORITY else NotificationManager.INTERRUPTION_FILTER_ALL)
                } else Shell.exec("cmd notification set_dnd ${if (on) "priority" else "off"}")
            }
            "torch" -> {
                val cm = ctx.getSystemService(CameraManager::class.java)
                val id = cm.cameraIdList.first { cm.getCameraCharacteristics(it).get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true }
                cm.setTorchMode(id, on)
            }
            else -> throw Router.Failure("unknown toggle $name")
        }
        Status.changed()
    }

    private fun ringer(mode: String) {
        val am = ctx.getSystemService(AudioManager::class.java)
        am.ringerMode = when (mode) {
            "silent" -> AudioManager.RINGER_MODE_SILENT
            "vibrate" -> AudioManager.RINGER_MODE_VIBRATE
            else -> AudioManager.RINGER_MODE_NORMAL
        }
    }

    /** "Find my phone": alarm-stream ringtone at full volume, rings through silent mode. */
    @Synchronized
    private fun ring(on: Boolean) {
        val am = ctx.getSystemService(AudioManager::class.java)
        ringtone?.stop()
        ringtone = null
        if (savedAlarmVolume >= 0) {
            am.setStreamVolume(AudioManager.STREAM_ALARM, savedAlarmVolume, 0)
            savedAlarmVolume = -1
        }
        if (!on) return
        savedAlarmVolume = am.getStreamVolume(AudioManager.STREAM_ALARM)
        am.setStreamVolume(AudioManager.STREAM_ALARM, am.getStreamMaxVolume(AudioManager.STREAM_ALARM), 0)
        val uri = RingtoneManager.getActualDefaultRingtoneUri(ctx, RingtoneManager.TYPE_RINGTONE)
            ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
        ringtone = RingtoneManager.getRingtone(ctx, uri)?.apply {
            audioAttributes = AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build()
            isLooping = true
            play()
        }
        Status.handler.postDelayed({ ring(false) }, 45_000)
    }
}
