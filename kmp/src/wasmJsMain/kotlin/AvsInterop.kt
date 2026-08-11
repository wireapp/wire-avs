/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/*
============================================================================
WARNING: THIS FILE IS AUTO-GENERATED. DO NOT EDIT MANUALLY.
Any manual modifications will be overwritten during the next avs build
============================================================================
*/


@file:OptIn(kotlin.js.ExperimentalWasmJsInterop::class)
@file:JsModule("@wireapp/avs")

package com.wire.avskmp

import kotlin.js.JsAny

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

external fun getAvsInstance(): JsAny

external class Wcall(em_module: JsAny) : JsAny {
  fun init(
    env: Int
  ): Int
  fun close()
  fun create(
    userid: String,
    clientid: String,
    readyh: ((version: Int, arg: Int) -> Unit)?,
    sendh: ((ctx: Int, convid: String, userid_self: String, clientid_self: String, targets: String?, unused: String?, data: String, len: Int, trans: Int, my_clients_only: Int, arg: Int) -> Int)?,
    sfth: ((ctx: Int, url: String, data: String, len: Int, arg: Int) -> Int)?,
    incomingh: ((convid: String, msg_time: Int, userid: String, clientid: String, video_call: Int, should_ring: Int, conv_type: Int, arg: Int) -> Unit)?,
    missedh: ((convid: String, msg_time: Int, userid: String, clientid: String, video_call: Int, arg: Int) -> Unit)?,
    answerh: ((convid: String, arg: Int) -> Unit)?,
    estabh: ((convid: String, userid: String, clientid: String, arg: Int) -> Unit)?,
    closeh: ((reason: Int, convid: String, msg_time: Int, userid: String, clientid: String, arg: Int) -> Unit)?,
    metricsh: ((convid: String, metrics_json: String, arg: Int) -> Unit)?,
    cfg_reqh: ((wuser: Int, arg: Int) -> Int)?,
    acbrh: ((userid: String, clientid: String, enabled: Int, arg: Int) -> Unit)?,
    vstateh: ((convid: String, userid: String, clientid: String, state: Int, arg: Int) -> Unit)?,
    arg: Int
  ): Int
  fun createEx(
    userid: String,
    clientid: String,
    use_mediamgr: Int,
    msys_name: String,
    readyh: ((version: Int, arg: Int) -> Unit)?,
    sendh: ((ctx: Int, convid: String, userid_self: String, clientid_self: String, targets: String?, unused: String?, data: String, len: Int, trans: Int, my_clients_only: Int, arg: Int) -> Int)?,
    sfth: ((ctx: Int, url: String, data: String, len: Int, arg: Int) -> Int)?,
    incomingh: ((convid: String, msg_time: Int, userid: String, clientid: String, video_call: Int, should_ring: Int, conv_type: Int, arg: Int) -> Unit)?,
    missedh: ((convid: String, msg_time: Int, userid: String, clientid: String, video_call: Int, arg: Int) -> Unit)?,
    answerh: ((convid: String, arg: Int) -> Unit)?,
    estabh: ((convid: String, userid: String, clientid: String, arg: Int) -> Unit)?,
    closeh: ((reason: Int, convid: String, msg_time: Int, userid: String, clientid: String, arg: Int) -> Unit)?,
    metricsh: ((convid: String, metrics_json: String, arg: Int) -> Unit)?,
    cfg_reqh: ((wuser: Int, arg: Int) -> Int)?,
    acbrh: ((userid: String, clientid: String, enabled: Int, arg: Int) -> Unit)?,
    vstateh: ((convid: String, userid: String, clientid: String, state: Int, arg: Int) -> Unit)?,
    arg: Int
  ): Int
  fun setShutdownHandler(
    wuser: Int,
    shuth: ((wuser: Int, arg: Int) -> Unit)?,
    arg: Int
  )
  fun destroy(
    wuser: Int
  )
  fun setTrace(
    wuser: Int,
    trace: Int
  )
  fun setBackground(
    wuser: Int,
    background: Int
  ): Int
  fun start(
    wuser: Int,
    convid: String,
    call_type: Int,
    conv_type: Int,
    audio_cbr: Int,
    meeting: Int
  ): Int
  fun answer(
    wuser: Int,
    convid: String,
    call_type: Int,
    audio_cbr: Int
  ): Int
  fun resp(
    wuser: Int,
    status: Int,
    reason: String,
    ctx: Int
  )
  fun configUpdate(
    wuser: Int,
    err: Int,
    json_str: String
  )
  fun sftResp(
    wuser: Int,
    perr: Int,
    buf: String,
    len: Int,
    ctx: Int
  )
  fun recvMsg(
    wuser: Int,
    buf: String,
    len: Int,
    curr_time: Int,
    msg_time: Int,
    convid: String,
    userid: String,
    clientid: String,
    conv_type: Int,
    meeting: Int
  ): Int
  fun end(
    wuser: Int,
    convid: String
  )
  fun reject(
    wuser: Int,
    convid: String
  ): Int
  fun isVideoCall(
    wuser: Int,
    convid: String
  ): Int
  fun setMediaEstabHandler(
    wuser: Int,
    mestabh: ((convid: String, peer: Int, userid: String, clientid: String, arg: Int) -> Unit)?
  )
  fun setMediaStoppedHandler(
    wuser: Int,
    mstoph: ((convid: String, arg: Int) -> Unit)?
  )
  fun setDataChanEstabHandler(
    wuser: Int,
    dcestabh: ((convid: String, userid: String, clientid: String, arg: Int) -> Unit)?
  )
  fun setVideoSendState(
    wuser: Int,
    convid: String,
    state: Int
  )
  fun setVideoHandlers(
    render_frame_h: JsAny?,
    size_h: ((w: Int, h: Int, userid: String, clientid: String, arg: Int) -> Unit)?,
    arg: Int
  )
  fun networkChanged()
  fun setGroupChangedHandler(
    wuser: Int,
    chgh: ((convid: String, arg: Int) -> Unit)?,
    arg: Int
  )
  fun setParticipantChangedHandler(
    wuser: Int,
    chgh: ((convid: String, mjson: String, arg: Int) -> Unit)?,
    arg: Int
  )
  fun setNetworkQualityHandler(
    wuser: Int,
    netqh: ((convid: String, userid: String, clientid: String, quality_info: String, arg: Int) -> Unit)?,
    interval: Int,
    arg: Int
  ): Int
  fun setLogHandler(
    logh: ((level: Int, msg: String, arg: JsAny?) -> Unit)?,
    arg: Int
  )
  fun setEpochInfo(
    wuser: Int,
    convid: String,
    epochid: Int,
    clients_json: String,
    key_base64: String
  ): Int
  fun setReqNewEpochHandler(
    wuser: Int,
    req_new_epochh: ((wuser: Int, convid: String, arg: Int) -> Unit)?
  )
  fun setDuration(
    wuser: Int,
    convid: String,
    duration: Int
  )
  fun getMute(
    wuser: Int
  ): Int
  fun setMute(
    wuser: Int,
    muted: Int
  )
  fun setMuteHandler(
    wuser: Int,
    muteh: ((muted: Int, arg: Int) -> Unit)?,
    arg: Int
  )
  fun setStateHandler(
    wuser: Int,
    stateh: ((convid: String, state: Int, arg: Int) -> Unit)?
  )
  fun getState(
    wuser: Int,
    convid: String
  ): Int
  fun iterateState(
    wuser: Int,
    stateh: ((convid: String, state: Int, arg: Int) -> Unit)?,
    arg: Int
  )
  fun propsyncRequest(
    wuser: Int,
    convid: String
  )
  fun enablePrivacy(
    wuser: Int,
    enabled: Int
  )
  fun setProxy(
    host: String,
    port: Int
  ): Int
  fun setReqClientsHandler(
    wuser: Int,
    reqch: ((wuser: Int, convid: String, arg: Int) -> Unit)?
  )
  fun setClientsForConv(
    wuser: Int,
    convid: String,
    json: String
  ): Int
  fun setActiveSpeakerHandler(
    wuser: Int,
    activeh: ((wuser: Int, convid: String, json_levels: String, arg: Int) -> Unit)?
  )
  fun requestVideoStreams(
    wuser: Int,
    convid: String,
    mode: Int,
    json: String
  ): Int
  fun processNotifications(
    wuser: Int,
    processing: Int
  ): Int
  fun audioRecord(
    wuser: Int,
    path: String
  ): Int
  fun poll()
  fun setupEx(
    flags: Int
  ): Int
  fun eventCreate(
    userid: String,
    clientid: String,
    incomingh: ((convid: String, msg_time: Int, userid: String, clientid: String, video_call: Int, should_ring: Int, conv_type: Int, arg: Int) -> Unit)?,
    missedh: ((convid: String, msg_time: Int, userid: String, clientid: String, video_call: Int, arg: Int) -> Unit)?,
    closeh: ((reason: Int, convid: String, msg_time: Int, userid: String, clientid: String, arg: Int) -> Unit)?,
    arg: Int
  ): Int
  fun eventStart(
    wuser: Int
  )
  fun eventProcess(
    wuser: Int,
    buf: String,
    len: Int,
    curr_time: Int,
    msg_time: Int,
    convid: String,
    userid: String,
    clientid: String,
    conv_type: Int
  ): Int
  fun eventEnd(
    wuser: Int
  )
  fun setMode(
    mode: Int
  )
  fun getMode(): Int
  fun setConfigVersion(
    sft_version: Int,
    turn_version: Int
  )
}
