import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Release signing. CI passes the keystore through environment variables;
// locally it comes from a properties file kept outside the repo
// (default ~/.local/share/deskray/signing/keystore.properties). Without either,
// release builds are left unsigned and debug builds are unaffected.
val signing: Map<String, String>? = run {
    System.getenv("DESKRAY_KEYSTORE")?.let { store ->
        return@run mapOf(
            "storeFile" to store,
            "storePassword" to System.getenv("DESKRAY_KEYSTORE_PASSWORD").orEmpty(),
            "keyAlias" to (System.getenv("DESKRAY_KEY_ALIAS") ?: "deskray"),
            "keyPassword" to System.getenv("DESKRAY_KEY_PASSWORD").orEmpty(),
        )
    }
    val file = File(
        System.getenv("DESKRAY_KEYSTORE_PROPERTIES")
            ?: "${System.getProperty("user.home")}/.local/share/deskray/signing/keystore.properties",
    )
    if (!file.exists()) return@run null
    Properties().apply { file.inputStream().use(::load) }.let { p ->
        p.stringPropertyNames().associateWith { p.getProperty(it) }
    }
}

android {
    namespace = "io.github.hayitsjose.deskray"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.hayitsjose.deskray"
        minSdk = 32
        targetSdk = 34
        // CI sets these from the git tag (v0.2.0 -> 0.2.0 / 2000).
        versionCode = (project.findProperty("deskray.versionCode") as String?)?.toInt() ?: 1
        versionName = (project.findProperty("deskray.versionName") as String?) ?: "0.1.0"
        buildConfigField("String", "SOURCE_URL", "\"https://github.com/hayitsjose/deskray\"")
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }
    signingConfigs {
        if (signing != null) {
            create("release") {
                storeFile = file(signing.getValue("storeFile"))
                storePassword = signing.getValue("storePassword")
                keyAlias = signing.getValue("keyAlias")
                keyPassword = signing.getValue("keyPassword")
            }
        }
    }
    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
            isMinifyEnabled = false  // BouncyCastle + JNI entry points; revisit with keep rules
        }
    }
    buildFeatures {
        prefab = true
        buildConfig = true
    }
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
