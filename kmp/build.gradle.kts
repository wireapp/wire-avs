import com.vanniktech.maven.publish.KotlinMultiplatform
import org.jetbrains.kotlin.gradle.dsl.KotlinMultiplatformExtension
import java.io.File

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.library)
    alias(libs.plugins.maven.publish.base)
}

group = findProperty("GROUP") as String? ?: "com.wire"
version = findProperty("VERSION_NAME") as String? ?: "0.0.1"

repositories {
    mavenCentral()
    google()
}

val iosLinkerOpts = listOf(
    "-framework", "AudioToolbox",
    "-framework", "AVFoundation",
    "-framework", "CFNetwork",
    "-framework", "CoreAudio",
    "-framework", "CoreGraphics",
    "-framework", "CoreMedia",
    "-framework", "CoreVideo",
    "-framework", "Metal",
    "-framework", "MetalKit",
    "-framework", "MobileCoreServices",
    "-framework", "Network",
    "-framework", "QuartzCore",
    "-framework", "ReplayKit",
    "-framework", "SystemConfiguration",
    "-framework", "UIKit",
    "-framework", "VideoToolbox",
    "-lc++",
    "-ObjC",
).joinToString(" ")

val osxLinkerOpts = listOf(
    "-framework", "AppKit",
    "-framework", "ApplicationServices",
    "-framework", "AudioToolbox",
    "-framework", "AudioUnit",
    "-framework", "AVFoundation",
    "-framework", "Cocoa",
    "-framework", "CoreAudio",
    "-framework", "CoreFoundation",
    "-framework", "CoreGraphics",
    "-framework", "CoreMedia",
    "-framework", "CoreVideo",
    "-framework", "Foundation",
    "-framework", "IOKit",
    "-framework", "OpenGL",
    "-framework", "QTKit",
    "-framework", "ScreenCaptureKit",
    "-framework", "Security",
    "-framework", "SystemConfiguration",
    "-lc++",
    "-ObjC",
).joinToString(" ")


fun generateIosOsxDef(
    buildDir: File,
    targetName: String,
    staticLibraryDir: String,
    linkerOpts: String,
): File {

    val generatedDir = File(buildDir, "generated/cinterop/$targetName").apply { mkdirs() }
    return File(generatedDir, "ios.def").apply {
        writeText(
            """
            language = Objective-C
            modules = avs
            package = avs
            staticLibraries = libavsobjc.a
            libraryPaths = $staticLibraryDir
            linkerOpts = $linkerOpts
            """.trimIndent()
        )
    }
}

// WPB-22449: ToDo: kmp complains about src files. As a workaround commonMain/empty.kt is added
kotlin {
    val path = System.getProperty("user.dir")
    val generatedBuildDir = File(path, "build")

    val appleTargets = listOf(
        Triple("iosArm64", "ios-arm64", "ios-arm64"),
        Triple("iosSimulatorArm64", "ios-arm64_x86_64-simulator", "iossim-arm64"),
        Triple("macosX64", "macos-arm64_x86_64", "osx-x86_64"),
        Triple("macosArm64", "macos-arm64_x86_64", "osx-arm64")
    )

    appleTargets.forEach { (targetName, xcDir, libDir) ->
        val target = when (targetName) {
            "iosArm64" -> iosArm64()
            "iosSimulatorArm64" -> iosSimulatorArm64()
            "macosX64" -> macosX64()
            "macosArm64" -> macosArm64()
            else -> error("Unknown target")
        }

        target.compilations.getByName("main") {
            val avs by cinterops.creating() {
                val frameworkPath = file("$path/build/dist/xc/avs.xcframework/$xcDir/").absolutePath
                val staticLibraryDir = file("$path/build/$libDir/lib").absolutePath
                val linkerOpts = if (targetName.startsWith("ios")) iosLinkerOpts else osxLinkerOpts

                definitionFile.set(
                    generateIosOsxDef(
                        generatedBuildDir,
                        targetName,
                        staticLibraryDir,
                        linkerOpts
                    )
                )

                compilerOpts("-framework", "avs", "-F$frameworkPath")
            }
        }
    }

    androidTarget() {
        publishLibraryVariants("release")
        compilerOptions {
            jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        }
    }

    @OptIn(org.jetbrains.kotlin.gradle.ExperimentalWasmDsl::class)
    wasmJs {
        browser{}
        binaries.library()
    }

    sourceSets {
        val androidMain by getting {
            dependencies {
                // Exported dependencies
                api(libs.apache.commons.math3)
                api(libs.camera.view)

                // Internal dependencies
                implementation(libs.google.guava)
                implementation(libs.camera.core)
                implementation(libs.camera.camera2)
                implementation(libs.camera.lifecycle)
                implementation(libs.camera.video)
                implementation(libs.camera.extensions)
                implementation(libs.android.core)
            }
        }

        val wasmJsMain by getting {
            dependencies { 
                // Core Kotlin standard library dependencies for Wasm generation
                implementation(kotlin("stdlib-wasm-js"))

                // Provide a new package.json
                // The one for npm (../build/dist/wasm/package.json) does not work here
                api(npm("@wireapp/avs", project.file("src/wasmJsMain/resources")))
            }

            // Provide a resource directory for wasm resources
            resources.srcDir("../build/dist/wasm/dist")
        }
    }
}

