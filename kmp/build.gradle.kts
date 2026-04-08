import com.vanniktech.maven.publish.KotlinMultiplatform
import org.jetbrains.kotlin.gradle.dsl.KotlinMultiplatformExtension
import java.io.File

plugins {
    kotlin("multiplatform") version "2.2.21"
    id("com.vanniktech.maven.publish.base") version "0.34.0"
}

group = findProperty("GROUP") as String? ?: "com.wire"
version = findProperty("VERSION_NAME") as String? ?: "0.0.1-kmp"

repositories {
    mavenCentral()
    google()
}

fun generateIosDef(
    buildDir: File,
    targetName: String,
    staticLibraryDir: String,
): File {
    val iosLinkerOpts = listOf(
        "-framework", "AVFoundation",
        "-framework", "AudioToolbox",
        "-framework", "CFNetwork",
        "-framework", "CoreAudio",
        "-framework", "CoreGraphics",
        "-framework", "CoreMedia",
        "-framework", "CoreVideo",
        "-framework", "Metal",
        "-framework", "MetalKit",
        "-framework", "Network",
        "-framework", "QuartzCore",
        "-framework", "ReplayKit",
        "-framework", "SystemConfiguration",
        "-framework", "UIKit",
        "-framework", "VideoToolbox",
        "-framework", "MobileCoreServices",
        "-lc++",
        "-ObjC",
    ).joinToString(" ")

    val generatedDir = File(buildDir, "generated/cinterop/$targetName").apply { mkdirs() }
    return File(generatedDir, "ios.def").apply {
        writeText(
            """
            language = Objective-C
            modules = avs
            package = avs
            staticLibraries = libavsobjc.a
            libraryPaths = $staticLibraryDir
            linkerOpts = $iosLinkerOpts
            """.trimIndent()
        )
    }
}

// WPB-22449: ToDo: kmp complains about src files. As a workaround commonMain/empty.kt is added
kotlin {
    val path = System.getProperty("user.dir")
    val generatedBuildDir = File(path, "build")

    iosArm64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                val frameworkPath = file("$path/build/dist/ios/avs.xcframework/ios-arm64/").absolutePath
                val staticLibraryDir = file("$path/build/ios-arm64/lib").absolutePath

                definitionFile.set(
                    generateIosDef(
                        generatedBuildDir,
                        "iosArm64",
                        staticLibraryDir,
                    )
                )

                compilerOpts("-framework", "avs", "-F${frameworkPath}")
            }
        }
    }

    iosSimulatorArm64() {
        compilations.getByName("main") {
            val avs by cinterops.creating {
                val frameworkPath = file("$path/build/dist/ios/avs.xcframework/ios-arm64_x86_64-simulator/").absolutePath
                val staticLibraryDir = file("$path/build/iossim-arm64/lib").absolutePath

                definitionFile.set(
                    generateIosDef(
                        generatedBuildDir,
                        "iosSimulatorArm64",
                        staticLibraryDir,
                    )
                )

                compilerOpts("-framework", "avs", "-F${frameworkPath}")
            }
        }
    }

    // We dont have an easy linux avs build in mac, disable linux target in mac host
    if (System.getProperty("os.name").contains("Linux")) {
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
}

// Allows skipping signing jars published to 'MavenLocal' repository
tasks.withType<Sign>().configureEach {
    if (System.getenv("CI") == null) {
        enabled = false
    }
}
