# Android port (arm64)

The 3.3.5a client as an Android app for phones and tablets (arm64-v8a), played by touch first, with gamepads and
USB / Bluetooth keyboards and mice as extras. It follows the macOS and Switch ports: the same POSIX layers, an
`Android/` folder where the platform differs, and one source tree on `main`, chosen by configuration.

## Decisions

| | Choice | Why |
|---|---|---|
| Rendering | A native **OpenGL ES 3.0** Gx device (`CGxDeviceGLES`), with the ARB vertex and fragment programs translated to GLSL ES and the fixed-function pipeline generated as GLSL | Android has no desktop OpenGL. A device of our own, rather than a translation library under the desktop one, can be tuned for mobile GPUs |
| Game data | A folder the player can reach: `/sdcard/WoW335/` by default (`Data/`, `WTF/`, `Interface/AddOns/`, `Logs/`), with **All files access** (`MANAGE_EXTERNAL_STORAGE`) | Copying 17 GB and add-ons from a PC or a file manager must be easy. Sideloaded APK only: Google Play doesn't allow that permission for this |
| Touch | **Hybrid**: floating joystick on the left, camera drag on the right, pinch to zoom, tap to click anything (action bars, windows, units), long press to right-click, and a few overlay buttons that work while moving and looking | Combat needs moving, looking and pressing at once; WoW's own action bars are already the best ability buttons |
| Testing | The Android emulator on this Mac (arm64 images run natively on Apple Silicon) for automated checks, and the user's phone for real touch, performance and GPU drivers | |
| App shell | `NativeActivity` (no Java of our own), with JNI calls for the soft keyboard, permissions and insets | No Gradle or AndroidX needed; the APK is built from the native library, the manifest and assets |
| Minimum | Android 11 (API 30), arm64-v8a, OpenGL ES 3.0 | All files access exists from Android 11 |

## How it fits the tree

- **CMake**: `cmake/AndroidToolchain.cmake` wraps the NDK's toolchain. `WOWDECOMP_PLATFORM_ANDROID` and
  `WOWDECOMP_PLATFORM_POSIX`. `wowdecomp_platform_sources` keeps `Mac/` folders and lets an `Android/` file replace the
  `Mac/` one of the same name, as `Switch/` does. The game builds as `libwow.so`, loaded by `NativeActivity`.
- **Os** (`Engine/Source/Os/Android/`): the window is the activity's `ANativeWindow`; input comes from the activity's
  input queue (multi-touch, gamepads, keyboards, mice); the clipboard, soft keyboard and system bars through JNI.
- **Files**: BlizzardCore's and Storm's POSIX file layers as on macOS, rooted at the game folder. Android's shared
  storage normally matches names without regard to case, as Windows does; the Switch's case resolver is there for
  devices where it doesn't.
- **Audio**: an FMOD output on AAudio (`fmodex/android/src/fmod_output_aaudio.cpp`), polled like the Switch's audren
  output.
