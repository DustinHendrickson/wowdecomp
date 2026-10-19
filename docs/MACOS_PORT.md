# macOS (Apple Silicon) port plan

Branch: `macos-arm64`. Goal: build and run the client natively on arm64 macOS (M-series) with
Apple Clang + CMake, without breaking the Windows x64/Win32 builds or their `// OFFSET:` comparability.

## Where we start

Measured on `main` at b1c71b8:

| Area | Size of the problem |
|---|---|
| Win64 platform layers (`Os`, `Storm`, `BlizzardCore/System/*`, `Streaming`, `WowServices`, `Gx/CGxDevice/Win64`, ...) | ~40k lines across ~25 `Win64/` dirs |
| Files including `<windows.h>` | 179 (56 BlizzardCore, 40 Os, 26 Storm, 15 SoundEngine, the rest scattered) |
| `DWORD` / `HANDLE`/`HWND` / `WINAPI`-style call conventions | ~1,900 / ~390 / ~1,700 hits |
| `long` / `unsigned long` (32-bit on Windows, **64-bit on macOS**) | ~645 hits in 116 files |
| SEH `__try/__except` | 15 files (mostly platform layers, D3D, FMOD win32) |
| SSE intrinsics | 6 files (`M2Model`, `M2Scene`, `CharacterComponent`, ComSat SPIRIT, vorbis) |
| MSVC inline `__asm` | Win32-only files + a few FMOD/lua spots |
| Direct3D 9 / 9Ex backends | Windows only, excluded on Mac |
| OpenGL backend (`CGxDeviceOpenGl`, ~6.6k lines, ARB vp/fp + NV paths) | Exists; only window/context glue is WGL |
| FMOD Ex | Source is in-tree; only the `win32/` platform layer (DirectSound/WinMM/ASIO) is Windows-specific |
| Prebuilt Windows-only binaries | `xvidcore/lib/x64/libxvidcore.lib`, `nvapi`, `lgLcd`, DivxDecoder |

Good news that makes this tractable:

- The code already separates platform code into `Win32/` / `Win64/` folders behind portable headers
  (`Os/*.h`, `Storm/H`, `BlizzardCore/System/*`). The original Blizzard client had Mac twins of these,
  so a `Mac/` folder per module is the natural shape.
- x64 is already the main target, so pointer-size issues are mostly solved. What changes is the
  data model: Windows is LLP64, macOS is LP64.
- The OpenGL device is decompiled. The original 3.3.5a Mac client ran on OpenGL using the
  `arbvp1`/`arbfp1` shaders that ship in the standard game MPQs. Apple Silicon's legacy OpenGL 2.1
  profile (layered on Metal) still exposes `ARB_vertex_program`, `ARB_fragment_program`, VBOs, FBOs and
  occlusion queries. Deprecated, but present on macOS 26.
- arm64 macOS has signed `char` like MSVC x86, and handles unaligned loads, so most game logic should
  port as-is.
