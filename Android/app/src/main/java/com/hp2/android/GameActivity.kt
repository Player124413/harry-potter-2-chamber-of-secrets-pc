package com.hp2.android

import android.os.Bundle
import android.view.KeyEvent
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.appcompat.app.AppCompatActivity
import com.hp2.android.databinding.ActivityGameBinding
import fi.iki.elonen.NanoHTTPD
import java.io.ByteArrayInputStream
import java.io.IOException

class GameActivity : AppCompatActivity() {

    private lateinit var binding: ActivityGameBinding
    private var server: Hp2Server? = null
    private var port: Int = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityGameBinding.inflate(layoutInflater)
        setContentView(binding.root)

        TouchConfig.load(this)

        // Start local server.
        val s = Hp2Server(this)
        try { s.start() } catch (e: Exception) { android.util.Log.e("Hp2Server", "start failed", e) }
        server = s
        port = s.listeningPort

        setupWebView()
        setupOverlay()
        setupButtons()

        binding.webView.loadUrl("http://127.0.0.1:$port/index.html")
    }

    private fun setupWebView() {
        val ws: WebSettings = binding.webView.settings
        ws.javaScriptEnabled = true
        ws.domStorageEnabled = true
        ws.allowFileAccess = false
        ws.mediaPlaybackRequiresUserGesture = false
        ws.cacheMode = WebSettings.LOAD_NO_CACHE
        ws.setRenderPriority(WebSettings.RenderPriority.HIGH)
        ws.mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
        binding.webView.setLayerType(android.view.View.LAYER_TYPE_HARDWARE, null)
        binding.webView.webViewClient = WebViewClient()
        binding.webView.webChromeClient = WebChromeClient()
    }

    private fun setupOverlay() {
        val overlay = binding.touchOverlay
        overlay.touchEnabled = TouchConfig.current.enabled
        overlay.onButtonEvent = { btn, down ->
            val code = btn.keyCode
            val keyEvent = if (down) {
                KeyEvent(KeyEvent.ACTION_DOWN, code)
            } else {
                KeyEvent(KeyEvent.ACTION_UP, code)
            }
            binding.webView.dispatchKeyEvent(keyEvent)
        }
        overlay.onConfigChanged = {
            TouchConfig.save(this)
        }
    }

    private fun setupButtons() {
        binding.btnEdit.setOnClickListener {
            val overlay = binding.touchOverlay
            overlay.editMode = !overlay.editMode
            binding.btnEdit.text = if (overlay.editMode) getString(R.string.btn_done) else getString(R.string.btn_edit)
        }
        binding.btnTouchToggle.setOnClickListener {
            val overlay = binding.touchOverlay
            TouchConfig.current.enabled = !TouchConfig.current.enabled
            overlay.touchEnabled = TouchConfig.current.enabled
            binding.btnTouchToggle.text = if (TouchConfig.current.enabled)
                getString(R.string.btn_touch_on) else getString(R.string.btn_touch_off)
            TouchConfig.save(this)
        }
        // initial label
        binding.btnTouchToggle.text = if (TouchConfig.current.enabled)
            getString(R.string.btn_touch_on) else getString(R.string.btn_touch_off)
    }

    override fun onDestroy() {
        super.onDestroy()
        try { server?.stop() } catch (_: Exception) { }
    }

    override fun onBackPressed() {
        if (binding.webView.canGoBack()) binding.webView.goBack() else super.onBackPressed()
    }
}

class Hp2Server(private val activity: GameActivity) : NanoHTTPD(0) {
    override fun serve(session: IHTTPSession): Response {
        val uri = session.uri.substringBefore('?')
        val path = if (uri == "/" || uri.isBlank()) "/index.html" else uri
        val assetPath = "web" + path
        return try {
            val bytes = activity.assets.open(assetPath).readBytes()
            val mime = mimeTypeFor(path)
            newFixedLengthResponse(Response.Status.OK, mime, ByteArrayInputStream(bytes), bytes.size.toLong()).apply {
                addHeader("Cross-Origin-Opener-Policy", "same-origin")
                addHeader("Cross-Origin-Embedder-Policy", "require-corp")
                addHeader("Cross-Origin-Resource-Policy", "same-origin")
                addHeader("Cache-Control", "no-store")
            }
        } catch (e: IOException) {
            newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "Not found: $path")
        }
    }

    private fun mimeTypeFor(path: String): String {
        return when {
            path.endsWith(".html") -> "text/html"
            path.endsWith(".js") -> "text/javascript"
            path.endsWith(".wasm") -> "application/wasm"
            path.endsWith(".css") -> "text/css"
            path.endsWith(".png") -> "image/png"
            path.endsWith(".json") -> "application/json"
            else -> "application/octet-stream"
        }
    }
}
