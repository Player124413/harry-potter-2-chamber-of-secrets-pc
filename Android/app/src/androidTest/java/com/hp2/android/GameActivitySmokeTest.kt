package com.hp2.android

import android.os.SystemClock
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GameActivitySmokeTest {

    @Test
    fun webAssemblyModuleOpensTheInGameLauncher() {
        AndroidWebSmokeProbe.reset()
        val scenario = ActivityScenario.launch(GameActivity::class.java)
        try {
            val deadline = SystemClock.elapsedRealtime() + 60_000L
            while (!AndroidWebSmokeProbe.launcherReady &&
                AndroidWebSmokeProbe.startupError == null &&
                SystemClock.elapsedRealtime() < deadline) {
                // Do not wait for the main looper to become idle: SDL/WebGL
                // continuously schedules frames while the launcher is open.
                SystemClock.sleep(500L)
            }

            assertNull(
                "WebView failed to start the game: ${AndroidWebSmokeProbe.startupError}",
                AndroidWebSmokeProbe.startupError,
            )
            assertTrue(
                "The WebAssembly engine did not render its launcher within 60 seconds. " +
                    "Last WebView console message: ${AndroidWebSmokeProbe.lastLog}",
                AndroidWebSmokeProbe.launcherReady,
            )
        } finally {
            scenario.close()
        }
    }
}
