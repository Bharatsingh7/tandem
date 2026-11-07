package com.cyberwolf.tandem.calls

import android.bluetooth.BluetoothDevice
import android.content.Context
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.CallLog
import android.telecom.Call
import android.telecom.CallAudioState
import android.telecom.DisconnectCause
import android.telecom.InCallService
import android.telecom.PhoneAccountHandle
import android.telecom.TelecomManager
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import com.cyberwolf.tandem.contacts.Lookup
import com.cyberwolf.tandem.link.Link
import com.cyberwolf.tandem.link.Router
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.FutureTask
import java.util.concurrent.TimeUnit

/**
 * Bound by Telecom because the shell helper registers us as a call companion
 * (`telecom add-or-remove-call-companion-app`). Gives full control over every call.
 */
class CompanionCallService : InCallService() {
    override fun onCreate() {
        super.onCreate()
        Calls.service = this
    }

    override fun onDestroy() {
        if (Calls.service === this) Calls.service = null
        super.onDestroy()
    }

    override fun onCallAdded(call: Call) = Calls.added(call)
    override fun onCallRemoved(call: Call) = Calls.removed(call)
    @Deprecated("Deprecated in Java")
    override fun onCallAudioStateChanged(audioState: CallAudioState) = Calls.audioChanged()
}

object Calls {
    @Volatile var service: CompanionCallService? = null
    private lateinit var ctx: Context
    private val main = Handler(Looper.getMainLooper())
    private val calls = LinkedHashMap<Call, Int>()
    private var nextId = 1
    /** Basic state from TelephonyCallback, used only when the companion service isn't bound. */
    @Volatile private var basicState = TelephonyManager.CALL_STATE_IDLE
    /** The PC's Bluetooth address; calls answered or dialed from the PC route their audio there. */
    @Volatile private var pcBt: String? = null
    private val viaPc = HashSet<Int>()
    private var dialViaPc = false

    private val callback = object : Call.Callback() {
        override fun onStateChanged(call: Call, state: Int) {
            applyRoute(call)
            push()
        }
        override fun onDetailsChanged(call: Call, details: Call.Details) = push()
        override fun onConferenceableCallsChanged(call: Call, c: MutableList<Call>) = push()
    }

