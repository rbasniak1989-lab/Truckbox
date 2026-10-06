plugins {
    id("com.android.application")
}

android {
    namespace = "br.com.truckbox.recovery"
    compileSdk = 36

    defaultConfig {
        applicationId = "br.com.truckbox.recovery"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "1.0.1"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
