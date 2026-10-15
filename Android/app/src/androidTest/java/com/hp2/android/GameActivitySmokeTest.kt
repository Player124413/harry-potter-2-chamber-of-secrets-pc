package com.hp2.android

import android.os.SystemClock
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GameActivitySmokeTest {

    @Test
    fun webAssemblyModuleOpensTheInGameLauncher() {
        AndroidWebSmokeProbe.reset()
        val instrumentation = InstrumentationRegistry.getInstrumentation()

        val scenario = ActivityScenario.launch(GameActivity::class.java)
        try {
            val deadline = SystemClock.elapsedRealtime() + 90_000L
            while (!AndroidWebSmokeProbe.launcherReady &&
                AndroidWebSmokeProbe.startupError == null &&
                SystemClock.elapsedRealtime() < deadline) {
                instrumentation.waitForIdleSync()
                SystemClock.sleep(500L)
            }

            assertNull(
                "WebView failed to start the game: ${AndroidWebSmokeProbe.startupError}",
                AndroidWebSmokeProbe.startupError,
            )
            assertTrue(
                "The WebAssembly engine did not render its launcher within 90 seconds. " +
                    "Last WebView console message: ${AndroidWebSmokeProbe.lastLog}",
                AndroidWebSmokeProbe.launcherReady,
            )
        } finally {
            scenario.close()
        }
    }
}
