import java.util.Properties

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
        versionCode = 12
        versionName = "1.7.0"
    }

    // --- Firma del proyecto ---
    //
    // Todos los APKs (CI y build local) comparten la misma firma: sin ella,
    // cada APK llevaría la llave debug de la máquina que lo compiló y
    // Android rechazaría la actualización ("el paquete no está bien").
    //
    // EL KEYSTORE YA NO VIAJA POR GIT (ver keystore/README.md): los passwords
    // se leen de local.properties (teclas acoustical.storePassword /
    // acoustical.keyPassword) o, en CI, de las variables de entorno
    // AcousticalStorePassword / AcousticalKeyPassword.
    val acousticalKeystoreFile = file("../keystore/acoustical.jks")
    val localSigningProps = Properties()
    rootProject.file("local.properties").takeIf { it.isFile }?.inputStream()?.use {
        localSigningProps.load(it)
    }
    val acousticalStorePassword: String? =
        localSigningProps.getProperty("acoustical.storePassword")
            ?: System.getenv("AcousticalStorePassword")
    val acousticalKeyPassword: String? =
        localSigningProps.getProperty("acoustical.keyPassword")
            ?: System.getenv("AcousticalKeyPassword")

    val useProjectSigning: Boolean = when {
        acousticalStorePassword != null && acousticalKeystoreFile.isFile -> true
        // Hay password pero no keystore: máquina recién clonada. Compilamos
        // sin firma de proyecto (con la debug local) y avisamos; NO es un
        // error porque el keystore puede llegar por otro canal.
        acousticalStorePassword != null -> {
            logger.warn(
                "[AcoustiCal] Hay password de firma (local.properties o " +
                    "AcousticalStorePassword) pero keystore/acoustical.jks no existe " +
                    "en esta copia: se compila SIN firma del proyecto. Recupera el " +
                    "keystore (keystore/README.md); si se ha perdido, las " +
                    "instalaciones existentes NO podrán actualizarse encima."
            )
            false
        }
        // Keystore en disco pero sin password: no se puede firmar y es una
        // señal de que la copia está incompleta (el password es el secreto,
        // no el fichero).
        acousticalKeystoreFile.isFile -> throw GradleException(
            "[AcoustiCal] keystore/acoustical.jks existe pero no hay password de " +
                "firma. Define acoustical.storePassword (y acoustical.keyPassword) " +
                "en el local.properties de android-acoustical/, o las variables de " +
                "entorno AcousticalStorePassword / AcousticalKeyPassword en la CI."
        )
        else -> throw GradleException(
            "[AcoustiCal] No hay password de firma ni keystore. " +
                "  - Build local: añade acoustical.storePassword y " +
                "acoustical.keyPassword al local.properties de android-acoustical/ " +
                "(ese fichero ya está en el .gitignore) y copia keystore/acoustical.jks. " +
                "  - CI: exporta AcousticalStorePassword y AcousticalKeyPassword y " +
                "copia el keystore. " +
                "  - Si has PERDIDO el keystore original: regenera uno con keytool " +
                "(ver keystore/README.md), pero TEN EN CUENTA la consecuencia: con " +
                "una firma nueva, los dispositivos con la app ya instalada NO " +
                "podrán actualizarse encima y deberán desinstalarla primero."
        )
    }

    if (useProjectSigning) {
        val storePwd = requireNotNull(acousticalStorePassword)
        signingConfigs {
            create("acoustical") {
                storeFile = acousticalKeystoreFile
                storePassword = storePwd
                keyAlias = "acoustical"
                // Si no se dio keyPassword por separado, caemos sobre el del
                // store (convención: la mayoría de keystores usan ambos iguales).
                keyPassword = acousticalKeyPassword ?: storePwd
            }
        }
    }

    buildTypes {
        // Debug y release firmados con la misma llave del proyecto (cuando
        // está disponible): cualquier APK nuevo se instala encima del
        // anterior sin desinstalar.
        getByName("debug") {
            if (useProjectSigning) {
                signingConfig = signingConfigs.getByName("acoustical")
            }
        }
        release {
            isMinifyEnabled = false
            if (useProjectSigning) {
                signingConfig = signingConfigs.getByName("acoustical")
            }
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
    // ktor (4 artefactos), coil (2) y koin (1) estaban aquí como deps muertas:
    // grep en src/main confirma que ningún código los importa — eran ~1 MB de
    // APK y superficie de CVEs sin uso. Retirados en la tanda de limpieza.

    // JVM unit tests for the pure-Kotlin DSP (FftProcessor, RoomCorrector).
    testImplementation(libs.junit)
    debugImplementation(libs.androidx.ui.tooling)
}
