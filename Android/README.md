# Harry Potter 2 — Android

Android wrapper for the repository's existing WebAssembly game build. It uses a
native launcher, a WebView served from a loopback-only local server, and a
configurable touch-control overlay.

## Features

- Launcher screen (New Game / Continue / select your own game-data folder).
- Android's Storage Access Framework (SAF) picker feeds the selected folder
  into the engine's existing retail/prototype import flow.
- The Android CI workflow builds and packages the WebAssembly engine; it is not
  downloaded at runtime.
- COOP/COEP headers are served locally for the isolated WebAssembly runtime.
- Virtual D-pad and action buttons send key events to the game.
- **Edit** mode: drag buttons to move, drag the bottom-right handle to resize,
  and long-press a button to toggle its visibility.
- **Touch On/Off** disables the touch overlay; settings persist across launches.

## Build locally

The repository pins the Emscripten SDK version in `ThirdParty/sources.json`
(currently 6.0.10). From the repository root, install/activate that emsdk and
build the browser module:

```sh
git clone --branch 6.0.10 --depth 1 https://github.com/emscripten-core/emsdk.git ../emsdk
../emsdk/emsdk install 6.0.10
../emsdk/emsdk activate 6.0.10
source ../emsdk/emsdk_env.sh
cmake --preset web-wasm64
cmake --build --preset web-wasm64
cp -a dist/web-wasm64/. Android/app/src/main/assets/web/
```

Then open the `Android/` folder in Android Studio (Hedgehog or newer) and build
or run the `app` configuration. A local Gradle 8.x installation is also
supported. Gradle intentionally fails early if `hp2_game.js` or
`hp2_game.wasm` is absent or if the JavaScript module is only a placeholder.

The GitHub Actions workflow performs the WebAssembly build, stages those exact
assets into the Android project, builds the debug APK, and uploads it as
`app-debug-apk`. The generated engine binaries are not committed to Git.

## Current status / known gap

This is still a work-in-progress Android wrapper, not a release-ready port.
The workflow builds the real game module into the APK, and the native SAF
picker is connected to the existing import flow, but launching, importing data,
and playing on an Android device have not yet been verified on an emulator or
physical device.

## Notes

- The WebView server binds only to `127.0.0.1`; cleartext is enabled so the
  WebView can access that local server. Game assets are not served to the LAN.
- Touch layout and enabled state are stored in SharedPreferences (`hp2_touch`).
- This project does not contain copyrighted game data. Use only your own copy
  of the original game files.
