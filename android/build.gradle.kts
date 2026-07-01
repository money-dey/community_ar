group = "dev.communityar"
version = "1.0-SNAPSHOT"

buildscript {
    val kotlinVersion = "2.3.20"
    repositories {
        google()
        mavenCentral()
    }

    dependencies {
        classpath("com.android.tools.build:gradle:9.0.1")
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:$kotlinVersion")
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

plugins {
    id("com.android.library")
}

android {
    namespace = "dev.communityar"

    compileSdk = 36

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs("src/main/kotlin")
        }
    }

    defaultConfig {
        minSdk = 24

        // Compile the C++ core into libcommunity_ar_native.so (loaded by
        // CommunityARPlugin via System.loadLibrary("community_ar_native")).
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }

        ndk {
            // 64-bit and 32-bit ARM; add x86_64 if you need emulator builds.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    // Point the native build at the repo-root CMakeLists.txt.
    //
    // NOTE: a device build additionally requires two vendored/fetched inputs:
    //   - TensorFlow Lite (C API + GPU delegate) — `bash tools/fetch_tflite.sh`
    //     (CMakeLists.txt consumes it from third_party/tensorflow-lite/)
    //   - MediaPipe model files — `bash tools/fetch_models.sh`
    // Without the TFLite libs the native link step fails (CMake emits a clear
    // error pointing at fetch_tflite.sh); the Gradle/Flutter structure itself
    // is complete.
    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}
