package com.cyberwolf.tandem

import android.content.Context
import android.content.SharedPreferences
import java.security.SecureRandom

object Prefs {
    private lateinit var sp: SharedPreferences

    fun init(ctx: Context) {
        sp = ctx.getSharedPreferences("tandem", Context.MODE_PRIVATE)
    }

    /** PC certificate fingerprint -> PC name. */
    var trusted: Map<String, String>
        get() = sp.getStringSet("trusted", emptySet())!!.associate { it.substringBefore('|') to it.substringAfter('|') }
        set(v) = sp.edit().putStringSet("trusted", v.map { "${it.key}|${it.value}" }.toSet()).apply()

    /** Secret the shell helper requires before it runs anything. */
    val shellToken: String
        get() = sp.getString("shellToken", null) ?: ByteArray(24).also { SecureRandom().nextBytes(it) }
            .joinToString("") { "%02x".format(it) }.also { sp.edit().putString("shellToken", it).apply() }
}
