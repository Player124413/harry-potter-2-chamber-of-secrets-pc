package com.hp2.android

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import android.os.Handler
import android.os.Looper
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

class TouchOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var editMode: Boolean = false
        set(value) {
            field = value
            invalidate()
        }

    var touchEnabled: Boolean = true
        set(value) {
            field = value
            invalidate()
        }

    /** Called when a button is pressed (true) or released (false). */
    var onButtonEvent: ((ButtonConfig, Boolean) -> Unit)? = null

    /** Called after any config change so the host can persist it. */
    var onConfigChanged: (() -> Unit)? = null

    private val density = resources.displayMetrics.density

    private val buttonPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(160, 122, 162, 247)
        style = Paint.Style.FILL
    }
    private val buttonStroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(220, 255, 255, 255)
        style = Paint.Style.STROKE
        strokeWidth = 2f * density
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        textSize = 22f * density
    }
    private val handlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(220, 255, 200, 80)
        style = Paint.Style.FILL
    }
    private val hiddenPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(120, 120, 120, 120)
        style = Paint.Style.FILL
    }

    private val handleSize = 14f * density

    // Active drag state.
    private data class DragState(
        val button: ButtonConfig,
        val mode: DragMode,
        val startTouchX: Float,
        val startTouchY: Float,
        val startX: Float,
        val startY: Float,
        val startW: Float,
        val startH: Float,
    )

    private enum class DragMode { MOVE, RESIZE_BR }

    private var dragState: DragState? = null
    private val longPressHandler = Handler(Looper.getMainLooper())
    private var longPressRunnable: Runnable? = null
    private val pressedButtons = mutableSetOf<String>()

    private val cfg: TouchConfigData get() = TouchConfig.current

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (!touchEnabled && !editMode) return

        for (btn in cfg.buttons) {
            if (!btn.visible && !editMode) continue
            val rect = RectF(btn.x, btn.y, btn.x + btn.width, btn.y + btn.height)
            val paint = if (btn.visible) buttonPaint else hiddenPaint
            canvas.drawRoundRect(rect, 16f * density, 16f * density, paint)
            canvas.drawRoundRect(rect, 16f * density, 16f * density, buttonStroke)
            val fm = textPaint.fontMetrics
            val cx = rect.centerX()
            val cy = rect.centerY() - (fm.ascent + fm.descent) / 2f
            canvas.drawText(btn.label, cx, cy, textPaint)

            if (editMode) {
                // Resize handle at bottom-right.
                val hx = rect.right - handleSize / 2f
                val hy = rect.bottom - handleSize / 2f
                canvas.drawCircle(hx, hy, handleSize, handlePaint)
                // Visibility indicator.
                val eyePaint = Paint(textPaint).apply { textSize = 12f * density }
                canvas.drawText(if (btn.visible) "👁" else "—", rect.left + 12f * density, rect.top + 16f * density, eyePaint)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!touchEnabled && !editMode) return false

        val x = event.x
        val y = event.y

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (editMode) {
                    // Check resize handle first.
                    val btn = findButtonAt(x, y, handle = true)
                    if (btn != null) {
                        dragState = DragState(btn, DragMode.RESIZE_BR, x, y, btn.x, btn.y, btn.width, btn.height)
                        return true
                    }
                    val btnMove = findButtonAt(x, y, handle = false)
                    if (btnMove != null) {
                        dragState = DragState(btnMove, DragMode.MOVE, x, y, btnMove.x, btnMove.y, btnMove.width, btnMove.height)
                        val r = Runnable { toggleVisibility(btnMove) }
                        longPressRunnable = r
                        longPressHandler.postDelayed(r, 500)
                        return true
                    }
                    return false
                } else {
                    val btn = findButtonAt(x, y, handle = false)
                    if (btn != null && btn.visible) {
                        pressedButtons.add(btn.id)
                        onButtonEvent?.invoke(btn, true)
                        invalidate()
                        return true
                    }
                    return false
                }
            }
            MotionEvent.ACTION_MOVE -> {
                longPressRunnable?.let { longPressHandler.removeCallbacks(it) }
                longPressRunnable = null
                val ds = dragState ?: return false
                if (editMode) {
                    when (ds.mode) {
                        DragMode.MOVE -> {
                            ds.button.x = ds.startX + (x - ds.startTouchX)
                            ds.button.y = ds.startY + (y - ds.startTouchY)
                        }
                        DragMode.RESIZE_BR -> {
                            ds.button.width = max(48f * density, ds.startW + (x - ds.startTouchX))
                            ds.button.height = max(48f * density, ds.startH + (y - ds.startTouchY))
                        }
                    }
                    invalidate()
                    return true
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                longPressRunnable?.let { longPressHandler.removeCallbacks(it) }
                longPressRunnable = null
                if (editMode) {
                    onConfigChanged?.invoke()
                    dragState = null
                    invalidate()
                    return true
                } else {
                    // Release any pressed button under finger.
                    val btn = findButtonAt(x, y, handle = false)
                    if (btn != null && pressedButtons.contains(btn.id)) {
                        pressedButtons.remove(btn.id)
                        onButtonEvent?.invoke(btn, false)
                    } else {
                        // Release all if finger lifted elsewhere.
                        for (id in pressedButtons.toList()) {
                            val b = cfg.buttons.firstOrNull { it.id == id }
                            if (b != null) onButtonEvent?.invoke(b, false)
                        }
                        pressedButtons.clear()
                    }
                    invalidate()
                    return true
                }
            }
        }
        return super.onTouchEvent(event)
    }

    private fun findButtonAt(x: Float, y: Float, handle: Boolean): ButtonConfig? {
        // Top-most: iterate reverse.
        for (btn in cfg.buttons.asReversed()) {
            if (!btn.visible && !editMode) continue
            if (handle && editMode) {
                val hx = btn.x + btn.width - handleSize / 2f
                val hy = btn.y + btn.height - handleSize / 2f
                if (abs(x - hx) <= handleSize && abs(y - hy) <= handleSize) {
                    return btn
                }
            } else {
                if (x in btn.x..(btn.x + btn.width) && y in btn.y..(btn.y + btn.height)) {
                    return btn
                }
            }
        }
        return null
    }

    /** Long-press handling for toggling visibility in edit mode. */
    override fun performLongClick(): Boolean {
        if (!editMode) return super.performLongClick()
        // Toggle the button currently under the last touch? We'll handle via a separate method.
        return true
    }

    fun toggleVisibility(btn: ButtonConfig) {
        btn.visible = !btn.visible
        onConfigChanged?.invoke()
        invalidate()
    }
}
