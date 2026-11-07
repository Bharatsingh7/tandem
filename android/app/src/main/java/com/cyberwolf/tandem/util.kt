package com.cyberwolf.tandem

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.util.Base64
import java.io.ByteArrayOutputStream

fun Bitmap.toBase64(format: Bitmap.CompressFormat = Bitmap.CompressFormat.JPEG, quality: Int = 85): String {
    val out = ByteArrayOutputStream()
    compress(format, quality, out)
    return Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP)
}

fun Drawable.toBitmap(size: Int): Bitmap {
    if (this is BitmapDrawable && bitmap != null && bitmap.width <= size) return bitmap
    val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
    setBounds(0, 0, size, size)
    draw(Canvas(bmp))
    return bmp
}

fun Bitmap.scaledTo(max: Int): Bitmap {
    val s = max.toFloat() / maxOf(width, height)
    return if (s >= 1f) this else Bitmap.createScaledBitmap(this, (width * s).toInt().coerceAtLeast(1), (height * s).toInt().coerceAtLeast(1), true)
}

fun bytesToBase64(b: ByteArray): String = Base64.encodeToString(b, Base64.NO_WRAP)
fun base64ToBytes(s: String): ByteArray = Base64.decode(s, Base64.DEFAULT)

/** "Motorola Edge 60 Pro" rather than "motorola motorola edge 60 pro". */
fun deviceName(): String {
    val model = android.os.Build.MODEL
    val maker = android.os.Build.MANUFACTURER
    val raw = if (model.startsWith(maker, ignoreCase = true)) model else "$maker $model"
    return raw.split(' ').joinToString(" ") { w -> w.replaceFirstChar { it.uppercase() } }
}