    fun register(c: Context) {
        ctx = c
        pcBt = c.getSharedPreferences("tandem", Context.MODE_PRIVATE).getString("pcBt", null)
        Router.on("call.prefs") { a ->
            pcBt = a.optString("pcBt").ifBlank { null }
            c.getSharedPreferences("tandem", Context.MODE_PRIVATE).edit().putString("pcBt", pcBt).apply()
            null
        }
        Router.on("call.list") { onMain { snapshot() } }
        Router.on("call.answer") { a -> onMain {
            val call = find(a)
            if (call != null) {
                if (a.optBoolean("pc", true)) calls[call]?.let { viaPc += it }
                call.answer(0)
            } else tm().acceptRingingCall()
        }; null }
        Router.on("call.reject") { a -> onMain {
            val call = find(a)
            val text = a.optString("text").ifBlank { null }
            if (call != null) call.reject(text != null, text) else tm().endCall()
        }; null }
        Router.on("call.end") { a -> onMain { find(a)?.disconnect() ?: tm().endCall() }; null }
        Router.on("call.hold") { a -> onMain { find(a)?.let { if (a.getBoolean("on")) it.hold() else it.unhold() } }; null }
        Router.on("call.swap") { onMain {
            calls.keys.firstOrNull { it.details.state == Call.STATE_HOLDING }?.unhold()
        }; null }
        Router.on("call.merge") { a -> onMain { find(a)?.let { c -> c.conferenceableCalls.firstOrNull()?.let { c.conference(it) } ?: c.mergeConference() } }; null }
        Router.on("call.dtmf") { a -> onMain {
            val call = find(a) ?: throw Router.Failure("no active call")
            a.getString("digits").forEach { d -> call.playDtmfTone(d); call.stopDtmfTone() }
        }; null }
        Router.on("call.mute") { a -> onMain { svc().setMuted(a.getBoolean("on")) }; null }
        Router.on("call.route") { a -> onMain {
            val to = a.getString("to")
            find(a)?.let { calls[it] }?.let { id -> if (to == "pc") viaPc += id else viaPc -= id }
            route(to, a.optString("device"))
        }; null }
        Router.on("call.dial") { a ->
            onMain { dialViaPc = a.optBoolean("pc", true) }
            dial(a.getString("number"), a.optString("account"))
            null
        }
        Router.on("call.accounts") { accounts() }
        Router.on("call.log") { a -> log(a.optInt("limit", 300), a.optLong("since", 0)) }
        Router.on("call.logDelete") { a ->
            ctx.contentResolver.delete(CallLog.Calls.CONTENT_URI, "${CallLog.Calls._ID} = ?", arrayOf(a.getLong("id").toString()))
            null
        }

        runCatching {
            ctx.getSystemService(TelephonyManager::class.java).registerTelephonyCallback(
                { main.post(it) },
                object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                    override fun onCallStateChanged(state: Int) {
                        val wasIdle = basicState == TelephonyManager.CALL_STATE_IDLE
                        basicState = state
                        if (service == null) push()
                        if (state == TelephonyManager.CALL_STATE_IDLE && !wasIdle) main.postDelayed({ Link.emit("call.logChanged", null) }, 1500)
                    }
                })
        }
    }

    private fun tm() = ctx.getSystemService(TelecomManager::class.java)
    private fun svc() = service ?: throw Router.Failure("call companion not active; start the helper from the PC")

    private fun <T> onMain(block: () -> T): T {
        if (Looper.myLooper() == Looper.getMainLooper()) return block()
        val task = FutureTask(block)
        main.post(task)
        return try {
            task.get(10, TimeUnit.SECONDS)
        } catch (e: java.util.concurrent.ExecutionException) {
            throw e.cause ?: e
        }
    }

    private fun find(a: JSONObject): Call? {
        if (a.has("id")) return calls.entries.firstOrNull { it.value == a.getInt("id") }?.key
        return calls.keys.firstOrNull { it.details.state == Call.STATE_RINGING }
            ?: calls.keys.firstOrNull { it.details.state == Call.STATE_ACTIVE }
            ?: calls.keys.firstOrNull()
    }

    fun added(call: Call) {
        val id = nextId++
        calls[call] = id
        if (call.details.callDirection == Call.Details.DIRECTION_OUTGOING && dialViaPc) {
            viaPc += id
            dialViaPc = false
        }
        call.registerCallback(callback)
        applyRoute(call)
        push()
    }

    fun audioChanged() {
        calls.keys.forEach { applyRoute(it) }
        push()
    }

    /** Audio follows the device that took the call: the PC for PC-handled calls, the phone otherwise. */
    @Suppress("DEPRECATION")
    private fun applyRoute(call: Call) {
        val s = service ?: return
        val id = calls[call] ?: return
        val st = call.details.state
        if (st != Call.STATE_ACTIVE && st != Call.STATE_DIALING) return
        val audio = s.callAudioState ?: return
        val pc = audio.supportedBluetoothDevices.firstOrNull { it.address.equals(pcBt, true) } ?: return
        val onPc = audio.route == CallAudioState.ROUTE_BLUETOOTH && audio.activeBluetoothDevice?.address.equals(pcBt, true)
        if (id in viaPc && !onPc) s.requestBluetoothAudio(pc)
        else if (id !in viaPc && onPc) s.setAudioRoute(CallAudioState.ROUTE_WIRED_OR_EARPIECE)
    }

    fun removed(call: Call) {
        val id = calls[call]
        id?.let { viaPc -= it }
        call.unregisterCallback(callback)
        push(ended = call to (id ?: 0))
        calls.remove(call)
        push()
        main.postDelayed({ Link.emit("call.logChanged", null) }, 1500)
    }

    fun push(ended: Pair<Call, Int>? = null) {
        val snap = snapshot()
        ended?.let { (call, id) ->
            snap.put("ended", callJson(call, id))
        }
        Link.emit("call.state", snap)
    }

    private fun stateName(s: Int) = when (s) {
        Call.STATE_RINGING -> "ringing"
        Call.STATE_DIALING, Call.STATE_CONNECTING, Call.STATE_PULLING_CALL -> "dialing"
        Call.STATE_ACTIVE -> "active"
        Call.STATE_HOLDING -> "holding"
        Call.STATE_DISCONNECTING, Call.STATE_DISCONNECTED -> "ended"
        Call.STATE_SELECT_PHONE_ACCOUNT -> "select-sim"
        else -> "new"
    }

    private fun callJson(call: Call, id: Int): JSONObject {
        val d = call.details
        val number = d.handle?.schemeSpecificPart ?: ""
        return JSONObject().put("id", id).put("number", number)
            .put("name", d.contactDisplayName ?: d.callerDisplayName ?: Lookup.name(ctx, number) ?: "")
            .put("contactId", Lookup.contactId(ctx, number) ?: -1)
            .put("state", stateName(call.details.state))
            .put("incoming", d.callDirection == Call.Details.DIRECTION_INCOMING)
            .put("since", d.connectTimeMillis)
            .put("created", d.creationTimeMillis)
            .put("canHold", d.can(Call.Details.CAPABILITY_HOLD))
            .put("conference", d.hasProperty(Call.Details.PROPERTY_CONFERENCE))
            .put("missed", d.disconnectCause?.code == DisconnectCause.MISSED)
            .put("rejected", d.disconnectCause?.code == DisconnectCause.REJECTED)
            .put("account", d.accountHandle?.id ?: "")
    }

    @Suppress("DEPRECATION")
    private fun snapshot(): JSONObject {
        val o = JSONObject().put("companion", service != null)
        val arr = JSONArray()
        if (service != null) {
            calls.forEach { (c, id) -> if (c.details.state != Call.STATE_DISCONNECTED) arr.put(callJson(c, id)) }
            service?.callAudioState?.let { a ->
                o.put("audio", JSONObject()
                    .put("route", when (a.route) {
                        CallAudioState.ROUTE_BLUETOOTH -> "bluetooth"
                        CallAudioState.ROUTE_SPEAKER -> "speaker"
                        CallAudioState.ROUTE_WIRED_HEADSET -> "wired"
                        CallAudioState.ROUTE_STREAMING -> "streaming"
                        else -> "earpiece"
                    })
                    .put("muted", a.isMuted)
                    .put("device", runCatching { a.activeBluetoothDevice?.address }.getOrNull() ?: "")
                    .put("onPc", a.route == CallAudioState.ROUTE_BLUETOOTH && runCatching { a.activeBluetoothDevice?.address }.getOrNull().equals(pcBt, true))
                    .put("pcAvailable", runCatching { a.supportedBluetoothDevices.any { it.address.equals(pcBt, true) } }.getOrDefault(false))
                    .put("devices", JSONArray().apply {
                        runCatching { a.supportedBluetoothDevices.forEach { put(JSONObject().put("address", it.address).put("name", it.name ?: "")) } }
                    }))
            }
        } else if (basicState != TelephonyManager.CALL_STATE_IDLE) {
            arr.put(JSONObject().put("id", 0).put("number", "").put("name", "").put("contactId", -1)
                .put("state", if (basicState == TelephonyManager.CALL_STATE_RINGING) "ringing" else "active")
                .put("incoming", true).put("since", 0).put("canHold", false))
        }
        return o.put("calls", arr)
    }

    @Suppress("DEPRECATION")
    private fun route(to: String, device: String?) {
        val s = svc()
        when (to) {
            "pc", "bluetooth" -> {
                val target = if (to == "pc") pcBt else device
                val devs = s.callAudioState?.supportedBluetoothDevices.orEmpty()
                val dev: BluetoothDevice? = devs.firstOrNull { !target.isNullOrBlank() && it.address.equals(target, true) }
                    ?: if (to == "pc") throw Router.Failure("PC isn't connected to the phone over Bluetooth") else devs.firstOrNull()
                if (dev != null) s.requestBluetoothAudio(dev) else s.setAudioRoute(CallAudioState.ROUTE_BLUETOOTH)
            }
            "speaker" -> s.setAudioRoute(CallAudioState.ROUTE_SPEAKER)
            else -> s.setAudioRoute(CallAudioState.ROUTE_WIRED_OR_EARPIECE)
        }
    }

    private fun dial(number: String, account: String?) {
        val extras = Bundle()
        if (!account.isNullOrBlank()) {
            tm().callCapablePhoneAccounts.firstOrNull { it.id == account }?.let {
                extras.putParcelable(TelecomManager.EXTRA_PHONE_ACCOUNT_HANDLE, it)
            }
        }
        tm().placeCall(Uri.fromParts("tel", number, null), extras)
    }

    private fun accounts(): JSONArray {
        val arr = JSONArray()
        val t = tm()
        val def: PhoneAccountHandle? = runCatching { t.getDefaultOutgoingPhoneAccount("tel") }.getOrNull()
        for (h in t.callCapablePhoneAccounts) {
            val acc = t.getPhoneAccount(h) ?: continue
            arr.put(JSONObject().put("id", h.id).put("label", acc.label?.toString() ?: "SIM")
                .put("number", acc.address?.schemeSpecificPart ?: "").put("default", h == def))
        }
        return arr
    }

    private fun log(limit: Int, since: Long): JSONArray {
        val arr = JSONArray()
        ctx.contentResolver.query(
            CallLog.Calls.CONTENT_URI.buildUpon().appendQueryParameter(CallLog.Calls.LIMIT_PARAM_KEY, limit.toString()).build(),
            arrayOf(CallLog.Calls._ID, CallLog.Calls.NUMBER, CallLog.Calls.CACHED_NAME, CallLog.Calls.TYPE, CallLog.Calls.DATE,
                CallLog.Calls.DURATION, CallLog.Calls.PHONE_ACCOUNT_ID, CallLog.Calls.GEOCODED_LOCATION),
            "${CallLog.Calls.DATE} > ?", arrayOf(since.toString()), "${CallLog.Calls.DATE} DESC"
        )?.use { c ->
            while (c.moveToNext()) {
                val type = when (c.getInt(3)) {
                    CallLog.Calls.INCOMING_TYPE -> "in"
                    CallLog.Calls.OUTGOING_TYPE -> "out"
                    CallLog.Calls.MISSED_TYPE -> "missed"
                    CallLog.Calls.REJECTED_TYPE -> "rejected"
                    CallLog.Calls.BLOCKED_TYPE -> "blocked"
                    CallLog.Calls.VOICEMAIL_TYPE -> "voicemail"
                    else -> "other"
                }
                val number = c.getString(1) ?: ""
                arr.put(JSONObject().put("id", c.getLong(0)).put("number", number)
                    .put("name", c.getString(2)?.ifBlank { null } ?: Lookup.name(ctx, number) ?: "")
                    .put("type", type).put("date", c.getLong(4)).put("duration", c.getLong(5))
                    .put("account", c.getString(6) ?: "").put("place", c.getString(7) ?: ""))
            }
        }
        return arr
    }
}
