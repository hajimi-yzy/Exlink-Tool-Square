import java.net.URI
import java.util.Properties

plugins { id("com.android.application"); id("org.jetbrains.kotlin.android"); id("org.jetbrains.kotlin.plugin.compose") }

fun buildConfigString(value: String) = "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\""

val privateCloudConfig = Properties().apply {
    rootProject.file("cloud.local.properties").takeIf { it.isFile }?.inputStream()?.use { load(it) }
}
fun privateSetting(name: String) = providers.environmentVariable(name).orNull
    ?: privateCloudConfig.getProperty(name).orEmpty()

val apiBaseUrl = privateSetting("EXLINK_API_BASE_URL").trim().trimEnd('/')
val tlsFingerprint = privateSetting("EXLINK_TLS_FINGERPRINT_SHA256")
    .replace(Regex("[:\\s]"), "").uppercase()
val cloudConfigured = apiBaseUrl.isNotEmpty() || tlsFingerprint.isNotEmpty()
if (cloudConfigured) {
    val apiUri = runCatching { URI(apiBaseUrl) }
        .getOrElse { throw GradleException("EXLINK_API_BASE_URL must be a valid HTTPS origin") }
    val apiHost = apiUri.host.orEmpty()
    val validIpv4 = apiHost.split('.').let { parts ->
        parts.size == 4 && parts.all { part ->
            part.isNotEmpty() && part.all(Char::isDigit) && part.toIntOrNull()?.let { it in 0..255 } == true
        }
    }
    if (apiUri.scheme != "https" || !validIpv4 || apiUri.userInfo != null || apiUri.query != null ||
        apiUri.fragment != null || (apiUri.path.isNotEmpty() && apiUri.path != "/") ||
        (apiUri.port != -1 && apiUri.port !in 1..65535)) {
        throw GradleException("EXLINK_API_BASE_URL must be an HTTPS origin with an IPv4 host")
    }
    if (!tlsFingerprint.matches(Regex("[0-9A-F]{64}"))) {
        throw GradleException("Both cloud settings are required; EXLINK_TLS_FINGERPRINT_SHA256 must be a SHA-256 certificate fingerprint")
    }
}

android {
    namespace = "com.exlink.mobile"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.exlink.mobile"
        minSdk = 26
        targetSdk = 35
        versionCode = 5
        versionName = "0.4.0"
        buildConfigField("String", "EXLINK_API_BASE_URL", buildConfigString(apiBaseUrl))
        buildConfigField("String", "EXLINK_TLS_FINGERPRINT_SHA256", buildConfigString(tlsFingerprint))
    }
    buildFeatures { compose = true; buildConfig = true }
    buildTypes {
        getByName("release") {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
