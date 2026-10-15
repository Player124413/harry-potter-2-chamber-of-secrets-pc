# Harry Potter 2 — Android port

This module wraps the existing WebAssembly build of the game in an Android
application with a native-feeling launcher and on-screen touch controls.

## Features
- Launcher screen (New Game / Continue / Import game data via SAF).
- In-game WebView running the cross-origin-isolated WebAssembly build.
- On-screen touch controls (virtual D-pad + action buttons) sending key events.
- **Edit mode** (Edit button): drag buttons to move, drag the bottom-right
  handle to resize, long-press a button to toggle its visibility.
- **Touch on/off** toggle to disable all on-screen controls.
- Configuration persists across launches (SharedPreferences).

## Build
Open the `Android/` folder in Android Studio (Hedgehog or newer) and run the
`app` configuration. The Gradle wrapper is not vendored; use a local Gradle
8.x installation or let Android Studio download one.

Before shipping, replace `app/src/main/assets/web/hp2_game.js` and add
`hp2_game.wasm` with the output of the web build:

    cmake --preset web-wasm64
    cmake --build --preset web-wasm64

then copy `dist/web-wasm64/*` into `app/src/main/assets/web/`.

## Notes
- The WebView requires COOP/COEP headers; a tiny in-process NanoHTTPD server
  serves the assets on 127.0.0.1 with those headers.
- Touch button layout is stored as JSON in SharedPreferences (`hp2_touch`).
