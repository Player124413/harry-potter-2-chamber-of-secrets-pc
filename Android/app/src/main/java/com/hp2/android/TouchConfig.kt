package com.hp2.android

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/**
 * Persistent configuration for the on-screen touch controls.
 *
 * Each button is described by a [ButtonConfig] (id, label, key code to send,
 * position in dp, size in dp, visibility). The whole configuration is stored
 * as JSON in SharedPreferences so it survives app restarts and can be reset
 * to defaults.
 */
data class ButtonConfig(
    val id: String,
    var label: String,
    var keyCode: Int,
    var x: Float,
    var y: Float,
    var width: Float,
    var height: Float,
    var visible: Boolean = true,
)

data class TouchConfigData(
    var enabled: Boolean = true,
    val buttons: MutableList<ButtonConfig> = mutableListOf(),
)

object TouchConfig {
    private const val PREFS = "hp2_touch"
    private const val KEY_JSON = "config_json"

    private val defaults: List<ButtonConfig> = listOf(
        ButtonConfig("left", "◀", android.view.KeyEvent.KEYCODE_DPAD_LEFT, 40f, 420f, 96f, 96f),
        ButtonConfig("right", "▶", android.view.KeyEvent.KEYCODE_DPAD_RIGHT, 240f, 420f, 96f, 96f),
        ButtonConfig("up", "▲", android.view.KeyEvent.KEYCODE_DPAD_UP, 140f, 320f, 96f, 96f),
        ButtonConfig("down", "▼", android.view.KeyEvent.KEYCODE_DPAD_DOWN, 140f, 520f, 96f, 96f),
        ButtonConfig("action", "A", android.view.KeyEvent.KEYCODE_SPACE, 900f, 420f, 112f, 112f),
        ButtonConfig("jump", "B", android.view.KeyEvent.KEYCODE_ENTER, 1040f, 420f, 112f, 112f),
        ButtonConfig("menu", "☰", android.view.KeyEvent.KEYCODE_ESCAPE, 40f, 40f, 80f, 80f),
    )

    var current: TouchConfigData = TouchConfigData(true, defaults.map { it.copy() }.toMutableList())
        private set

    fun load(context: Context) {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val raw = prefs.getString(KEY_JSON, null)
        current = if (raw.isNullOrBlank()) {
            TouchConfigData(true, defaults.map { it.copy() }.toMutableList())
        } else {
            try {
                fromJson(raw)
            } catch (e: Exception) {
                TouchConfigData(true, defaults.map { it.copy() }.toMutableList())
            }
        }
    }

    fun save(context: Context) {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_JSON, toJson(current)).apply()
    }

    fun reset(context: Context) {
        current = TouchConfigData(true, defaults.map { it.copy() }.toMutableList())
        save(context)
    }

    private fun fromJson(raw: String): TouchConfigData {
        val obj = JSONObject(raw)
        val enabled = obj.optBoolean("enabled", true)
        val arr = obj.optJSONArray("buttons") ?: JSONArray()
        val buttons = mutableListOf<ButtonConfig>()
        for (i in 0 until arr.length()) {
            val b = arr.getJSONObject(i)
            buttons.add(
                ButtonConfig(
                    id = b.getString("id"),
                    label = b.optString("label", ""),
                    keyCode = b.optInt("keyCode", android.view.KeyEvent.KEYCODE_UNKNOWN),
                    x = b.optDouble("x", 0.0).toFloat(),
                    y = b.optDouble("y", 0.0).toFloat(),
                    width = b.optDouble("width", 96.0).toFloat(),
                    height = b.optDouble("height", 96.0).toFloat(),
                    visible = b.optBoolean("visible", true),
                )
            )
        }
        if (buttons.isEmpty()) {
            buttons.addAll(defaults.map { it.copy() })
        }
        return TouchConfigData(enabled, buttons)
    }

    private fun toJson(cfg: TouchConfigData): String {
        val obj = JSONObject()
        obj.put("enabled", cfg.enabled)
        val arr = JSONArray()
        for (b in cfg.buttons) {
            val o = JSONObject()
            o.put("id", b.id)
            o.put("label", b.label)
            o.put("keyCode", b.keyCode)
            o.put("x", b.x.toDouble())
            o.put("y", b.y.toDouble())
            o.put("width", b.width.toDouble())
            o.put("height", b.height.toDouble())
            o.put("visible", b.visible)
            arr.put(o)
        }
        obj.put("buttons", arr)
        return obj.toString()
    }
}
