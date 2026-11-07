package com.cyberwolf.tandem.ui

import android.Manifest
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.os.PowerManager
import android.provider.Settings
import com.cyberwolf.tandem.notify.NotificationMirror
import org.json.JSONObject

/** Everything Tandem needs granted. The PC's setup step grants all of these over adb in one go. */
object Perms {
    val runtime = listOf(
        Manifest.permission.POST_NOTIFICATIONS,
        Manifest.permission.READ_PHONE_STATE,
        Manifest.permission.READ_PHONE_NUMBERS,
        Manifest.permission.CALL_PHONE,
        Manifest.permission.ANSWER_PHONE_CALLS,
        Manifest.permission.READ_CALL_LOG,
        Manifest.permission.WRITE_CALL_LOG,
        Manifest.permission.READ_CONTACTS,
        Manifest.permission.WRITE_CONTACTS,
        Manifest.permission.READ_SMS,
        Manifest.permission.SEND_SMS,
        Manifest.permission.RECEIVE_SMS,
        Manifest.permission.RECEIVE_MMS,
        Manifest.permission.READ_MEDIA_IMAGES,
        Manifest.permission.READ_MEDIA_VIDEO,
        Manifest.permission.BLUETOOTH_CONNECT,
    )

    fun granted(c: Context, p: String) = c.checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED

    fun listenerEnabled(c: Context): Boolean {
        val flat = Settings.Secure.getString(c.contentResolver, "enabled_notification_listeners") ?: return false
        val me = ComponentName(c, NotificationMirror::class.java)
        return flat.split(':').any { ComponentName.unflattenFromString(it) == me }
    }

    fun summary(c: Context): JSONObject = JSONObject().apply {
        put("missing", org.json.JSONArray(runtime.filterNot { granted(c, it) }.map { it.substringAfterLast('.') }))
        put("notifications", listenerEnabled(c))
        put("dnd", c.getSystemService(NotificationManager::class.java).isNotificationPolicyAccessGranted)
        put("battery", c.getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(c.packageName))
    }
}
