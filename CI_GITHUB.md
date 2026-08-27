# Build automático no GitHub

O workflow `.github/workflows/android-build.yml` compila o APK debug automaticamente em todo push para `main` e também pode ser executado manualmente em **Actions → TruckBox Android Build → Run workflow**.

Etapas principais:
- Java 17 (Temurin)
- Android SDK / API 36
- Gradle 9.4.1
- `:app:assembleDebug`
- upload do `app-debug.apk` como artefato `TruckBox-Motorista-v0.6.1-debug`

O projeto não depende do `gradle-wrapper.jar` para o CI: o Gradle 9.4.1 é instalado pela própria action oficial `gradle/actions/setup-gradle`.
