@file:OptIn(kotlinx.cinterop.ExperimentalForeignApi::class)

import avs.wcall_init
import avs.wcall_close

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlinx.coroutines.test.runTest
import kotlinx.cinterop.* // Essential for manual C-interop pointers if required

class AvsMacosTests {

    @Test
    fun testWcallInit() = runTest {
        // Native C method handles parameters straight as Kotlin Primitive types
        val err: Int = wcall_init(0)
        assertEquals(0, err, "Wcall init failure with error ${err}")
        wcall_close()

    }
}