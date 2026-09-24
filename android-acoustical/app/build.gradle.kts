plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

android {
    namespace = "com.rork.acoustical"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.rork.acoustical"
        minSdk = 24
        targetSdk = 36
        versionCode = 7
        versionName = "1.4.0"
    }

    // Llave de firma del proyecto (en el repo, que es privado). Todos los
    // APKs — CI y build local — comparten esta firma: sin ella, cada APK
    // lleva la llave debug de la máquina que lo compiló y Android rechaza la
    // actualización con "el paquete no está bien" (firma distinta).
    signingConfigs {
        create("acoustical") {
            storeFile = file("../keystore/acoustical.jks")
            storePassword = "acoustical2026"
            keyAlias = "acoustical"
            keyPassword = "acoustical2026"
        }
    }

    buildTypes {
        // Debug y release firmados con la misma llave del proyecto: cualquier
        // APK nuevo se instala encima del anterior sin desinstalar.
        getByName("debug") {
            signingConfig = signingConfigs.getByName("acoustical")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("acoustical")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    // Native DSP: builds libacoustical_dsp.so from app/src/main/cpp, which
    // links the shared C core (dsp/acoustical_dsp.c) so Android runs the same
    // FFT / band-aggregation as the Windows app. Pure-Kotlin fallback used if
    // the lib is absent.
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_11)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    implementation(libs.androidx.material.icons.extended)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.ktor.client.core)
    implementation(libs.ktor.client.android)
    implementation(libs.ktor.client.content.negotiation)
    implementation(libs.ktor.serialization.json)
    implementation(libs.coil.compose)
    implementation(libs.coil.network.okhttp)
    implementation(libs.koin.androidx.compose)

    // JVM unit tests for the pure-Kotlin DSP (FftProcessor, RoomCorrector).
    testImplementation(libs.junit)
    debugImplementation(libs.androidx.ui.tooling)
}
