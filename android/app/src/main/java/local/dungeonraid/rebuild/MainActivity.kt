package local.dungeonraid.rebuild

import android.annotation.SuppressLint
import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewClientCompat

/**
 * M13 — thin WebView wrapper around the final art build (milestones/M13/PLAN.md).
 * All game logic lives in the bundled page (rebuild/web/template.html); this Activity is
 * page plumbing only: fullscreen portrait WebView, keep-screen-on, asset loading via
 * androidx.webkit WebViewAssetLoader, BACK -> window.drBack() page hook, pause/resume of the
 * WebView. Private sideload build — never published.
 */
class MainActivity : Activity() {

    private lateinit var webView: WebView

    // https://appassets.androidplatform.net/assets/… serves app/src/main/assets/… so the
    // page's fetch()/ASSET_BASE 'assets/ripped' relative URLs work (no file:// CORS).
    private val assetLoader: WebViewAssetLoader by lazy {
        WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // U4b: CDP (DevTools) for the test harness — DEBUG builds ONLY (release unchanged).
        if (BuildConfig.DEBUG) WebView.setWebContentsDebuggingEnabled(true)

        webView = WebView(this)
        setContentView(webView)

        webView.settings.apply {
            javaScriptEnabled = true                   // PLAN: JS on
            domStorageEnabled = true                   // PLAN: DOM storage on (saves)
            mediaPlaybackRequiresUserGesture = false   // PLAN: sounds without a gesture
            allowFileAccess = false                    // content comes from the asset loader
        }
        webView.webViewClient = object : WebViewClientCompat() {
            override fun shouldInterceptRequest(
                view: WebView,
                request: WebResourceRequest
            ): WebResourceResponse? = assetLoader.shouldInterceptRequest(request.url)
        }

        hideSystemBars()
        webView.loadUrl(PAGE_URL)
    }

    // BACK: ask the page hook first. template.html exports window.drBack() (same function as
    // the desktop Escape handler): true = the page handled the key; false = main menu, where
    // the original exits on BACK (Game::BackPressed @0x26a98, M13/PLAN.md) -> finish().
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        webView.evaluateJavascript("window.drBack&&window.drBack()") { value ->
            if (value == "false") finish()
        }
    }

    override fun onPause() {
        webView.onPause()
        webView.pauseTimers()
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        webView.onResume()
        webView.resumeTimers()
        hideSystemBars()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()   // re-apply immersive after dialogs/system gestures
    }

    override fun onDestroy() {
        webView.destroy()
        super.onDestroy()
    }

    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.systemBars())
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility =
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                    View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        }
    }

    companion object {
        private const val PAGE_URL =
            "https://appassets.androidplatform.net/assets/index.html"
    }
}
