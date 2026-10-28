// M13/U2 — app module: WebView wrapper around the art dist (see build_apk.sh).
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// build_apk.sh passes -PdistVersion=<dist folder suffix> so the APK versionName tracks the
// bundled dist (e.g. M11-20261026-3).
val distVersion: String = providers.gradleProperty("distVersion").getOrElse("dev")
// U9: versionCode must INCREASE with every shipped build — Android only treats a sideload as an
// update when versionCode >= the installed one, and an equal versionCode lets an OLDER build
// silently replace a newer one (no downgrade protection). build_apk.sh derives it from the dist
// name (M11-20261027-9 -> 2026102709) and passes -PversionCode; the default below only covers
// raw `gradlew` dev runs, which are never shipped.
val buildVersionCode: String = providers.gradleProperty("versionCode").getOrElse("1")
// X3/M14: --hd passes -Phd=true -> applicationId .hd suffix + "Open Dungeon Raid HD"
// label (U14 rename 2026-10-28, M14/PLAN.md: installs next to the normal build).
val hd: Boolean = providers.gradleProperty("hd").getOrElse("false") == "true"

android {
    namespace = "local.dungeonraid.rebuild"
    compileSdk = 34

    defaultConfig {
        applicationId = "local.dungeonraid.rebuild" + if (hd) ".hd" else ""
        minSdk = 24
        targetSdk = 34
        versionCode = buildVersionCode.toInt()
        versionName = distVersion
        // Manifest label via placeholder so --hd can rename without touching strings.xml
        // (the normal build's label string is unchanged, byte-for-byte in badging).
        manifestPlaceholders["appLabel"] =
            if (hd) "Open Dungeon Raid HD" else "Open Dungeon Raid"
    }

    buildFeatures {
        // U4b: MainActivity gates WebView.setWebContentsDebuggingEnabled(true) on BuildConfig.DEBUG.
        buildConfig = true
    }

    buildTypes {
        debug {
            // U4b: testing-only variant on the shared redroid device. Same applicationId (the
            // task: uninstall the release first — same app id, different key), debug-signed by
            // AGP's default debug keystore. Release stays unchanged.
            isDebuggable = true
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            // The launcher icon must stay byte-for-byte the original game's icon.png
            // (Master change 2026-10-26) — build_apk.sh proves it by sha256 of the APK entry
            // vs `unzip -p` of the original APK (AGP 8.7 has no crunchPngs DSL property;
            // aapt2 link stores PNG resources as copied).
            // Signing happens in build_apk.sh with apksigner + ~/keys/dungeon-raid/release.jks,
            // so the private keystore never enters this project tree.
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    lint {
        // lintVital on release would run a full lint pass over a plumbing-only shell.
        checkReleaseBuilds = false
        abortOnError = false
    }
}

dependencies {
    // Only dependency: WebViewAssetLoader (assets served over https://appassets… so the
    // page's fetch()/relative asset URLs work without file:// CORS).
    implementation("androidx.webkit:webkit:1.11.0")
}
