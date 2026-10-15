plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.hp2.android"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.hp2.android"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        viewBinding = true
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.webkit:webkit:1.11.0")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("org.nanohttpd:nanohttpd:2.3.1")
    implementation("org.json:json:20240303")
}

// Refuse to create an APK without the real game module. Generated WebAssembly
// binaries are ignored by Git; CI stages them before invoking Gradle.
val verifyWebGameAssets = tasks.register("verifyWebGameAssets") {
    group = "verification"
    description = "Checks that the actual WebAssembly game module is staged."
    doLast {
        val webDir = file("src/main/assets/web")
        val js = webDir.resolve("hp2_game.js")
        val wasm = webDir.resolve("hp2_game.wasm")
        if (!js.isFile || js.length() == 0L || !wasm.isFile || wasm.length() == 0L) {
            throw GradleException(
                "Real WebAssembly assets are missing. Build with `cmake --preset web-wasm64` " +
                    "and `cmake --build --preset web-wasm64`, then copy dist/web-wasm64/* " +
                    "into app/src/main/assets/web/."
            )
        }
        if (js.readText().contains("placeholder", ignoreCase = true)) {
            throw GradleException("hp2_game.js is still a placeholder, not the game module.")
        }
    }
}

tasks.named("preBuild") {
    dependsOn(verifyWebGameAssets)
}
