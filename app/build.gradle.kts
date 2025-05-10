plugins {
    id("com.android.application")
    kotlin("android")
}

android {
    namespace = "com.example.whizwordz"
    compileSdk = 33

    defaultConfig {
        applicationId = "com.example.whizwordz"
        minSdk = 28
        targetSdk = 33
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-O3")
            }
        }
        ndk {
            abiFilters += listOf("armeabi-v7a","arm64-v8a","x86","x86_64")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    kotlinOptions {
        jvmTarget = "1.8"
    }
}

dependencies {
    implementation(kotlin("stdlib"))
    implementation("androidx.appcompat:appcompat:1.5.1")
    implementation("androidx.core:core-ktx:1.8.0")
}
