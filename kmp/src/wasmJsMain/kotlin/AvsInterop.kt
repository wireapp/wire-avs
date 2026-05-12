@file:OptIn(kotlin.js.ExperimentalWasmJsInterop::class)
@file:JsModule("@wireapp/avs")

package com.wire.avskmp

import kotlin.js.JsAny

external fun getAvsInstance(): JsAny

external class Wcall(em_module: JsAny) : JsAny {
    fun init(env: Int): Int
    fun close()
}