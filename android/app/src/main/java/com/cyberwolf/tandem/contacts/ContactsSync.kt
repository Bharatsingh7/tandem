package com.cyberwolf.tandem.contacts

import android.content.ContentProviderOperation
import android.content.ContentUris
import android.content.Context
import android.database.ContentObserver
import android.graphics.BitmapFactory
import android.os.Handler
import android.os.HandlerThread
import android.provider.ContactsContract
import android.provider.ContactsContract.CommonDataKinds.Email
import android.provider.ContactsContract.CommonDataKinds.Organization
import android.provider.ContactsContract.CommonDataKinds.Phone
import android.provider.ContactsContract.CommonDataKinds.Photo
import android.provider.ContactsContract.CommonDataKinds.StructuredName
import android.provider.ContactsContract.Contacts
import android.provider.ContactsContract.Data
import android.provider.ContactsContract.RawContacts
import com.cyberwolf.tandem.base64ToBytes
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import com.cyberwolf.tandem.scaledTo
import com.cyberwolf.tandem.toBase64
import org.json.JSONArray
import org.json.JSONObject

/**
 * Two-way contact sync. The PC keeps a SQLite mirror and asks for deltas since its last sync;
 * edits made on the PC are written back here through ContentProviderOperation batches.
 */
object ContactsSync {
    private lateinit var ctx: Context
    private val handler = Handler(HandlerThread("tandem-contacts").apply { start() }.looper)
    private val notify = Runnable { Lookup.clear(); Link.emit("contacts.changed", null) }

    private val phoneTypes = mapOf(
        "mobile" to Phone.TYPE_MOBILE, "home" to Phone.TYPE_HOME, "work" to Phone.TYPE_WORK,
        "main" to Phone.TYPE_MAIN, "other" to Phone.TYPE_OTHER, "fax" to Phone.TYPE_FAX_WORK, "pager" to Phone.TYPE_PAGER,
    )
    private val emailTypes = mapOf("home" to Email.TYPE_HOME, "work" to Email.TYPE_WORK, "other" to Email.TYPE_OTHER, "mobile" to Email.TYPE_MOBILE)

    fun register(c: Context) {
        ctx = c
        Router.on("contacts.sync") { a -> sync(a.optLong("since", 0)) }
        Router.on("contacts.get") { a -> load(listOf(a.getLong("id"))).firstOrNull() ?: throw Router.Failure("no such contact") }
        Router.on("contacts.photo") { a -> photo(a.getLong("id"), a.optBoolean("full")) }
        Router.on("contacts.save") { a -> save(a) }
        Router.on("contacts.delete") { a ->
            ctx.contentResolver.delete(ContentUris.withAppendedId(Contacts.CONTENT_URI, a.getLong("id")), null, null)
            null
        }
        Router.on("contacts.star") { a ->
            ctx.contentResolver.update(ContentUris.withAppendedId(Contacts.CONTENT_URI, a.getLong("id")),
                android.content.ContentValues().apply { put(Contacts.STARRED, if (a.getBoolean("on")) 1 else 0) }, null, null)
            null
        }
        Router.on("contacts.setPhoto") { a -> setPhoto(a.getLong("id"), base64ToBytes(a.getString("jpeg"))); null }

        runCatching {
            c.contentResolver.registerContentObserver(Contacts.CONTENT_URI, true, object : ContentObserver(handler) {
                override fun onChange(selfChange: Boolean) {
                    handler.removeCallbacks(notify)
                    handler.postDelayed(notify, 1500)
                }
            })
        }
    }

    private fun sync(since: Long): JSONObject {
        val now = System.currentTimeMillis()
        val ids = ArrayList<Long>()
        val changed = ArrayList<Long>()
        ctx.contentResolver.query(Contacts.CONTENT_URI, arrayOf(Contacts._ID, Contacts.CONTACT_LAST_UPDATED_TIMESTAMP), null, null, null)?.use { c ->
            while (c.moveToNext()) {
                ids += c.getLong(0)
                if (c.getLong(1) >= since) changed += c.getLong(0)
            }
        }
        return JSONObject().put("now", now).put("ids", JSONArray(ids)).put("changed", JSONArray(load(changed)))
    }

