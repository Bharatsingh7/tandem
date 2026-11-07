package com.cyberwolf.tandem.link

import org.json.JSONObject
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException

/** Wire format: 4-byte big-endian length, then a UTF-8 JSON object. Shared with desktop/tandem/link.py. */
object Frames {
    const val MAX = 24 shl 20

    fun write(out: DataOutputStream, obj: JSONObject) {
        val b = obj.toString().toByteArray(Charsets.UTF_8)
        out.writeInt(b.size)
        out.write(b)
        out.flush()
    }

    fun read(inp: DataInputStream, max: Int = MAX): JSONObject {
        val n = inp.readInt()
        if (n < 0 || n > max) throw IOException("bad frame length $n")
        val b = ByteArray(n)
        inp.readFully(b)
        return JSONObject(String(b, Charsets.UTF_8))
    }
}
