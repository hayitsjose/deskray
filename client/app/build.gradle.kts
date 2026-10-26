plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "io.github.hayitsjose.deskray"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.hayitsjose.deskray"
        minSdk = 32
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }
    buildFeatures { prefab = true }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    // BouncyCastle jars each carry the same OSGi manifest; Android doesn't need it.
    packaging { resources { excludes += "META-INF/versions/9/OSGI-INF/MANIFEST.MF" } }
}

dependencies {
    implementation("org.khronos.openxr:openxr_loader_for_android:1.1.47")
    // Self-signed client certificate generation for pairing (Android has no cert builder API).
    implementation("org.bouncycastle:bcpkix-jdk18on:1.80")
}
