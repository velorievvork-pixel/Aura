// :app — Compose-клиент Aura Agent. Секретов в коде нет: адрес сервера и FCM-конфиг
// приходят из gradle-свойств или переменных окружения (в CI — из GitHub Secrets).
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("org.jetbrains.kotlin.plugin.serialization")
}

fun cfg(name: String, default: String = ""): String =
    (project.findProperty(name) as String?)?.takeIf { it.isNotBlank() }
        ?: System.getenv(name)?.takeIf { it.isNotBlank() }
        ?: default

android {
    namespace = "ai.aura.agent"
    compileSdk = 34

    defaultConfig {
        applicationId = "ai.aura.agent"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"
        // Адрес бэкенда по умолчанию (10.0.2.2 — это localhost компьютера из эмулятора);
        // меняется на экране входа.
        buildConfigField("String", "DEFAULT_API_URL", "\"${cfg("AURA_API_URL", "http://10.0.2.2:8080")}\"")
        // FCM без google-services.json: параметры Firebase-приложения (не секрет, но и не в git).
        buildConfigField("String", "FCM_API_KEY", "\"${cfg("AURA_FCM_API_KEY")}\"")
        buildConfigField("String", "FCM_APP_ID", "\"${cfg("AURA_FCM_APP_ID")}\"")
        buildConfigField("String", "FCM_PROJECT_ID", "\"${cfg("AURA_FCM_PROJECT_ID")}\"")
        buildConfigField("String", "FCM_SENDER_ID", "\"${cfg("AURA_FCM_SENDER_ID")}\"")
    }

    buildTypes {
        release {
            isMinifyEnabled = false
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
        compose = true
        buildConfig = true
    }
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation(platform("androidx.compose:compose-bom:2024.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.5")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.5")
    implementation("androidx.security:security-crypto:1.1.0-alpha06")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    implementation(platform("com.google.firebase:firebase-bom:33.3.0"))
    implementation("com.google.firebase:firebase-messaging")

    testImplementation("junit:junit:4.13.2")
    testImplementation("com.squareup.okhttp3:mockwebserver:4.12.0")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.8.1")
}