    /** Builds full contact records for [ids] with two queries regardless of how many there are. */
    private fun load(ids: List<Long>): List<JSONObject> {
        if (ids.isEmpty()) return emptyList()
        val byId = LinkedHashMap<Long, JSONObject>()
        ids.chunked(500).forEach { chunk ->
            val sel = "${Contacts._ID} IN (${chunk.joinToString(",")})"
            ctx.contentResolver.query(Contacts.CONTENT_URI,
                arrayOf(Contacts._ID, Contacts.DISPLAY_NAME_PRIMARY, Contacts.STARRED, Contacts.CONTACT_LAST_UPDATED_TIMESTAMP,
                    Contacts.PHOTO_ID, Contacts.LOOKUP_KEY, Contacts.TIMES_CONTACTED), sel, null, null)?.use { c ->
                while (c.moveToNext()) {
                    byId[c.getLong(0)] = JSONObject().put("id", c.getLong(0)).put("name", c.getString(1) ?: "")
                        .put("starred", c.getInt(2) == 1).put("updated", c.getLong(3)).put("hasPhoto", !c.isNull(4) && c.getLong(4) > 0)
                        .put("lookup", c.getString(5) ?: "")
                        .put("given", "").put("family", "").put("org", "").put("title", "")
                        .put("phones", JSONArray()).put("emails", JSONArray())
                }
            }
            val dsel = "${Data.CONTACT_ID} IN (${chunk.joinToString(",")}) AND ${Data.MIMETYPE} IN (?,?,?,?)"
            ctx.contentResolver.query(Data.CONTENT_URI,
                arrayOf(Data.CONTACT_ID, Data.MIMETYPE, Data.DATA1, Data.DATA2, Data.DATA3, Data.DATA4, Data.DATA5),
                dsel, arrayOf(StructuredName.CONTENT_ITEM_TYPE, Phone.CONTENT_ITEM_TYPE, Email.CONTENT_ITEM_TYPE, Organization.CONTENT_ITEM_TYPE),
                null)?.use { c ->
                while (c.moveToNext()) {
                    val o = byId[c.getLong(0)] ?: continue
                    when (c.getString(1)) {
                        StructuredName.CONTENT_ITEM_TYPE -> {
                            // DATA2 = given, DATA3 = family
                            if (o.getString("given").isEmpty()) o.put("given", c.getString(3) ?: "")
                            if (o.getString("family").isEmpty()) o.put("family", c.getString(4) ?: "")
                        }
                        Phone.CONTENT_ITEM_TYPE -> {
                            val n = c.getString(2) ?: continue
                            val arr = o.getJSONArray("phones")
                            if ((0 until arr.length()).none { arr.getJSONObject(it).getString("n").filter(Char::isDigit) == n.filter(Char::isDigit) })
                                arr.put(JSONObject().put("n", n).put("type", label(phoneTypes, c.getInt(3), c.getString(4))))
                        }
                        Email.CONTENT_ITEM_TYPE -> {
                            val e = c.getString(2) ?: continue
                            o.getJSONArray("emails").put(JSONObject().put("a", e).put("type", label(emailTypes, c.getInt(3), c.getString(4))))
                        }
                        Organization.CONTENT_ITEM_TYPE -> {
                            // DATA1 = company, DATA4 = title
                            if (o.getString("org").isEmpty()) o.put("org", c.getString(2) ?: "")
                            if (o.getString("title").isEmpty()) o.put("title", c.getString(5) ?: "")
                        }
                    }
                }
            }
        }
        return byId.values.toList()
    }

    private fun label(types: Map<String, Int>, type: Int, custom: String?) =
        if (type == 0 && !custom.isNullOrBlank()) custom else types.entries.firstOrNull { it.value == type }?.key ?: "other"

    private fun photo(id: Long, full: Boolean): JSONObject {
        val uri = ContentUris.withAppendedId(Contacts.CONTENT_URI, id)
        val bytes = Contacts.openContactPhotoInputStream(ctx.contentResolver, uri, full)?.use { it.readBytes() }
            ?: return JSONObject().put("id", id).put("jpeg", JSONObject.NULL)
        val bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return JSONObject().put("id", id).put("jpeg", JSONObject.NULL)
        return JSONObject().put("id", id).put("jpeg", bmp.scaledTo(if (full) 720 else 160).toBase64())
    }

    /** First writable raw contact behind a contact; edits go there. */
    private fun rawContactId(contactId: Long): Long? =
        ctx.contentResolver.query(RawContacts.CONTENT_URI, arrayOf(RawContacts._ID, RawContacts.RAW_CONTACT_IS_READ_ONLY),
            "${RawContacts.CONTACT_ID} = ? AND ${RawContacts.DELETED} = 0", arrayOf(contactId.toString()), null)?.use { c ->
            var fallback: Long? = null
            while (c.moveToNext()) {
                if (c.getInt(1) == 0) return@use c.getLong(0)
                if (fallback == null) fallback = c.getLong(0)
            }
            fallback
        }

