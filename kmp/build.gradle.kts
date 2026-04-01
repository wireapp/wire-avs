import com.vanniktech.maven.publish.KotlinMultiplatform
import org.jetbrains.kotlin.gradle.dsl.KotlinMultiplatformExtension

plugins {
    kotlin("multiplatform") version "2.0.21"
    id("com.vanniktech.maven.publish.base") version "0.34.0"
}

group = findProperty("GROUP") as String? ?: "com.wire"
version = findProperty("VERSION_NAME") as String? ?: "0.0.1-kmp"

repositories {
    mavenCentral()
    google()
}

// WPB-22449: ToDo: kmp complains about src files. As a workaround commonMain/empty.kt is added
kotlin {
    val path = System.getProperty("user.dir")

    iosArm64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                definitionFile.set(project.file("src/nativeInterop/cinterop/ios.def"))

                val frameworkPath = file("$path/build/dist/ios/avs.xcframework/ios-arm64/").absolutePath
                compilerOpts("-framework", "avs", "-F/${frameworkPath}")
            }
        }
   }

    iosSimulatorArm64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                definitionFile.set(project.file("src/nativeInterop/cinterop/ios.def"))

                val frameworkPath = file("$path/build/dist/ios/avs.xcframework/ios-arm64_x86_64-simulator/").absolutePath
                compilerOpts("-framework", "avs", "-F/${frameworkPath}")
            }
        }
   }

   linuxX64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                definitionFile.set(project.file("src/nativeInterop/cinterop/linux.def"))

                val includePath = file("$path/build/dist/linux/avscore/include/avs/").absolutePath
                val libraryPath = file("$path/build/dist/linux/avscore/lib/").absolutePath
                compilerOpts("-I$includePath")
                extraOpts("-libraryPath", "$libraryPath")
            }
        }
    }
}

// Allows skipping signing jars published to 'MavenLocal' repository
tasks.withType<Sign>().configureEach {
    if (System.getenv("CI") == null) {
        enabled = false
    }
}

