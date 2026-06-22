package com.wire.avskmp

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlinx.coroutines.test.runTest
import kotlinx.cinterop.* // Essential for manual C-interop pointers if required

class AvsMacosTests {
    
    @Test
    fun testWcallInit() = runTest {
        // Kotlin/Native maps C structures and pointers directly here
        val wcallInstance: Wcall = getAvsInstance()
        
        // Native C method handles parameters straight as Kotlin Primitive types
        val err: Int = wcallInstance.init(0)
        assertEquals(0, err, "Wcall init failure with error ${err}")
        
        wcallInstance.close()
    }
}