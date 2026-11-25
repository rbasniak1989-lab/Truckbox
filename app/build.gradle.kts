plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "br.com.truckbox.app"
    compileSdk = 36

    defaultConfig {
        applicationId = "br.com.truckbox.driver"
        minSdk = 26
        targetSdk = 36
        versionCode = 67
        versionName = "0.6.7"
        resValue("string", "app_name", "TruckBox Motorista")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildFeatures {
        compose = true
        resValues = true
    }
    buildTypes {
        create("cloudTest") {
            initWith(getByName("debug"))
            applicationIdSuffix = ".rotas"
            resValue("string", "app_name", "TruckBox Motorista Rotas")
            matchingFallbacks += "debug"
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    testOptions {
        unitTests.isIncludeAndroidResources = true
        unitTests.all {
            it.maxHeapSize = "2g"
            val testHome=layout.buildDirectory.dir("test-home").get().asFile
            it.systemProperty("user.home",testHome.absolutePath)
            it.systemProperty("robolectric.dependency.repo.url","https://repo.maven.apache.org/maven2")
            it.doFirst {testHome.mkdirs()}
        }
    }
    lint {abortOnError = true}

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2026.06.01")
    implementation(composeBom)

    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.10.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    implementation("com.google.code.gson:gson:2.13.2")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.16.1")
    testImplementation(composeBom)
    testImplementation("androidx.compose.ui:ui-test-junit4")
    testImplementation("androidx.test.ext:junit:1.3.0")
    testImplementation("androidx.test:runner:1.7.0")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
    androidTestImplementation(composeBom)
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
