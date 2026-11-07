package com.cyberwolf.tandem.link

import org.json.JSONObject

/** Method registry for requests coming from the PC. Feature modules register handlers at startup. */
object Router {
    class Failure(msg: String) : Exception(msg)

    private val handlers = HashMap<String, (JSONObject) -> Any?>()

    fun on(method: String, handler: (JSONObject) -> Any?) {
        synchronized(handlers) { handlers[method] = handler }
    }

    fun dispatch(method: String, args: JSONObject): Any? {
        val h = synchronized(handlers) { handlers[method] } ?: throw Failure("unknown method $method")
        return h(args)
    }
}
