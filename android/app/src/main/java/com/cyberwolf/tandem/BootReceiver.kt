package com.cyberwolf.tandem

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.cyberwolf.tandem.link.LinkService

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        LinkService.start(ctx)
    }
}
