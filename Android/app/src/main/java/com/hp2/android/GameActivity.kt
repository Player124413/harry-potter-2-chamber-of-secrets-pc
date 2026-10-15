package com.hp2.android

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.KeyEvent
import android.webkit.ConsoleMessage
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import com.hp2.android.databinding.ActivityGameBinding
import fi.iki.elonen.NanoHTTPD
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap
import java.util.UUID
import java.util.concurrent.Executors

class GameActivity : AppCompatActivity() {

    private data class FolderRequest(val id: String, val kind: String)

    private lateinit var binding: ActivityGameBinding
    private var server: Hp2Server? = null
    private var port: Int = 0
    private var pendingFolderRequest: FolderRequest? = null
    private val importExecutor = Executors.newSingleThreadExecutor()

    private val folderPicker = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        val request = pendingFolderRequest
        pendingFolderRequest = null
        if (request == null) return@registerForActivityResult
        if (uri == null) {
            sendFolderResult(request, "Folder selection cancelled.")
            return@registerForActivityResult
        }
        try {
            contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
            getSharedPreferences("hp2", MODE_PRIVATE).edit()
                .putString("data_tree_uri", uri.toString()).apply()
        } catch (e: Exception) {
            sendFolderResult(request, "Could not keep access to the selected folder: $e")
            return@registerForActivityResult
        }
        readFolderTree(request, uri, fromSavedGrant = false)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityGameBinding.inflate(layoutInflater)
        setContentView(binding.root)

        TouchConfig.load(this)

        val localServer = Hp2Server(this)
        try {
            localServer.start()
        } catch (e: Exception) {
            val message = "Local game server failed to start: $e"
            AndroidWebSmokeProbe.fail(message)
            android.util.Log.e("Hp2Server", message, e)
            finish()
            return
        }
        server = localServer
        port = localServer.listeningPort