    private fun save(a: JSONObject): JSONObject {
        val ops = ArrayList<ContentProviderOperation>()
        val existing = a.optLong("id", -1).takeIf { it > 0 }
        val raw = existing?.let { rawContactId(it) ?: throw Router.Failure("contact not found") }
        val ref: (ContentProviderOperation.Builder) -> ContentProviderOperation.Builder
        if (raw == null) {
            ops += ContentProviderOperation.newInsert(RawContacts.CONTENT_URI)
                .withValue(RawContacts.ACCOUNT_TYPE, null).withValue(RawContacts.ACCOUNT_NAME, null).build()
            ref = { it.withValueBackReference(Data.RAW_CONTACT_ID, 0) }
        } else {
            ops += ContentProviderOperation.newDelete(Data.CONTENT_URI)
                .withSelection("${Data.RAW_CONTACT_ID} = ? AND ${Data.MIMETYPE} IN (?,?,?,?)",
                    arrayOf(raw.toString(), StructuredName.CONTENT_ITEM_TYPE, Phone.CONTENT_ITEM_TYPE, Email.CONTENT_ITEM_TYPE, Organization.CONTENT_ITEM_TYPE))
                .build()
            ref = { it.withValue(Data.RAW_CONTACT_ID, raw) }
        }
        val given = a.optString("given").trim()
        val family = a.optString("family").trim()
        val display = a.optString("name").trim().ifEmpty { "$given $family".trim() }
        if (display.isEmpty()) throw Router.Failure("a contact needs a name")
        ops += ref(ContentProviderOperation.newInsert(Data.CONTENT_URI))
            .withValue(Data.MIMETYPE, StructuredName.CONTENT_ITEM_TYPE)
            .withValue(StructuredName.DISPLAY_NAME, display)
            .withValue(StructuredName.GIVEN_NAME, given.ifEmpty { null })
            .withValue(StructuredName.FAMILY_NAME, family.ifEmpty { null })
            .build()
        a.optJSONArray("phones")?.let { arr ->
            for (i in 0 until arr.length()) {
                val p = arr.getJSONObject(i)
                val n = p.optString("n").trim().ifEmpty { continue }
                val t = p.optString("type", "mobile")
                ops += ref(ContentProviderOperation.newInsert(Data.CONTENT_URI))
                    .withValue(Data.MIMETYPE, Phone.CONTENT_ITEM_TYPE).withValue(Phone.NUMBER, n)
                    .withValue(Phone.TYPE, phoneTypes[t] ?: Phone.TYPE_CUSTOM)
                    .withValue(Phone.LABEL, if (phoneTypes.containsKey(t)) null else t).build()
            }
        }
        a.optJSONArray("emails")?.let { arr ->
            for (i in 0 until arr.length()) {
                val e = arr.getJSONObject(i)
                val addr = e.optString("a").trim().ifEmpty { continue }
                val t = e.optString("type", "home")
                ops += ref(ContentProviderOperation.newInsert(Data.CONTENT_URI))
                    .withValue(Data.MIMETYPE, Email.CONTENT_ITEM_TYPE).withValue(Email.ADDRESS, addr)
                    .withValue(Email.TYPE, emailTypes[t] ?: Email.TYPE_CUSTOM)
                    .withValue(Email.LABEL, if (emailTypes.containsKey(t)) null else t).build()
            }
        }
        val org = a.optString("org").trim()
        val title = a.optString("title").trim()
        if (org.isNotEmpty() || title.isNotEmpty()) {
            ops += ref(ContentProviderOperation.newInsert(Data.CONTENT_URI))
                .withValue(Data.MIMETYPE, Organization.CONTENT_ITEM_TYPE)
                .withValue(Organization.COMPANY, org.ifEmpty { null }).withValue(Organization.TITLE, title.ifEmpty { null }).build()
        }
        val res = ctx.contentResolver.applyBatch(ContactsContract.AUTHORITY, ops)
        val rawId = raw ?: ContentUris.parseId(res[0].uri!!)
        val contactId = ctx.contentResolver.query(ContentUris.withAppendedId(RawContacts.CONTENT_URI, rawId),
            arrayOf(RawContacts.CONTACT_ID), null, null, null)?.use { if (it.moveToFirst()) it.getLong(0) else null }
            ?: throw Router.Failure("saved, but the contact id is unknown")
        if (a.has("starred")) {
            ctx.contentResolver.update(ContentUris.withAppendedId(Contacts.CONTENT_URI, contactId),
                android.content.ContentValues().apply { put(Contacts.STARRED, if (a.getBoolean("starred")) 1 else 0) }, null, null)
        }
        a.optString("jpeg").takeIf { it.isNotEmpty() }?.let { setPhoto(contactId, base64ToBytes(it)) }
        Lookup.clear()
        return load(listOf(contactId)).first()
    }

    private fun setPhoto(contactId: Long, jpeg: ByteArray) {
        val raw = rawContactId(contactId) ?: throw Router.Failure("contact not found")
        val ops = arrayListOf(
            ContentProviderOperation.newDelete(Data.CONTENT_URI)
                .withSelection("${Data.RAW_CONTACT_ID} = ? AND ${Data.MIMETYPE} = ?", arrayOf(raw.toString(), Photo.CONTENT_ITEM_TYPE)).build(),
            ContentProviderOperation.newInsert(Data.CONTENT_URI)
                .withValue(Data.RAW_CONTACT_ID, raw).withValue(Data.MIMETYPE, Photo.CONTENT_ITEM_TYPE)
                .withValue(Photo.PHOTO, jpeg).build(),
        )
        ctx.contentResolver.applyBatch(ContactsContract.AUTHORITY, ops)
    }

}
