package com.cyberwolf.tandem

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import com.cyberwolf.tandem.calls.Calls
import com.cyberwolf.tandem.clip.Clip
import com.cyberwolf.tandem.contacts.ContactsSync
import com.cyberwolf.tandem.files.Files
import com.cyberwolf.tandem.link.Identity
import com.cyberwolf.tandem.media.MediaBridge
import com.cyberwolf.tandem.notify.Notifs
import com.cyberwolf.tandem.shell.Toggles
import com.cyberwolf.tandem.sms.Sms
import com.cyberwolf.tandem.status.Status

class TandemApp : Application() {
    override fun onCreate() {
        super.onCreate()
        app = this
        Prefs.init(this)
        Identity.ensure()
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CH_LINK, "Connection", NotificationManager.IMPORTANCE_MIN))
        nm.createNotificationChannel(NotificationChannel(CH_PAIR, "Pairing requests", NotificationManager.IMPORTANCE_HIGH))
        nm.createNotificationChannel(NotificationChannel(CH_FILES, "File transfers", NotificationManager.IMPORTANCE_LOW))

        Status.register(this)
        Toggles.register(this)
        Notifs.register(this)
        Sms.register(this)
        Calls.register(this)
        ContactsSync.register(this)
        MediaBridge.register(this)
        Clip.register(this)
        Files.register(this)
    }

    companion object {
        lateinit var app: TandemApp
            private set
        const val CH_LINK = "link"
        const val CH_PAIR = "pair"
        const val CH_FILES = "files"
    }
}
