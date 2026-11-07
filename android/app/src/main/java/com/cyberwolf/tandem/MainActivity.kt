package com.cyberwolf.tandem

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.LinkService
import com.cyberwolf.tandem.link.Pairing
import com.cyberwolf.tandem.shell.Shell
import com.cyberwolf.tandem.ui.Perms
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private val Paper = Color(0xFFF4F1EA)
private val Ink = Color(0xFF1F2A24)
private val Muted = Color(0xFF6B6F68)
private val Amber = Color(0xFFD9A441)
private val Card = Color(0xFFFFFDF8)
private val Good = Color(0xFF3E7C59)
private val Bad = Color(0xFFB4533C)

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        LinkService.start(this)
        setContent {
            MaterialTheme(colorScheme = lightColorScheme(primary = Ink, onPrimary = Paper, background = Paper, surface = Card)) {
                Screen(
                    requestRuntime = { requestPermissions(Perms.runtime.toTypedArray(), 1) },
                    open = { startActivity(it) },
                )
            }
        }
    }
}

@Composable
private fun Screen(requestRuntime: () -> Unit, open: (Intent) -> Unit) {
    val st by Link.state.collectAsState()
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val tick = remember { mutableIntStateOf(0) }
    val owner = LocalLifecycleOwner.current
    LaunchedEffect(owner) {
        owner.repeatOnLifecycle(Lifecycle.State.RESUMED) { tick.intValue++ }
    }
    val helper by androidx.compose.runtime.produceState<String?>(null, tick.intValue, st.peer) {
        value = withContext(Dispatchers.IO) { Shell.version() }
    }
    val perms = remember(tick.intValue) { Perms.summary(ctx) }
    val missing = perms.getJSONArray("missing").length()

    Column(
        Modifier.fillMaxSize().background(Paper).safeDrawingPadding().verticalScroll(rememberScrollState())
            .padding(horizontal = 22.dp, vertical = 18.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text("Tandem", fontFamily = FontFamily.Serif, fontSize = 40.sp, color = Ink, fontWeight = FontWeight.Medium)
        Text("Your phone, on your Linux desktop.", color = Muted, fontSize = 15.sp)
        Spacer(Modifier.size(4.dp))

        st.pairing?.let { p ->
            Section("Pairing request") {
                Text("${p.name} wants to link with this phone.", color = Ink)
                Text(p.code.chunked(3).joinToString(" "), fontFamily = FontFamily.Monospace, fontSize = 38.sp,
                    color = Ink, modifier = Modifier.padding(vertical = 6.dp))
                Text("Accept only if the PC shows the same code.", color = Muted, fontSize = 13.sp)
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.padding(top = 8.dp)) {
                    Button(onClick = { Pairing.answer(true) }) { Text("Accept") }
                    OutlinedButton(onClick = { Pairing.answer(false) }) { Text("Reject") }
                }
            }
        }

        if (st.pairing == null) {
            var open by remember { mutableStateOf(Pairing.windowOpen()) }
            LaunchedEffect(open) { if (open) { kotlinx.coroutines.delay(Pairing.WINDOW_MS); open = Pairing.windowOpen() } }
            Section("Pair a new PC") {
                Text(if (open) "Pairing is open for 2 minutes. Start Tandem on the PC now."
                     else "PCs can only ask to pair while this is open. Over USB, the PC opens it for you.", color = Muted, fontSize = 13.sp)
                if (!open) Button(onClick = { Pairing.openWindow(); open = true }, modifier = Modifier.padding(top = 8.dp)) { Text("Allow pairing") }
                else OutlinedButton(onClick = { Pairing.closeWindow(); open = false }, modifier = Modifier.padding(top = 8.dp)) { Text("Stop") }
            }
        }

        Section("Connection") {
            StatusLine(st.peer != null, st.peer?.let { "Linked to $it" } ?: if (st.running) "Waiting for your PC" else "Starting…")
            StatusLine(helper != null, if (helper != null) "Shell helper running" else "Shell helper not running")
            if (helper == null) Hint("Plug in USB and open Tandem on the PC. It starts the helper, which toggles Bluetooth, hotspot and more.")
        }

        Section("Access") {
            StatusLine(missing == 0, if (missing == 0) "All permissions granted" else "$missing permissions missing")
            if (missing > 0) TextButton(onClick = requestRuntime) { Text("Grant permissions") }
            StatusLine(perms.getBoolean("notifications"), "Notification access (WhatsApp, messages)")
            if (!perms.getBoolean("notifications")) TextButton(onClick = {
                open(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
            }) { Text("Open notification access") }
            StatusLine(perms.getBoolean("dnd"), "Do Not Disturb control")
            StatusLine(perms.getBoolean("battery"), "Unrestricted battery")
            if (!perms.getBoolean("battery")) TextButton(onClick = {
                open(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:${ctx.packageName}")))
            }) { Text("Allow background use") }
            Hint("Quickest: on the PC choose Settings → Set up phone with USB connected. It grants all of this over adb.")
        }

        val trusted = remember(tick.intValue, st.peer) { Prefs.trusted }
        if (trusted.isNotEmpty()) Section("Paired computers") {
            trusted.forEach { (fp, name) ->
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                    Column(Modifier.weight(1f)) {
                        Text(name, color = Ink)
                        Text(fp.take(16).chunked(4).joinToString(" "), color = Muted, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                    }
                    TextButton(onClick = { Pairing.forget(fp); tick.intValue++ },
                        colors = ButtonDefaults.textButtonColors(contentColor = Bad)) { Text("Forget") }
                }
            }
        }
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(18.dp)).background(Card).padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(title.uppercase(), color = Amber, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.4.sp)
        content()
    }
}

@Composable
private fun StatusLine(ok: Boolean, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Spacer(Modifier.size(9.dp).clip(CircleShape).background(if (ok) Good else Bad))
        Spacer(Modifier.size(10.dp))
        Text(text, color = Ink, fontSize = 15.sp)
    }
}

@Composable
private fun Hint(text: String) = Text(text, color = Muted, fontSize = 13.sp, lineHeight = 18.sp)