        setupWebView()
        setupOverlay()
        setupButtons()
        binding.webView.loadUrl("http://127.0.0.1:$port/index.html?hp2AndroidSingleThreaded=1")
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
        binding.webView.webViewClient = object : WebViewClient() {
            override fun onPageStarted(view: WebView?, url: String?, favicon: android.graphics.Bitmap?) {
                AndroidWebSmokeProbe.record("WebView page started: ${url ?: "<unknown>"}")
            }

            override fun onPageFinished(view: WebView?, url: String?) {
                AndroidWebSmokeProbe.record("WebView page finished: ${url ?: "<unknown>"}")
                view?.evaluateJavascript(
                    "(function(){var s=document.getElementById('status');var n=navigator.storage;" +
                        "return JSON.stringify({readyState:document.readyState," +
                        "status:s?s.textContent:null,crossOriginIsolated:!!self.crossOriginIsolated," +
                        "opfs:!!(n&&n.getDirectory)});})()",
                ) { state -> AndroidWebSmokeProbe.record("WebView state: $state") }
            }

            override fun onReceivedError(
                view: WebView?, request: WebResourceRequest, error: WebResourceError,
            ) {
                val message = "WebView load error ${error.errorCode} for ${request.url}: ${error.description}"
                android.util.Log.e("HP2Web", message)
                if (request.isForMainFrame) AndroidWebSmokeProbe.fail(message)
                else AndroidWebSmokeProbe.record(message)
            }

            override fun onReceivedHttpError(
                view: WebView?, request: WebResourceRequest, response: WebResourceResponse,
            ) {
                val message = "WebView HTTP ${response.statusCode} ${response.reasonPhrase} for ${request.url}"
                android.util.Log.e("HP2Web", message)
                val path = request.url.path.orEmpty()
                val requiredAsset = path.endsWith("/hp2_game.js") ||
                    path.endsWith("/hp2_game.wasm") || path.endsWith("/hp2-web.js")
                if (request.isForMainFrame || requiredAsset) AndroidWebSmokeProbe.fail(message)
                else AndroidWebSmokeProbe.record(message)
            }

            override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest): Boolean {
                val uri = request.url
                // Keep the JavaScript interface confined to the bundled same-origin game.
                return uri.scheme != "http" || uri.host != "127.0.0.1" || uri.port != port
            }
        }
        binding.webView.webChromeClient = object : WebChromeClient() {
            override fun onConsoleMessage(message: ConsoleMessage): Boolean {
                AndroidWebSmokeProbe.record(message.message())
                android.util.Log.d("HP2Web", message.message())
                return true
            }
        }
        binding.webView.addJavascriptInterface(AndroidGameDataBridge(this), "Hp2Android")
    }

    private fun setupOverlay() {
        val overlay = binding.touchOverlay
        overlay.touchEnabled = TouchConfig.current.enabled
        overlay.onButtonEvent = { btn, down ->
            val keyEvent = KeyEvent(
                if (down) KeyEvent.ACTION_DOWN else KeyEvent.ACTION_UP,
                btn.keyCode,
            )
            binding.webView.dispatchKeyEvent(keyEvent)
        }
        overlay.onConfigChanged = { TouchConfig.save(this) }
    }

    private fun setupButtons() {
        binding.btnEdit.setOnClickListener {
            val overlay = binding.touchOverlay
            overlay.editMode = !overlay.editMode
            binding.btnEdit.text = if (overlay.editMode) getString(R.string.btn_done)
            else getString(R.string.btn_edit)
        }
        binding.btnTouchToggle.setOnClickListener {
            val overlay = binding.touchOverlay
            TouchConfig.current.enabled = !TouchConfig.current.enabled
            overlay.touchEnabled = TouchConfig.current.enabled
            binding.btnTouchToggle.text = if (TouchConfig.current.enabled)
                getString(R.string.btn_touch_on) else getString(R.string.btn_touch_off)
            TouchConfig.save(this)
        }
        binding.btnTouchToggle.text = if (TouchConfig.current.enabled)
            getString(R.string.btn_touch_on) else getString(R.string.btn_touch_off)
    }

    /** Called only by the trusted same-origin WebView JavaScript bridge. */
    fun requestGameDataFolder(requestId: String, kind: String) {
        runOnUiThread {
            if (kind != "retail" && kind != "prototype") {
                sendFolderResult(FolderRequest(requestId, kind), "Unknown game-data type.")
                return@runOnUiThread
            }
            if (requestId.toLongOrNull() == null) {
                sendFolderResult(FolderRequest(requestId, kind), "Invalid folder request ID.")
                return@runOnUiThread
            }

            val request = FolderRequest(requestId, kind)
            val savedUri = getSharedPreferences("hp2", MODE_PRIVATE)
                .getString("data_tree_uri", null)
            if (savedUri != null) {
                readFolderTree(request, Uri.parse(savedUri), fromSavedGrant = true)
            } else {
                pendingFolderRequest = request
                folderPicker.launch(null)
            }
        }
    }

    fun releaseImportedFolder(requestId: String) {
        server?.releaseFolder(requestId)
    }

    private fun readFolderTree(request: FolderRequest, uri: Uri, fromSavedGrant: Boolean) {
        importExecutor.execute {
            try {
                val root = DocumentFile.fromTreeUri(this, uri)
                    ?: throw IOException("The selected folder is no longer available.")
                val files = mutableListOf<AndroidImportFile>()
                val visitedDirectories = HashSet<String>()

                fun visit(directory: DocumentFile, parentPath: String) {
                    if (!visitedDirectories.add(directory.uri.toString())) return
                    for (child in directory.listFiles()) {
                        val name = child.name?.takeIf { it.isNotBlank() } ?: continue
                        val relativePath = if (parentPath.isEmpty()) name else "$parentPath/$name"
                        if (child.isDirectory) {
                            visit(child, relativePath)
                        } else if (child.isFile) {
                            files.add(AndroidImportFile(relativePath, child.uri, child.length()))
                        }
                    }
                }

                visit(root, "")
                if (files.isEmpty()) throw IOException("No files were found in the selected folder.")
                val localServer = server ?: throw IOException("The local game server is stopped.")
                val token = localServer.registerFolder(request.id, files)
                runOnUiThread {
                    if (!isFinishing && !isDestroyed) sendFolderResult(request, null, token)
                    else localServer.releaseFolder(request.id)
                }
            } catch (e: Exception) {
                android.util.Log.e("Hp2Import", "Could not read selected game-data folder", e)
                if (fromSavedGrant && !isFinishing && !isDestroyed) {
                    getSharedPreferences("hp2", MODE_PRIVATE).edit()
                        .remove("data_tree_uri").apply()
                    runOnUiThread {
                        if (isFinishing || isDestroyed) return@runOnUiThread
                        pendingFolderRequest = request
                        folderPicker.launch(null)
                    }
                } else {
                    runOnUiThread {
                        if (!isFinishing && !isDestroyed) sendFolderResult(request, e.message ?: e.toString())
                    }
                }
            }
        }
    }

    private fun sendFolderResult(request: FolderRequest, error: String?, token: String? = null) {
        val id = JSONObject.quote(request.id)
        val kind = JSONObject.quote(request.kind)
        val errorJson = if (error == null) "null" else JSONObject.quote(error)
        val tokenJson = if (token == null) "null" else JSONObject.quote(token)
        binding.webView.evaluateJavascript(
            "window.hp2AndroidFolderResult($id, $kind, null, $errorJson, $tokenJson);",
            null,
        )
    }

    override fun onDestroy() {
        importExecutor.shutdownNow()
        binding.webView.removeJavascriptInterface("Hp2Android")
        binding.webView.stopLoading()
        binding.webView.destroy()
        try {
            server?.stop()
        } catch (_: Exception) {
        }
        super.onDestroy()
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (binding.webView.canGoBack()) binding.webView.goBack() else super.onBackPressed()
    }
}

