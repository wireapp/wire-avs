@file:OptIn(kotlin.js.ExperimentalWasmJsInterop::class)

import com.wire.avskmp.* // Functions imported from local Maven package

import kotlin.js.Promise
import kotlin.js.unsafeCast
import kotlin.test.Test
import kotlin.test.Ignore
import kotlin.test.assertEquals
import kotlinx.coroutines.await
import kotlinx.coroutines.test.runTest

fun createWcallClient(userid: String, clientid: String): WcallClient =
    js("({ userid: userid, clientid: clientid })")

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
        
        
        // 2. Create with explicit callbacks
        val statusCode = wcallInstance.create(
            userid = "user_123",
            clientid = "client_abc",
            readyh = { version, arg -> 
                println("Wcall ready! Version: $version, Arg: $arg") 
            },
            sendh = null,
            sfth = null, // Pass null for unused handlers
            incomingh = null,
            missedh = null,
            answerh = null,
            estabh = null,
            closeh = null,
            metricsh = null,
            cfg_reqh = null,
            acbrh = null,
            vstateh = null
        )


        
        
        wcallInstance.close()
    }

    @Test
    fun testSampleEnum() {
        val qualityCode = QUALITY.NORMAL
        assertEquals(1, qualityCode, "check value of a sample imported enum")
    }

    @Test
    fun testSampleStruct() {
        val client = createWcallClient(userid = "james", clientid = "bond")
        assertEquals("james", client.userid, "check value of a sample imported struct property")
    }
}

