@file:OptIn(kotlin.js.ExperimentalWasmJsInterop::class)
@file:JsModule("@wireapp/avs")

package com.wire.avskmp

import kotlin.js.JsAny

external fun getAvsInstance(): JsAny

external class Wcall(em_module: JsAny) : JsAny {
    fun init(env: Int): Int
    fun close()
       // The Kotlin mapping of the massive create method
    fun create(
        userid: String,
        clientid: String,
        readyh: ((version: Int, arg: Int) -> Unit)?,
        sendh: ((ctx: Int, convid: String?, userid_self: String?, clientid_self: String?, targets: String?, unused: String?, data: String?, len: Int, trans: Int, my_clients_only: Int, arg: Int) -> Int)?,
        sfth: ((ctx: Int, url: String?, data: String?, len: Int, arg: Int) -> Int)?,
        incomingh: ((convid: String?, msg_time: Int, userid: String?, clientid: String?, video_call: Int, should_ring: Int, conv_type: Int, arg: Int) -> Unit)?,
        missedh: ((convid: String?, msg_time: Int, userid: String?, clientid: String?, video_call: Int, arg: Int) -> Unit)?,
        answerh: ((convid: String?, arg: Int) -> Unit)?,
        estabh: ((convid: String?, userid: String?, clientid: String?, arg: Int) -> Unit)?,
        closeh: ((reason: Int, convid: String?, msg_time: Int, userid: String?, clientid: String?, arg: Int) -> Unit)?,
        metricsh: ((convid: String?, metrics_json: String?, arg: Int) -> Unit)?,
        cfg_reqh: ((wuser: Int, arg: Int) -> Int)?,
        acbrh: ((userid: String?, clientid: String?, enabled: Int, arg: Int) -> Unit)?,
        vstateh: ((convid: String?, userid: String?, clientid: String?, state: Int, arg: Int) -> Unit)?,
        arg: Int = definedExternally
    ): Int
}

// Represents the enum objects exported by the JS module
@JsName("ERROR")
external object ERROR : JsAny {
  val NO_MEMORY: Int
  val INVALID: Int
  val TIMED_OUT: Int
  val ALREADY: Int
  val UNKNOWN_PROTOCOL: Int
}

@JsName("QUALITY")
external object QUALITY : JsAny {
  val NORMAL: Int
  val MEDIUM: Int
  val POOR: Int
  val NETWORK_PROBLEM: Int
  val RECONNECTING: Int
}

@JsName("REASON")
external object REASON : JsAny {
  val NORMAL: Int
  val ERROR: Int
  val TIMEOUT: Int
  val LOST_MEDIA: Int
  val CANCELED: Int
  val ANSWERED_ELSEWHERE: Int
  val IO_ERROR: Int
  val STILL_ONGOING: Int
  val TIMEOUT_ECONN: Int
  val DATACHANNEL: Int
  val REJECTED: Int
  val OUTDATED_CLIENT: Int
  val NOONE_JOINED: Int
  val EVERYONE_LEFT: Int
  val AUTH_FAILED: Int
  val AUTH_FAILED_START: Int
  val DURATION: Int
}

@JsName("LOG_LEVEL")
external object LOG_LEVEL : JsAny {
  val DEBUG: Int
  val INFO: Int
  val WARN: Int
  val ERROR: Int
}

@JsName("AUDIO_STATE")
external object AUDIO_STATE : JsAny {
  val CONNECTING: Int
  val ESTABLISHED: Int
  val NETWORK_PROBLEM: Int
}

@JsName("VIDEO_STATE")
external object VIDEO_STATE : JsAny {
  val STOPPED: Int
  val STARTED: Int
  val BAD_CONN: Int
  val PAUSED: Int
  val SCREENSHARE: Int
  val RECONNECTING: Int
}

@JsName("ENV")
external object ENV : JsAny {
  val DEFAULT: Int
  val FIREFOX: Int
}

@JsName("CALL_TYPE")
external object CALL_TYPE : JsAny {
  val NORMAL: Int
  val VIDEO: Int
  val FORCED_AUDIO: Int
}

@JsName("CONV_TYPE")
external object CONV_TYPE : JsAny {
  val ONEONONE: Int
  val GROUP: Int
  val CONFERENCE: Int
  val CONFERENCE_MLS: Int
}

@JsName("STATE")
external object STATE : JsAny {
  val NONE: Int
  val OUTGOING: Int
  val INCOMING: Int
  val ANSWERED: Int
  val MEDIA_ESTAB: Int
  val TERM_LOCAL: Int
  val TERM_REMOTE: Int
  val UNKNOWN: Int
}

@JsName("VSTREAMS")
external object VSTREAMS : JsAny {
  val LIST: Int
}

@JsName("RESOLUTION")
external object RESOLUTION : JsAny {
  val ANY: Int
  val LOW: Int
  val HIGH: Int
}

@JsName("MODE")
external object MODE : JsAny {
  val MARSHAL: Int
  val DIRECT: Int
}

external interface WcallMember : JsAny {
    val userid: String
    val clientid: String
    val aestab: Int
    val vrecv: Int
    val muted: Int
}

external interface WcallClient : JsAny {
    val userid: String
    val clientid: String
}