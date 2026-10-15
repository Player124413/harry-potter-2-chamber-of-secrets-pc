# Harry Potter 2 — Android

Android wrapper for the repository's existing WebAssembly game build. It uses a
native launcher, a WebView served from a loopback-only local server, and a
configurable touch-control overlay.

## Features

- Launcher screen (New Game / Continue / select your own game-data folder).
- Android's Storage Access Framework (SAF) picker feeds the selected folder
  into the engine's existing retail/prototype import flow.
- The Android CI workflow builds and packages a dedicated wasm32,
  single-threaded WebAssembly engine; it is not downloaded at runtime. Android
  WebView does not expose `crossOriginIsolated` / `SharedArrayBuffer`, which the
  browser's pthread-enabled build requires, and wasm32 avoids requiring newer
  WebView support for WebAssembly Memory64.
- The normal browser build remains pthread-enabled and uses COOP/COEP. The
  Android-specific build avoids that unsupported WebView requirement.
- Virtual D-pad and action buttons send key events to the game.
- **Edit** mode: drag buttons to move, drag the bottom-right handle to resize,
  and long-press a button to toggle its visibility.
- **Touch On/Off** disables the touch overlay; settings persist across launches.

## Build locally

The repository pins the Emscripten SDK version in `ThirdParty/sources.json`
(currently 6.0.10). From the repository root, install/activate that emsdk and
build the Android-compatible module:

```sh
git clone --branch 6.0.10 --depth 1 https://github.com/emscripten-core/emsdk.git ../emsdk
../emsdk/emsdk install 6.0.10
../emsdk/emsdk activate 6.0.10
source ../emsdk/emsdk_env.sh
cmake --preset web-android
cmake --build --preset web-android
cp -a dist/web-android/. Android/app/src/main/assets/web/
```

The normal browser target is `web-wasm64`; it uses pthreads and must be served
with COOP/COEP headers. It is not the module packaged into the Android APK.

Then open the `Android/` folder in Android Studio (Hedgehog or newer) and build
or run the `app` configuration. A local Gradle 8.x installation is also
supported. Gradle intentionally fails early if `hp2_game.js` or
`hp2_game.wasm` is absent or if the JavaScript module is only a placeholder.

The GitHub Actions workflow performs the WebAssembly build, stages those exact
assets into the Android project, builds the debug APK, and uploads it as
`app-debug-apk`. The generated engine binaries are not committed to Git.

## Current status / known gap

This is still a work-in-progress Android wrapper, not a release-ready port.
The workflow builds the real game module and runs an Android-emulator smoke test
that checks the in-game launcher renders. The Android module is intentionally
single-threaded; streamed audio refills run synchronously, so frame-time and
audio behavior need validation. Importing a real game-data folder, map startup,
long-session stability, and touch-control feel also need validation on an
emulator or physical device with user-owned game data.

## Notes

- The WebView server binds only to `127.0.0.1`; cleartext is enabled so the
  WebView can access that local server. Game assets are not served to the LAN.
- Touch layout and enabled state are stored in SharedPreferences (`hp2_touch`).
- This project does not contain copyrighted game data. Use only your own copy
  of the original game files.
