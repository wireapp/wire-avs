plugins {
    alias(libs.plugins.kotlin.multiplatform)
}

kotlin {
    @OptIn(org.jetbrains.kotlin.gradle.ExperimentalWasmDsl::class)
    wasmJs {
        browser {}
    }

    sourceSets {
        // ... commonTest / commonMain / wasmJsMain definitions
        val wasmJsTest by getting {
            dependencies {
                implementation(kotlin("test"))
                implementation(libs.coroutines.core)
                implementation(libs.coroutines.test) 
                implementation(libs.avs.local)
            }
        }
    }
}