- [whoa](https://github.com/whoahq/whoa) and [thunderbrew](https://github.com/thunderbrewhq/thunderbrew)
  (both credited in the README) are cross-platform reimplementations of this client with macOS
  support. Use them as reference for the Mac platform layer and GL context setup.

## Toolchain (this machine)

Present: Xcode with Apple Clang 17, Homebrew. Missing: CMake and Ninja.

```bash
brew install cmake ninja
```

Build command once the port lands:

```bash
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
```

```bash
cmake --build build/mac --target WowClient
```

## Phases

Each phase ends with something that builds or runs, so progress can be checked as you go.

### Phase 0: CMake configures on macOS

- Root `CMakeLists.txt`: drop the `Windows only` fatal error. Add `WOWDECOMP_PLATFORM_WIN` /
  `WOWDECOMP_PLATFORM_MAC` definitions. Only define `WIN32`, `_WINDOWS`, `WIN32_LEAN_AND_MEAN` on Windows
  (today they're unconditional, which would make every `#ifdef WIN32` take the Windows path).
- Pick sources per platform. Most modules use `GLOB_RECURSE "*.cpp"` and rely on
  `#if defined(WOWDECOMP_ARCH_64)` guards inside `Win64/` files. Add a shared helper
  (`cmake/Platform.cmake`) that filters out `/Win32/` and `/Win64/` dirs on Mac and adds `/Mac/`.
- Guard Windows-only link libraries (`d3d9.lib`, `opengl32`, `ws2_32`, `winmm`, `dinput8`, `imm32`,
  `wininet`, `nvapi`, ...). Link the Mac frameworks instead: `Cocoa`, `OpenGL`, `IOKit`, `CoreAudio`,
  `AudioToolbox`, `Carbon` (keyboard layouts and TIS input sources), `Security`, `CoreServices`.
- Skip `waystone.rc`, `Tools/XvidDecoder`, nvapi, lgLcd and SteelseriesMouse on Mac.
- Add a Clang warning baseline (`-Wno-microsoft-*`, `-fms-extensions` only where needed, `-fno-strict-aliasing`
  since decompiled code type-puns heavily).

**Done when:** `cmake -S . -B build/mac` configures, and ThirdParty (zlib, bzip2, lzma, expat, freetype,
pcre, lua) builds.

**Status: done.** `cmake/Platform.cmake` picks sources per platform (`wowdecomp_platform_sources`), and
`Engine/Source/Base/Platform.h` is force-included on macOS. Configure succeeds, and ThirdParty, `jpeg`, `lua` and
`pcreposix` build for arm64. Full keep-going build: 730 failed objects. The errors that remain, in order of
impact (they open Phase 1):

1. `<windows.h>` in widely shared headers: `BlizzardCore/.../Lock/Lock.h` (`CRITICAL_SECTION` mutex type),
   `BlizzardCore/SFile2-Core.h`, `Storm/H/stpl.h`, `Storm/Source/SLock.h`, `Base/CDataStore.h`, `Gx/CGxDevice/*.h`,
   `fmod.h`. Fixing the lock types alone unblocks most modules.
2. "declaration shadows template parameter" (~100). MSVC accepts this, Clang doesn't. Rename the shadowing locals.
3. x86-only intrinsic headers (`xmmintrin.h`, ...) on arm64: bring in sse2neon.
4. Odds and ends: `process.h`, `mbstring.h`, `winerror.h`, `reinterpret_cast` dropping `const`, NEON vector types
   passed to builtins.

### Phase 1: Portability shim + compile the portable modules

- Add `Engine/Source/Base/Platform.h` (or similar) with:
  - Fixed-width typedefs for the Windows names that leak into shared code (`DWORD` -> `uint32_t`,
    `BOOL`, `HANDLE` -> `void*`, ...). Shared code should use these, not `<windows.h>`.
  - Calling-convention macros that expand to nothing on non-Windows (`__stdcall`, `__cdecl`, `__fastcall`,
    `__thiscall`, `WINAPI`, `CALLBACK`).
  - CRT name mapping: `_stricmp` -> `strcasecmp`, `_snprintf`, `_vsnprintf`, `_alloca` (`<malloc.h>` ->
    `<alloca.h>`), `_itoa`, `__forceinline`, `__declspec(align)` -> `alignas`, `__declspec(noinline)`.
  - Intrinsic replacements: `_Interlocked*` -> `__atomic_*`, `_BitScan*` -> `__builtin_ctz/clz`,
    `_rotl/_rotr`, `_byteswap_*` -> `__builtin_bswap*`, `__rdtsc` -> `mach_absolute_time`, `__cpuid` -> stub
    that reports a fixed feature set.
- SSE: add [sse2neon](https://github.com/DLTcollab/sse2neon) (single header) to ThirdParty and include it
  instead of `<xmmintrin.h>`/`<emmintrin.h>` on arm64. Fastest way to get correct results. Hand-written NEON
  only if profiling asks for it.
- **LLP64 -> LP64 audit (biggest correctness risk).** Every `long`/`unsigned long` in shared code is 32-bit
  in the original. Replace with `int32_t`/`uint32_t` wherever it describes on-disk data, network packets,
  struct layouts, hashes or bit math. Add `static_assert(sizeof(...))` for packet and file-format structs
  (none exist today) so layout drift is caught at compile time.
- `wchar_t` is 4 bytes on macOS and 2 on Windows. Any UTF-16 handling (IME, clipboard, Unicode helpers)
  must use `char16_t`/`uint16_t` explicitly.
- Strip `#include <windows.h>` from shared (non-`Win*/`) files: use the shim, or move the code into the
  platform folder.

**Done when:** every module except the platform layers compiles with Clang on arm64. (Linking isn't
expected yet.)

**Status: done.** All 52 libraries build on arm64. SSE code uses
[sse2neon](https://github.com/DLTcollab/sse2neon) v1.9.1, vendored in `ThirdParty/sse2neon`: on arm64,
`Engine/Source/Base/Compat/MacArm64` makes `<xmmintrin.h>` / `<emmintrin.h>` resolve to it, so the five SSE sources
are unchanged. What changed:

- Shared files include `<windows.h>` (and `process.h`, `winerror.h`, `mbstring.h`) only under `_WIN32`.
- `Platform.h` gained `CRITICAL_SECTION` (= recursive `pthread_mutex_t` in the Mac lock code), `RECT`,
  STRICT-style `HWND`/`HINSTANCE`, opaque `OVERLAPPED`/`SYSTEMTIME`/`LARGE_INTEGER`, `fopen_s`, `_atoi64` and
  friends, `_ismbcspace`, `_cexit`, and `GetACP`.
- Clang-only fixes that don't change Windows codegen: `CDataStoreCache::DetachBuffer` parameter name,
  const-correct adapter casts in `BSNEncodeInterface.cpp`, `<cfloat>` in `World.cpp`.
- `Storm.cpp`: `StormGetInstance` / `StormExitProcess` have small in-place Mac branches.
- FMOD Ex: CD audio is off on Mac, and the Windows OpenAL loader only builds on Windows.
- `UILCD/LCD.cpp` (Logitech LCD) is excluded on Mac. Phase 2 needs `CGLCD` stubs.

Not checked yet: `long` (LP64) audit and `wchar_t` usage. These compile cleanly but can be wrong at runtime.

**Link status (start of Phase 2):** `Wow` / `WowClient` fail to link with 387 undefined symbols, the functions
the excluded `Win64/` folders provided. By area: Os ~105, BlizzardCore ~95 (File, Lock, Streaming, Thread, Time,
Log), Storm ~68, FMOD platform layer ~39, Net (`WowConnection*`) ~23, LCD stubs 17, Warden/BotCheck 9, Gx 8,
Battlenet/ComSat 6, movie decoder 3, and `main`.

### Phase 2: Mac platform layer (the bulk of the work)

Write a `Mac/` implementation next to each `Win64/` folder, using POSIX + Cocoa (Objective-C++ `.mm`
files where AppKit is needed). Order is by dependency, lowest first:

| Module | Mac implementation |
|---|---|
| `BlizzardCore/System/{Thread,Lock,Time,Process,Debug,Log,Version,Unicode,Hardware}` | pthreads, `os_unfair_lock`/pthread mutex, condition variables, `mach_absolute_time`, `sysctl`, `os_log` |
| `BlizzardCore/System/File`, `Storm/Source/Win64` (SFile, SThread, SErr, SMem, MachineState) | POSIX file I/O, `mmap`; case-insensitive path lookup (MPQ-relative paths and `WTF/`/`Interface/` assume a case-insensitive FS; APFS default is fine, but don't count on it); `\` -> `/` |
| `Os` (Time, Path, OsFile, OsTcp, Tls, OsMemory, OsSecureRandom, OsLibrary, OsCall, Clipboard, URLDownload, VersionHash) | BSD sockets with `kqueue` or `poll` replacing the Winsock async model; `SecRandomCopyBytes`; `dlopen`; `NSPasteboard`; `NSURLSession` |
| `Os/Gui`, `Os/Input`, `Os/IME`, `Os/Joystick` | `NSApplication` run loop, `NSWindow` + `NSView`, keyboard/mouse events -> existing `OsQueue` events, Mac virtual keycodes -> client key table, cursor capture for mouselook (`CGAssociateMouseAndMouseCursorPosition`), `NSTextInputClient` for IME, `GCController` or stub for joystick |
| `Gx/CGxDevice/Mac` + OpenGL context | `NSOpenGLContext` (legacy profile), display mode enumeration via `CGDisplay*`, gamma via `CGSetDisplayTransferByTable`, Retina backing scale |
| `BlizzardCore/Packages/Streaming`, `iounit` | POSIX threads + file I/O |
| `WowServices`, `Battlenet`, `Game/GameClient`, `Glue`, `ComSat` | Port or stub; BotCheck/Warden-style code stubbed |
| `WoW/Source/Win64/WinMain.cpp` | `Mac/main.mm`: build the `.app` bundle, set the working dir to the folder next to the bundle so `Data/` is found |
| `Tools/WowClient/crash_log.cpp` | `signal`/`sigaction` + `backtrace()` crash logger |

**Status: lowest layer done.** Mac implementations exist for BlizzardCore Lock, Process, Thread, Time and the
native File backend (`Filestack.cpp` on POSIX), and for Storm's SCritSect, SLock (sync objects, CSRWLock) and
SThread. Notes:

- `BlizzardCore/Source/System/Lock/Mac/Waitable.*` provides Win32-style waitable objects (events, mutexes,
  semaphores, thread handles, wait-any / wait-all with timeouts). BlizzardCore and Storm handles are these objects.
- `Win64/File.cpp` (Throttle) and `Win64/Filestack_Streaming.cpp` are portable, so they build on Mac as they are
  (include guard only).
- Mac files carry no `// OFFSET:` tags: they aren't decompiled functions.
- A smoke test (threads, events, TLS destructors, waits, semaphores, rwlocks, time round trips, file I/O with
  backslash paths) passes.
- Undefined symbols at link: 387 -> 286.

**Status: BlizzardCore and Storm done.** The rest of both modules now has Mac versions too: Log, Debug, Hardware,
Version, Unicode, URL (system libcurl), iounit, plus Storm SErr, SLog, SReg (CFPreferences, domain
`org.wowdecomp.client`), SCmd, SServer, IOFileUnit and a small MachineState. Notes:

- Streaming (`Manifests`, `Storage`, `Streaming`, `Transport`; ~3,300 decompiled lines) builds from `Win64/` as it
  is. Its only Windows dependencies are events and waits, which `Lock/Mac/Win32Sync.h` provides on MacWaitable.
  The `BLIZZARDCOMPONENT` resource reader is wrapped in `#if defined(_WIN32)`, with a Mac version that returns no
  data. `Storm/Source/Win64/SMsg.cpp` also builds as it is.
- `CRITICAL_SECTION` in `Platform.h` is now `{ pthread_mutex_t mutex; void* DebugInfo; }`, because decompiled code
  clears `DebugInfo` after deleting a lock.
- SErr writes `Errors/<date> Error.txt` / `Crash.txt` next to the game (header, registers, demangled stack),
  shows a CFUserNotification dialog, and catches crashes with `sigaction` on an alternate stack.
- BlizzardCore + Storm now link stand-alone on macOS. A second smoke test passes: registry round trip, logs, error
  and crash logs (a real fault in a child process), Storm IOUnits (buffered and unbuffered), and HTTP GET / 404 / HEAD
  through libcurl against a local server.
- Undefined symbols at link: 286 -> 225. What's left is Os, the FMOD platform layer, Net, LCD stubs,
  Warden/BotCheck, Gx, Battlenet/ComSat, the movie decoder and `main`.

**Status: Os done.** Every Os function the client uses has a Mac version (`Engine/Source/Os/Mac/`). The window and
input layer is AppKit (`Input.mm`, `Gui.mm`, `OsClipboard.mm`). `OsMacWindow.h` is the Mac-only entry point the Gx
device will use to create the game window and get its view. Notes:

- The game view turns AppKit events into the same OSINPUT events `OsWindowProc` makes from window messages:
  keys from hardware key codes plus the keyboard layout (like `MapVirtualKey`), text through `NSTextInputClient`,
  left/right modifiers, mouse, wheel (120 per notch), relative mouse via
  `CGAssociateMouseAndMouseCursorPosition`, focus, live resize and close. `OsInputGet` drains AppKit events
  without blocking, as Windows drains `PeekMessage`.
- Coordinates and sizes are points (top-left origin). On Retina screens the GL drawable can be twice that; Phase 3
  picks the backing scale.
- `Win64/OsFile.cpp` builds on Mac as is (only Win32 constants, from `Mac/Win32Files.h`).
- `OsGetOsId` / `OsGetProcessorId` still report `'Win'` / `'x86'` in the login challenge, because the client speaks
  the Windows protocol. That also means servers send Windows (x86) Warden modules, which can't run on arm64.
  Playing needs a server with Warden off for this client, or a Warden replacement.
- Not done yet: CJK composition/candidate UI (dead keys work), gamepads (GameController), and a decision on the
  Command key (it isn't a game key; Cmd+Q asks the game to close).
- `Tools/OsWindowTest` (Mac only) opens the game window and prints input events; `--self-test` checks the
  translation automatically and passes.
- Undefined symbols at link: 225 -> 114 (FMOD 39, Net 19, LCD 17, Warden/BotCheck 12, Battlenet/ComSat 10,
  Gx 8, movie 4, `main` and a few odd ones).

SEH (`__try/__except`) in these layers becomes `sigaction`/`setjmp` handlers or plain error checks.
D3D's SEH stays in the Windows-only backend.

**Done when:** `WowClient` links on Mac and reaches the glue (login) screen with no audio.

### Phase 3: Rendering on OpenGL

- Force `GxApi_OpenGl` on Mac. D3D backends aren't compiled, so `gxApi` falls back to OpenGL.
- Split `CGxDeviceOpenGl` / `GlExt` WGL code (window, pixel format, swap, `wglGetProcAddress`) into a
  small platform interface: `Win64` keeps WGL, `Mac` uses NSOpenGL + direct symbols.
- Shader path: Apple GL has no `NV_register_combiners`, `NV_texture_shader` or `NV_vertex_program`. Make
  sure capability detection picks `arbvp1`/`arbfp1`, and confirm those shader trees load from the MPQs.
- Things known to bite on Apple's GL: `GL_TEXTURE_RECTANGLE` vs NPOT, `GL_BGRA` upload formats, DXT
  compression (supported), FBO-based render targets, and vsync via `NSOpenGLCPSwapInterval`.
- Watch for performance problems in immediate-mode / client-array paths. The Metal-backed GL layer punishes
  them more than old drivers did.

**Done when:** login screen, character select, and in-world rendering are visually correct.

**Status: device up.** The decompiled OpenGL device (`CGxDeviceOpenGl`, `GlExt`, VBO broker) builds on macOS
and renders into the Os game window. On an M4 it reports `2.1 Metal - 90.5` and picks `arbvp1` / `arbfp1`, the shader
path the original Mac client used. Notes:

- GlExt keeps its extension pointer variables. On Mac, `CGxDeviceOpenGL/Mac/GlMac.h` renames them (Apple declares
  those names as functions) and `wglGetProcAddress` uses `dlsym` on OpenGL.framework, so `BindGlExtensions` runs
  unchanged. NV register combiners and NV vertex programs stay null.
- 26 Win32/WGL functions in `CGxDeviceOpenGl.cpp` are wrapped in `#if defined(_WIN32)` (bodies untouched). Their
  Mac versions are in `Mac/CGxDeviceOpenGlMac.inl`, included at the end of the file so they share its statics.
  `wglMakeCurrent` / `wglSwapLayerBuffers` map onto NSOpenGLContext, so render-target copy and `ScenePresent` stay
  shared.
- The context is NSOpenGLContext on the legacy 2.1 profile, at 1x (points, matching Os coordinates). The window
  listener in `OsMacWindow.h` delivers resize, minimize and focus to `DeviceWM`, as `WindowProcGl` does on
  Windows. There are no pbuffers, so render targets use the FBO or copy paths. Gamma goes through
  `CGSetDisplayTransferByTable` in fullscreen only. Fullscreen is a borderless window at desktop resolution (no
  mode switching).
- `Gx/CGxDevice/Mac`: desktop and monitor modes from CGDisplay, adapter class 3, and D3D factories that return
  nullptr. `GxDefaultApi` / `GxApiSupported` are OpenGL-only on Mac.
- `Tools/GxDrawTest` (Mac only) creates the device with `CreateGxDevice(GxApi_OpenGl, OsWindowProc, ...)` and
  draws a triangle. `--self-test` reads the back buffer and checks clear color, triangle, shader targets and resize,
  and passes.
- Not exercised yet: game shaders and textures (they need the MPQs, which need the full client), fullscreen,
  Retina 2x rendering, multisampling.
- Undefined symbols at link: 114 -> 106 (FMOD, Net, LCD stubs, Warden/BotCheck, Battlenet/ComSat, movie, `main`). Phase 4
  took them to 0.

Later option, out of scope for the first playable build: a Metal backend, or D3D9 through a translation
layer. Apple may drop OpenGL at some point.

### Phase 4: Audio, movies, polish

- FMOD Ex: add a `mac/` output platform layer (CoreAudio `AudioUnit` output, or reuse the in-tree
  `fmod_channel_openal` path against macOS's (deprecated) OpenAL.framework). Exclude the DirectSound,
  WinMM, ASIO, ASF/WMSDK and Winamp code. Swap the `*_asm.s` x86 DSP paths for their C fallbacks.
- Movies: `libxvidcore.lib` is a Windows prebuilt. Build xvidcore from source for arm64 (generic C,
  no asm), or skip intro and cinematic movies at first.
- Stub or remove nvapi, Logitech LCD, SteelSeries mouse and DivX on Mac.
- `.app` bundle, icon (`waystone.ico` -> `.icns`), `Info.plist`, Retina/HiDPI handling, ad-hoc
  codesign so Gatekeeper lets it launch.
- Update `README.md` with Mac requirements and build steps.

**Done when:** full session (login -> world -> zone changes, sound, music) works.

**Status: boots into the world with game data.** `Wow` and `WowClient` link with no undefined symbols
(106 -> 0). Without a `Data` folder the client gets through Storm, command line, CVars and locale setup, then stops at
"Missing or corrupted data" from `OpenArchives`, as the original does. Notes:

- Net: `WowServices/Mac/WowConnectionNet.cpp` runs the shared `WowConnection` state machine on a `poll()` loop with a
  wake pipe in place of WSAEventSelect. The interest set and service flags are the Windows ones. The portable
  `Win64/WowConnection.cpp`, `Win64/BattlenetLogin.cpp` and ComSat's address code build unchanged against
  `Os/Mac/Win32Sockets.h` (Winsock names on BSD sockets). `WowConnectionNet::WORKER` is packed on Windows only,
  because a packed pthread mutex is misaligned on arm64. ComSat's UDP socket is `ComSat/Mac/ComSatNetworkSocketPosix.cpp`.
- `Tools/NetTest` drives `WowConnection` against a loopback server and passes. It covers connecting by address and
  by name, 2- and 3-byte message frames, stream reads, a 4 MB queued send, disconnects from either side, refused
  connections reported through `WCCantConnect`, and no CPU use while idle.
- FMOD Ex: `fmodex/mac/src` has a CoreAudio output (`OutputCoreAudio`, a polled output feeding the default output
  AudioUnit from a ring buffer), plus the OS layer on pthreads, dispatch semaphores and stdio, and a BSD socket net
  layer. The resampler and distortion DSPs use their C paths. `Tools/SoundTest` sets FMOD up the way SoundEngine
  does, checks that CoreAudio is picked (48 kHz stereo PCM16 on an M4), and plays a sample and a file stream in real
  time. It passes; `--audible` plays them out loud.
- Stubs: `CGLCD` (no Logitech LCD), DivX (movies fail to open; `StartMovie` returns 0 and the glue goes on),
  `HidManager` with no devices, Battlenet library loading through `dlopen`, BotCheck without SEH, and the patch
  downloader launched through `Blizzard::Process`. `WardenClient.cpp` builds as is. Warden's modules are x86 and
  can't run, so the client needs a server with Warden off. OS and CPU stay `Win` / `x86` in the login packets.
- `WoW/Source/Mac/main.mm`: ignores SIGPIPE, changes to the game folder, and calls `CommonMain`.
- `Wow` is `bin/Mac/Wow.app`: an Info.plist, an icon built from `waystone.ico` with `sips` / `iconutil`, and an ad-hoc
  signature over the bundle. `WowClient` stays a plain executable for terminal use. In a bundle the game folder is
  the one holding `Wow.app` (`GetModuleFileNameUTF8` / `GetProcessDirectory` stop at `.app`), so `Data/` and `WTF/`
  sit next to it.
- Linkage gotcha: Clang with `-fms-extensions` keeps external linkage for a `static` declared after an `extern` of the
  same name, where MSVC makes it internal. MinimapFrame's `s_mapId` collided with Map's, so it was renamed
  `s_minimapMapId`.
- With a 3.3.5a `Data` folder (custom patch MPQs included) the client opens every archive, shows the login screen,
  and logs in through realm list and character select into the world on a live server. Fixes on the way:
  - Monitor modes are window sizes in points that fit the desktop. The display's pixel mode list has no 1024x768, so
    the default `gxResolution` was rejected and left unset.
  - Threads get at least 1 MB of stack, the reservation Windows gives every thread whatever size it asks for.
    FMOD's 32 KB loader thread overflowed in the MPQ read callback.
  - Texture render targets: the GL device draws into a pbuffer through its own context and copies the result. With
    no pbuffer, `wglMakeCurrent` made no context current and all GL calls until the target was unset were lost while
    the state cache recorded them, so models exploded or vanished. The pbuffer is now a framebuffer object.
  - `IXformSetProjection` tested the sign of the whole master-enable mask instead of `NormalProjection` (bit 7), so
    every perspective frame had its clip planes 5x too far out. This was a decompilation error in shared code.
  - libjpeg's `INT32` (`long`, 64-bit here) overflowed the 16 KB allocation chunk, which crashed screenshots.
    `fopen` turns backslashes into slashes.
- The login scene renders correctly on the shader path. Checked from the game's own screenshots, with
  `WOWDECOMP_SCREENSHOTS="15,25"` pressing Print Screen at those seconds; `WOWDECOMP_GLDEBUG=1` reports rejected
  ARB programs, GL errors, bad draw ranges and context-less buffer calls.
- In the world (checked from the user's screenshots in Goldshire): terrain, doodads, characters, sky and clouds render
  correctly at about 120 FPS on an M4, and low and medium shadows work.
  - `rand`/`srand` are MSVC's (`RAND_MAX 0x7FFF`) through `Platform.h`. macOS's 31-bit `rand` broke the procedural
    cloud texture (`rand() / 32767` noise), M2 animation repeat counts and `(rand() << 16) | rand()` seeds.
  - Shadows: `ValidateShadowMode` allowed low and medium only on D3D9 and the Mac client's GLL renderer. On macOS the
    OpenGL device counts as GLL; the pbuffer is at least 2048 square and reports `D24X8` targets, and depth textures
    copied out of it get depth comparison for `SHADOW2D` lookups. High and above stay D3D only, as in the original
    Mac client.
  - The system cursor is hidden over the game view (the GL device draws the game's cursor itself).
- Open: one earlier in-world crash that left no log, Warden on servers that enforce it, movies (DivX stub), and
  Retina 2x rendering.

## Rules of the road

- Never change Windows behaviour or `// OFFSET:` function bodies to make Mac happy. Platform-specific code
  goes in `Mac/` folders or behind `WOWDECOMP_PLATFORM_*`. Type fixes (`long` -> `int32_t`) are fine
  because they keep Windows layouts identical.
- Keep both Windows configs building. Add a CI matrix (GitHub Actions `windows-latest` x64 +
  `macos-latest` arm64) early, so regressions show up per PR.
- Land work in small PRs per phase or module. A single mega-branch against an actively merged upstream
  will rot fast. Merge `main` into `macos-arm64` often.

## Risks

1. **LP64 layout bugs.** Silent breakage in packets, DBC/ADT/M2 parsing and saved variables. Mitigate
   with `static_assert`s and `-Wshorten-64-to-32`.
2. **OpenGL path maturity.** The x64 Windows build almost certainly runs D3D9 day to day, so the GL
   backend may have unexercised porting bugs. Test it on Windows (`SET gxApi "opengl"` in `Config.wtf`)
   first, to separate GL backend bugs from Mac bugs.
3. **Apple deprecating OpenGL.** It works today on macOS 26. Long-term insurance is a Metal backend.
4. **Effort.** ~40k lines of platform layer to reimplement, plus an FMOD output layer. Phases 0-1 are
   mechanical. Phase 2 is the long pole.

## Suggested first concrete steps

1. `brew install cmake ninja`.
2. Phase 0 CMake changes + `Platform.h` shim, then iterate on `cmake --build` until the portable modules
   compile. That turns the rest of the plan into a concrete error list.
3. In parallel on Windows: run the x64 client with `gxApi opengl` to check the GL backend.