object AndroidWebSmokeProbe {
    @Volatile var launcherReady: Boolean = false
        private set
    @Volatile var startupError: String? = null
        private set
    @Volatile var lastLog: String = ""
        private set

    fun reset() {
        launcherReady = false
        startupError = null
        lastLog = ""
    }

    fun fail(message: String) {
        val detail = message.take(1000)
        startupError = detail
        lastLog = detail
    }

    fun record(message: String) {
        lastLog = message.take(1000)
        if (message.contains("HP2Launcher: ready")) launcherReady = true
        if (message.contains("Web startup failed") ||
            message.contains("This browser has no private file storage") ||
            message.contains("WebGL2 is unavailable")) {
            startupError = message.take(1000)
        }
    }
}

class AndroidGameDataBridge(private val activity: GameActivity) {
    @JavascriptInterface
    fun pickFolder(requestId: String, kind: String) {
        activity.requestGameDataFolder(requestId, kind)
    }

    @JavascriptInterface
    fun releaseFolder(requestId: String) {
        activity.releaseImportedFolder(requestId)
    }
}

internal data class AndroidImportFile(
    val relativePath: String,
    val uri: Uri,
    val length: Long,
)

class Hp2Server(private val activity: GameActivity) : NanoHTTPD("127.0.0.1", 0) {
    private val importedFolders = ConcurrentHashMap<String, List<AndroidImportFile>>()
    private val requestTokens = ConcurrentHashMap<String, String>()

    internal fun registerFolder(requestId: String, files: List<AndroidImportFile>): String {
        releaseFolder(requestId)
        val token = UUID.randomUUID().toString().replace("-", "")
        importedFolders[token] = files
        requestTokens[requestId] = token
        return token
    }

    fun releaseFolder(requestId: String) {
        requestTokens.remove(requestId)?.let(importedFolders::remove)
    }

    override fun serve(session: IHTTPSession): Response {
        val uri = session.uri.substringBefore('?')
        val segments = uri.split('/').filter { it.isNotEmpty() }
        if (segments.size == 3 && segments[0] == "__hp2_android_import") {
            return serveImportedFile(segments[1], segments[2])
        }

        val path = if (uri == "/" || uri.isBlank()) "/index.html" else uri
        val assetPath = "web$path"
        return try {
            val bytes = activity.assets.open(assetPath).use { it.readBytes() }
            newFixedLengthResponse(
                Response.Status.OK,
                mimeTypeFor(path),
                bytes.inputStream(),
                bytes.size.toLong(),
            ).withIsolationHeaders()
        } catch (e: IOException) {
            newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "Not found: $path")
        }
    }

    private fun serveImportedFile(requestId: String, item: String): Response {
        val files = importedFolders[requestId]
            ?: return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "Import expired")
        if (item == "manifest") {
            val manifest = JSONArray()
            files.forEachIndexed { index, file ->
                manifest.put(JSONObject().put("path", file.relativePath).put("index", index))
            }
            return newFixedLengthResponse(
                Response.Status.OK,
                "application/json",
                manifest.toString(),
            ).withIsolationHeaders()
        }

        val index = item.toIntOrNull()
            ?: return newFixedLengthResponse(Response.Status.BAD_REQUEST, "text/plain", "Invalid file index")
        val file = files.getOrNull(index)
            ?: return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "File not found")
        return try {
            val input = activity.contentResolver.openInputStream(file.uri)
                ?: return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "File unavailable")
            val response = if (file.length > 0L) {
                newFixedLengthResponse(Response.Status.OK, "application/octet-stream", input, file.length)
            } else {
                newChunkedResponse(Response.Status.OK, "application/octet-stream", input)
            }
            response.withIsolationHeaders()
        } catch (e: Exception) {
            newFixedLengthResponse(Response.Status.INTERNAL_ERROR, "text/plain", "Could not read file")
        }
    }

    private fun Response.withIsolationHeaders(): Response = apply {
        addHeader("Cross-Origin-Opener-Policy", "same-origin")
        addHeader("Cross-Origin-Embedder-Policy", "require-corp")
        addHeader("Cross-Origin-Resource-Policy", "same-origin")
        addHeader("Cache-Control", "no-store")
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
