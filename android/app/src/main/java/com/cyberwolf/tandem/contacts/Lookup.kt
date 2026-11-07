package com.cyberwolf.tandem.contacts

import android.content.Context
import android.net.Uri
import android.provider.ContactsContract.PhoneLookup
import java.util.concurrent.ConcurrentHashMap

/** Number -> contact name, cached; invalidated when contacts change. */
object Lookup {
    private val cache = ConcurrentHashMap<String, String>()

    fun clear() = cache.clear()

    fun name(ctx: Context, number: String?): String? {
        if (number.isNullOrBlank()) return null
        cache[number]?.let { return it.ifEmpty { null } }
        val name = runCatching {
            ctx.contentResolver.query(
                Uri.withAppendedPath(PhoneLookup.CONTENT_FILTER_URI, Uri.encode(number)),
                arrayOf(PhoneLookup.DISPLAY_NAME), null, null, null
            )?.use { if (it.moveToFirst()) it.getString(0) else null }
        }.getOrNull()
        cache[number] = name ?: ""
        return name
    }

    fun contactId(ctx: Context, number: String?): Long? {
        if (number.isNullOrBlank()) return null
        return runCatching {
            ctx.contentResolver.query(
                Uri.withAppendedPath(PhoneLookup.CONTENT_FILTER_URI, Uri.encode(number)),
                arrayOf(PhoneLookup._ID), null, null, null
            )?.use { if (it.moveToFirst()) it.getLong(0) else null }
        }.getOrNull()
    }
}
