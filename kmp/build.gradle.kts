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

    androidTarget() {
        publishLibraryVariants("release")
        compilerOptions {
            jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        }
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
    }
}

android {
    namespace = "com.waz.avs"
    compileSdk = 34

    defaultConfig {
        minSdk = 26
    }

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