// This is needed to add a custom package.json in wasmjs
tasks.named<org.gradle.jvm.tasks.Jar>("wasmJsJar") {
    // Instructs the JAR engine to bypass duplicate path safety blocks
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
}

// Define a helper task to get js filenames in ./build/dist/wasm/dist/ and
// provide and aggregate avs.js
val generateJsExports by tasks.registering {
    group = "build"
    description = "Generates a JS file exporting all JavaScript files from a specific directory."

    val inputDir = file("../build/dist/wasm/dist/")
    val outputFile = file("./src/wasmJsMain/resources/avs.js")

    inputs.dir(inputDir).withPathSensitivity(PathSensitivity.RELATIVE)
    outputs.file(outputFile)

    doLast {
        if (!inputDir.exists() || !inputDir.isDirectory) {
            logger.warn("Input directory does not exist: ${inputDir.absolutePath}")
            return@doLast
        }

        val exportLines = inputDir.listFiles { file -> file.isFile && file.extension == "js" }
            ?.sortedBy { it.name }
            ?.map { file -> "export * from './${file.name}';" }
            ?: emptyList()

        // Define the warning comment block about file being auto generated
        val fileHeader = """
            // ============================================================================
            // WARNING: THIS FILE IS AUTO-GENERATED. DO NOT EDIT MANUALLY.
            // Any manual modifications will be overwritten during the next Gradle build
            // with task ./wire-avs/kmp/build.gradle.kts:generateJsExports
            // ============================================================================
            
        """.trimIndent()

        outputFile.parentFile.mkdirs()
        
        // Combine the header comment and the export lines
        val finalContent = fileHeader + "\n" + exportLines.joinToString("\n") + "\n"
        outputFile.writeText(finalContent)
        
        logger.lifecycle("Generated JS exports in ${outputFile.path}")
    }
}

// Resources for the wasmJsMain compilation unit are processed with 
// wasmJsProcessResources. Hooking the helper before this will ensure correct aggregation.
tasks.named("wasmJsProcessResources") {
    dependsOn(generateJsExports)
}

// Get used devtools/ndk version, default to lask known working one if not found
val systemNdkVersion = System.getenv("ANDROID_NDK_VER") ?: "28.2.13676358"

android {
    namespace = "com.waz.avs"
    compileSdk = 34

    defaultConfig {
        minSdk = 26
    }

    // Set ndk version to resonate with devtoos/ndk version
    // to avoid compatability errors in :avs-kmp:stripReleaseDebugSymbols
    ndkVersion = systemNdkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            res.srcDirs("../build/dist/android/aar")
            jniLibs.srcDirs("../build/dist/android/aar")
            // Set source directory for avs and com.waz packages
            java.srcDirs("../android/lib/src/main/java")
        }
    }

    libraryVariants.all {
        val variantName = name
        val capitalizedName = variantName.replaceFirstChar {
            if (it.isLowerCase()) it.titlecase() else it.toString()
        }

        // Copy prebuild org.webrtc binaries into temporary build folder of compiled java
        // Kmp android plugin will use this folder as a source for packaging into class.jar
        val copyPrebuildWebrtcBinaries = tasks.register<Copy>("copyPrebuildWebrtcBinariesFor$capitalizedName") {
            val sourceJar =  File(rootDir, "build/dist/android/aar/classes.jar")
            from(sourceJar)
            from(zipTree(sourceJar).matching{
                include("org/**")
            })
            into(layout.buildDirectory.dir("intermediates/javac/${variantName}/compile${capitalizedName}JavaWithJavac/classes"))
        }

        // Register new task as a dependency to kmp chain to be sure it is done before packaging
        tasks.matching { it.name == "sync${capitalizedName}LibJars" }.configureEach {
            dependsOn(copyPrebuildWebrtcBinaries)
        }
    }
}

// Allows skipping signing jars published to 'MavenLocal' repository
tasks.withType<Sign>().configureEach {
    if (System.getenv("CI") == null) {
        enabled = false
    }
}

mavenPublishing {
    publishToMavenCentral(automaticRelease = true)
    signAllPublications()
    pom {
        name.set(findProperty("POM_NAME") as String)
        description.set(findProperty("POM_DESCRIPTION") as String)
        url.set(findProperty("POM_URL") as String)

        licenses {
            license {
                name.set(findProperty("POM_LICENCE_NAME") as String)
                url.set(findProperty("POM_LICENCE_URL") as String)
                distribution.set(findProperty("POM_LICENSE_DIST") as String)
            }
        }

        scm {
            url.set(findProperty("POM_SCM_URL") as String)
            connection.set(findProperty("POM_SCM_CONNECTION") as String)
            developerConnection.set(findProperty("POM_SCM_DEV_CONNECTION") as String)
        }

        developers {
            developer {
                name.set(findProperty("POM_DEVELOPER_NAME") as String)
                email.set(findProperty("POM_DEVELOPER_EMAIL") as String)
                organization.set(findProperty("POM_NAME") as String)
                organizationUrl.set(findProperty("POM_URL") as String)
            }
        }
    }
}
