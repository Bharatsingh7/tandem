package com.cyberwolf.tandem

import android.content.Context

object BuildConfigCompat {
    /** Changes on every install, so a helper started from an older APK is detected and replaced. */
    fun versionCode(c: Context): Long = c.packageManager.getPackageInfo(c.packageName, 0).lastUpdateTime
}
