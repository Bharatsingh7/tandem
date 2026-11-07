package com.cyberwolf.tandem.link

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONObject

/** The current PC connection, if any. Features call [emit] to push events; they're dropped while offline. */
object Link {
    const val PORT = 47900

    data class PairRequest(val name: String, val code: String)
    data class State(val running: Boolean = false, val peer: String? = null, val pairing: PairRequest? = null)

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    @Volatile var session: Session? = null
        private set

    val connected get() = session?.let { it.trusted && !it.closed } == true

    fun emit(event: String, data: Any? = null) {
        session?.takeIf { it.trusted }?.event(event, data)
    }

    fun emit(event: String, build: JSONObject.() -> Unit) = emit(event, JSONObject().apply(build))

    internal fun setRunning(r: Boolean) = _state.update { it.copy(running = r) }
    internal fun setPairing(p: PairRequest?) = _state.update { it.copy(pairing = p) }

    internal fun attach(s: Session) {
        val old = session
        session = s
        if (old != null && old !== s) old.close()
        _state.update { it.copy(peer = s.peerName) }
    }

    internal fun detach(s: Session) {
        if (session === s) {
            session = null
            _state.update { it.copy(peer = null) }
        }
    }
}
