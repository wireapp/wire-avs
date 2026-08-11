@file:OptIn(kotlin.js.ExperimentalWasmJsInterop::class)

import com.wire.avskmp.* // Functions imported from local Maven package

import kotlin.js.Promise
import kotlin.js.unsafeCast
import kotlin.test.Test
import kotlin.test.Ignore
import kotlin.test.assertEquals
import kotlinx.coroutines.await
import kotlinx.coroutines.test.runTest

// Tests will be executed inside the headless browser
class AvsWasmTests {
    @Test
    fun testWcallInit() = runTest {
        // Try to access AVS exports and do a smoke test
        val instance: JsAny = getAvsInstance()
        val promiseInstance: Promise<Wcall> = instance.unsafeCast<Promise<Wcall>>()
        val wcallInstance: Wcall = promiseInstance.await()
        val err: Int = wcallInstance.init(0)
        assertEquals(0, err, "Wcall init failure with error ${err}")
        wcallInstance.close()
    }
}