- **Network**: POSIX sockets, as on macOS. The manifest asks for `INTERNET`.
- **Packaging**: `Tools/Android/build.sh` builds `libwow.so` with the NDK, then the APK with `aapt2`, `zipalign` and
  `apksigner` (a debug key, or the user's release key). The SwitchControls-style add-on for touch (`TouchControls`) is
  packed in the APK's assets and installed into `Interface/AddOns` at every start.

## The OpenGL ES device

`CGxDeviceGLES` derives from `CGxDevice` like the OpenGL and Direct3D devices, reusing the base class's state
tracking, buffer pools and texture management. What differs from the desktop OpenGL device:

- **Fixed function as generated shaders.** The state the game sets through the fixed-function path (texture stage
  combiners, alpha test, fog, up to 8 lights with color material, texture coordinate generation and matrices, point
  sizes, clip planes) forms a key; each key gets a GLSL ES 3.00 vertex and fragment shader, compiled once and cached.
  UI, particles, and anything drawn without the game's shaders go this way.
- **ARB programs to GLSL ES.** The `.bls` shader files hold ARB assembly (`arbvp1` / `arbfp1`). A translator turns
  each program into GLSL ES at load time (registers to locals, `program.env[n]` / `program.local[n]` to uniform
  arrays, `state.*` bindings to uniforms the device keeps current, each opcode to its expression), keyed by the
  program text and cached.
- **Textures.** DXT1/3/5 use `EXT_texture_compression_s3tc` where the GPU has it; Mali and some others don't, so
  there DXT is decoded to RGBA8 on load (a background thread, with the decoded size counted against memory). BGRA
  formats are swizzled on upload. Mipmaps, anisotropy and depth comparison (shadows) are core ES 3.0.
- **Draws.** Vertex buffer objects and `glDrawRangeElements` as on desktop, with vertex arrays bound through attribute
  locations instead of client arrays. No immediate mode or quads to replace.
- **Render targets** are framebuffer objects, as the macOS and Switch devices already use.
- **Not available.** Wireframe (`glPolygonMode`) is dropped. User clip planes are done with `gl_ClipDistance`
  (`EXT_clip_cull_distance`) or a discard.

It can be developed and checked on the Android emulator, whose OpenGL ES goes to this Mac's GPU, with the game's
own screenshots and `WOWDECOMP_FRAMEDUMP` as on macOS.

## Touch and small screens

The native input layer owns every finger and decides what it does, so moving, looking and pressing work at once with
the game's single mouse:

- **Left third**: a floating joystick where the thumb lands. It sends the movement keys (W/S, strafe Q/E), and
  pushing it to the edge runs.
- **Right of it, on the 3D world**: dragging turns the camera and character (mouselook); a quick tap clicks there
  (selects a unit); a long press right-clicks (talk, loot, attack).
- **On the UI** (action bars, windows, the minimap, unit frames): taps are left clicks, long presses right clicks,
  drags drag (items, spells), at the point touched.
- **Pinch** zooms the camera; a two-finger drag on a window scrolls it.
- **Overlay buttons** that the `TouchControls` add-on lays out and the native layer presses as keys, so they work
  with a thumb on the joystick and a finger on the camera: jump, interact (loot / talk / use nearby, the Switch
  port's), target nearest enemy, target nearest friendly or neutral, open chat, game menu, and a toggle to hide them.
- **Text**: tapping an edit box opens Android's keyboard; Back closes windows (Escape).

Small-screen improvements in the `TouchControls` add-on:

- UI scale from the screen's physical size (density), so text and buttons are readable on a phone; adjustable.
- Safe areas: the UI keeps clear of the notch, rounded corners and gesture bar (Android's insets).
- Bigger action buttons and keybind-free labels; auto loot on by default; the Switch port's interact and
  non-hostile targeting; a compact chat frame.
- Landscape only, the screen kept on while playing, and a 30 / 60 fps choice for battery.

Gamepads use the Switch port's mapping (play / cursor modes, the D-pad menu navigation).

## Milestones

1. **Tooling**: JDK, Android SDK command-line tools, NDK, build tools, the emulator and an arm64 system image.
2. **Boots**: `libwow.so` builds and links, the APK installs, and the app reaches the "no game data" message, then
   opens the archives with data in place (logs in `Logs/Console.txt` and `adb logcat`).
3. **Draws**: the GLES device renders the login screen correctly on the emulator.
4. **Plays**: into the world, rendering checked against the macOS client; sound through AAudio; network play.
5. **Touch**: the input router, overlay buttons and the small-screen improvements.
6. **Phone**: performance and GPU-driver fixes on the user's phone; a release build.

## Status

- **Milestone 1 (tooling): done.** JDK 17 (Homebrew), the SDK command-line tools, platform 35, build tools 35, NDK
  27.2, the emulator and an Android 15 arm64 image; the virtual device `wowtest` (Pixel 7).
- **Milestone 2 (boots): in progress.** `cmake -S . -B build/android -G Ninja
  -DCMAKE_TOOLCHAIN_FILE=cmake/AndroidToolchain.cmake` configures, and every shared library compiles. Linking
  `libwow.so` still needs the Android Os layer (input, window, clipboard, message boxes), the entry point and the GLES
  device: 33 symbols. Done so far on the Android side: the AAudio output, crash reports (signals into the Switch's
  crash context and machine-state log), the Registry.txt store, and the adapter's display modes.
