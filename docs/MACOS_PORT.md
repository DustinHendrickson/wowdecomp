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
