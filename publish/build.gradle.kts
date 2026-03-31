import com.vanniktech.maven.publish.KotlinMultiplatform
import org.jetbrains.kotlin.gradle.dsl.KotlinMultiplatformExtension

plugins {
    kotlin("multiplatform") version "2.0.21"
    id("com.vanniktech.maven.publish.base") version "0.34.0"
}

group = "com.wire"
// WPB-22449: ToDo: get version information from build
version = "10.3.9.0-kmp"

repositories {
    mavenCentral()
    google()
}

// WPB-22449: ToDo: kmp complauns about src files that is why iosArm64Main and linuxX64Main 
// are added. Base publish plugin may eliminate these dummy files
kotlin {
    iosArm64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                // Path to the .def file
                definitionFile.set(project.file("src/nativeInterop/cinterop/ios.def"))

                // WPB-22449: ToDo: Fix paths
                compilerOpts("-framework", "avs", "-F/Users/sifa/wire/wire-avs/build/dist/ios/avs.xcframework/ios-arm64")
            }
        }
   }

   linuxX64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                definitionFile.set(project.file("src/nativeInterop/cinterop/linux.def"))
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

