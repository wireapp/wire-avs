import org.gradle.internal.os.OperatingSystem

plugins {
    alias(libs.plugins.kotlin.multiplatform)
}

val isMacOs = OperatingSystem.current().isMacOsX

kotlin {
    @OptIn(org.jetbrains.kotlin.gradle.ExperimentalWasmDsl::class)
    wasmJs {
        // by default empty browser {} block will use chrome headles
        browser {
            testTask {
                useKarma {
                    useChromeHeadless()
                    // useFirefoxHeadless()
                    if (isMacOs) {
                        // safari does not have an headless mode
                        useSafari()
                    }
                }
            }
        }
    }

    // Defines the macOS target (macosArm64 for Apple Silicon M1/M2/M3, macosX64 for Intel)
    macosArm64 {
        // we dont need the following block becouse kotlin generates it by default
        // if we would like to configure, we can use
        // getTest("DEBUG").apply { }
        // binaries.test {
            // Generates a native executable for tests
        // }
    }

    sourceSets {
        // Dependencies for all platform tests will be here when 'avs' is fully multiplatform
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test")) 
            }
        }


        // ... commonTest / commonMain / wasmJsMain definitions
        val wasmJsTest by getting {
            dependencies {
                implementation(kotlin("test"))
                implementation(libs.coroutines.core)
                implementation(libs.coroutines.test) 
                implementation(libs.avs.local)
            }
        }

        // macOS test target
        val macosArm64Test by getting {
            dependencies {
                // Pulls in the library specifically for macOS testing execution
                implementation(kotlin("test"))
                implementation(libs.avs.local) 
            }
        }
    }
}