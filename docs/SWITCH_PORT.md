# Nintendo Switch port

Branch: `switch-aarch64`, built on top of `macos-arm64`. Goal: run the client on a Switch (Erista or Mariana, any
firmware that runs current homebrew) as a homebrew NRO started from the Homebrew Menu, playable with a controller.

## How it's built

The Switch is AArch64 like Apple Silicon, and the macOS port already turned the Windows platform layers into POSIX ones.
So the Switch port reuses most of `Mac/` and adds a `Switch/` folder only where macOS-specific APIs were used:

| Area | Switch implementation |
|---|---|
| Toolchain | devkitPro's devkitA64 + libnx in Docker (`Tools/Switch/Dockerfile`). Sources compile with **LLVM Clang 19** for `aarch64-none-elf` against devkitA64's newlib / libstdc++ and libnx headers, because the decompiled code needs Clang's `-fms-extensions` / `-fdeclspec`, as on macOS. devkitA64's GCC links with libnx's `switch.specs`, and `elf2nro` packages the result. Thread-locals use emulated TLS (libnx's thread pointer is behind `__aarch64_read_tp`, which Clang can't call). |
| CMake | `cmake/SwitchToolchain.cmake`. `WOWDECOMP_PLATFORM_SWITCH` and `WOWDECOMP_PLATFORM_POSIX` (also set on macOS). `wowdecomp_platform_sources` keeps `Mac/` folders on both POSIX platforms; a file in a `Switch/` folder replaces the `Mac/` file of the same name next to it. The game libraries link as one group (GNU ld needs it; ld64 doesn't). |
| Shared `Mac/` code | Small `#if defined(__SWITCH__)` / `__APPLE__` branches: clocks, thread names and priorities, file attributes, crash contexts, the executable path. `Base/Compat/Switch` supplies `<crt_externs.h>` and a `<dlfcn.h>` whose loads always fail. `Os/Switch/Posix.cpp` adds `pread`, `pwrite`, `sysconf` and friends that newlib lacks. |
| Rendering | The decompiled OpenGL device, as on macOS (`CGxDeviceOpenGlMac.inl` is shared). Mesa's nouveau driver for the Tegra X1 through EGL, with a desktop GL **compatibility** context, which keeps the fixed-function pipeline and the ARB vertex / fragment programs (`arbvp1` / `arbfp1`) the Mac client used. Mesa exports no desktop GL symbols, so CMake generates a trampoline for every function in Mesa's `<GL/gl.h>` (`GlSwitchEntryPoints.cpp`), filled from `eglGetProcAddress`. Render targets use the same framebuffer-object pbuffer as macOS. The game draws its own (software) cursor. |
| Screen | 1280x720, or 1920x1080 when docked and chosen in the video options; the system scales it to the display. |
| Audio | FMOD Ex with the macOS POSIX OS / net layers and a new output on libnx's audio renderer (`fmodex/switch/src`): one 48 kHz stereo PCM16 voice whose wave buffers a feeder thread fills from FMOD's ring; surround mixes fold to stereo. (The simpler `audout` service refused to start on hardware.) |
| Network | The macOS `poll()` loop for `WowConnection`. The Switch has no pipes and its `poll` only takes sockets, so the wake-up "pipe" is a loopback UDP socket connected to itself. Socket buffers are raised at start (`WoW/Source/Switch/NxMain.cpp`). |
| Input | `Os/Switch/Input.cpp` (game side) and `NxInput.cpp` (libnx side): controllers, the touch screen, USB keyboard and mouse, the software keyboard. See [Controls](#controls). |
| Storage | The registry (`SReg`) is `Registry.txt` in the game folder. The clipboard is the game's own. |
| Errors and crashes | Storm's error dialogs use the system error applet. CPU faults reach libnx's exception handler, which writes the usual `Errors/<date> Crash.txt` with registers and a frame-pointer stack walk; addresses are also given as `Wow.elf+0x...` for `aarch64-none-elf-addr2line -Cfe bin/Switch/Wow.elf <offset>`. |

Not ported: dynamic libraries (none are needed), child processes (the patch downloader), CJK input methods, display
gamma, Warden (as on macOS and Windows x64: the server needs Warden off).

## Building

Docker (Docker Desktop on a Mac) is all you need; the first run builds the image (devkitA64 plus Clang, about 1 GB).

```bash
Tools/Switch/build.sh
```

Switch builds skip the MPQ and interface signature checks (`WOWDECOMP_DISABLE_SIGNATURE_CHECKS`), so a Data folder
with UI mods or extra patch archives loads; `WOWDECOMP_SIGNATURE_CHECKS=1 Tools/Switch/build.sh` keeps them.

This writes `bin/Switch/WoW.nro` (and `bin/Switch/Wow.elf`, the same program with symbols, for reading crash logs).
Extra arguments go to `cmake --build`, for example `Tools/Switch/build.sh --target WowNro -j8`.

## Installing

On the SD card:

```
sdmc:/switch/wow/WoW.nro
sdmc:/switch/wow/Data/          <- the Data folder of a 3.3.5a (12340) client, about 17 GB
```

The MPQs are the same as on Windows and macOS. None of the 3.3.5a archives is over 4 GB, so a FAT32 card works. Extra patch
archives that change interface files (a `patch-Z.MPQ` from a UI mod, say) load, because Switch builds skip the
signature checks.
Point `Data/<locale>/realmlist.wtf` at your server before copying. Settings (`WTF/`), logs, screenshots, AddOns and
`Registry.txt` are written next to `WoW.nro`, as next to `Wow.exe`.

## Running

**Start the Homebrew Menu over a game** (hold R while starting any game from the HOME menu), then pick World of
Warcraft. Started from the album, homebrew runs as an applet with under 0.5 GB of memory, which isn't enough; the game
says so and quits. Over a game it gets about 3.2 GB.

During development, `nxlink` sends the NRO over Wi-Fi and shows its output (stdout / stderr) on the computer:

```bash
nxlink -s bin/Switch/WoW.nro
```

(the Homebrew Menu must be open with its nxlink server running: press Y in it). The NRO's folder is then `sdmc:/switch/`,
so put `Data` there or pass the NRO's final path with `-p switch/wow/WoW.nro`.

Without nxlink, stdout and stderr go to `Logs/Console.txt` in the game folder. Development settings go in `Debug.txt`
there, one `name=value` per line (the Switch has no environment variables): `screenshots=15,25` presses Print Screen
15 and 25 seconds after start, like `WOWDECOMP_SCREENSHOTS` on macOS.

### NSP forwarder

The NRO is what homebrew runs as today. A home-screen icon that starts it is an NSP *forwarder*, built on your own
computer with your console's keys (for example with nro2nsp or a forwarder generator, pointing at
`sdmc:/switch/wow/WoW.nro`), and installed with sigpatches. Forwarders start as a game, so they get the full memory too.
The build doesn't make one: that needs keys this repository can't include.

## Controls

The game sees a keyboard and mouse; everything is mapped to those, so the in-game key bindings apply.

- **USB keyboard and mouse** (docked) work as on a computer.
- **Touch screen**: tap to left-click, drag to left-drag, touch and hold for half a second to right-click.
- **Controller** (handheld, Joy-Cons, Pro Controller), in two modes. A short press of **Minus** or clicking the
  **right stick** switches between them; **holding Minus** opens and closes the controls list. The login and character screens start in cursor mode; entering the world switches to play
  mode, and opening a menu or window (game menu, bags, character, spellbook, loot, popups, ...) switches to cursor mode
  until it closes. Going back to play mode with a window open keeps play mode until the windows close.

| Play mode | | Cursor mode | |
|---|---|---|---|
| Left stick | move (W / S forward and back, Q / E strafe) | Left stick | move |
| Right stick | look (turns the camera and character, as right-drag) | Right stick | move the cursor |
| B | jump (Space) | A or ZR | left click (hold to drag) |
| A, X, Y | action keys 1, 2, 3 | B or ZL | right click |
| D-pad right, up, left, down | action keys 4, 5, 6, 7 | D-pad up / down | mouse wheel |
| L | action key 8 | Y | Enter (opens chat) |
| R | Tab (next enemy) | X | the controls list |
| ZL, ZR (hold) | Shift, Ctrl, for more bindings | | |

Play mode hides the cursor; cursor mode shows it where you last left it. Touching the screen or moving a USB mouse
shows it in either mode. In both modes **L3** (left stick click) is autorun (Num Lock) and **Plus** is Escape (game
menu, close windows, clear target). Shift and Ctrl with the face buttons and D-pad give 24 more bindings; set them in
Key Bindings.

**On screen** (the built-in SwitchControls add-on, packed in `WoW.nro` and installed into `Interface/AddOns` at every
start, so it always matches the build):

- A hint bar at the top of the screen names the mode (play, cursor, typing) and shows what the buttons do in it.
  Tapping it opens the controls list.
- Action buttons are labeled with the controller buttons that press them (A, ZL+X, D-pad, ...) instead of key names.
  Keys no controller button sends keep their usual labels.
- A controls list opens the first time you enter the world. Hold **Minus**, tap the hint bar, or press **X** in cursor
  mode to open it again; the same, **Plus**, or its Close button closes it. (`/switch hints` turns the hint bar off and
  on, for a USB keyboard.)

The add-on's sources are `Tools/Switch/romfs/AddOns/SwitchControls`; `Tools/Switch/make_glyphs.py` draws its button
icons. Its button layout must match `Engine/Source/Os/Switch/Input.cpp`.

**Typing.** When a text box gets the focus (chat, login, mail, ...), the Switch's software keyboard opens. OK puts the
text in the box; the box's existing text is the keyboard's starting text. While a text box has the focus, the face
buttons edit instead of playing: **A** Enter (send), **B** Escape, **X** Tab (next field), **Y** opens the keyboard
again, **D-pad** arrow keys. To chat: cursor mode (Minus), **Y** (Enter) opens the chat box and the keyboard, type, OK,
then **A** sends.

Logging in: the keyboard opens for the account name; OK, then **X** (Tab) moves to the password, whose keyboard opens
by itself; OK, then **A** (Enter) logs in.

## Status

- Builds and links (`WoW.nro`, no undefined symbols). The macOS build and its self-tests are unaffected.
- **Not yet run on hardware.** Everything up to the first frame is untested: EGL context creation on Mesa, the GL entry
  points, the ARB programs on nouveau, audout, sockets, the controller mapping. Expect a round of fixes from the first
  crash logs (`Errors/` next to `WoW.nro`) and nxlink output.
- Performance on the Tegra X1 is unknown. Start at 1280x720 with low settings; the CPU (three 1 GHz A57 cores) is likely
  the limit in cities.

## Risks and next steps

1. **Mesa's compatibility profile on nouveau**: ARB assembly programs go through Mesa's generic translator, so they
   should work, but the immediate-mode and client-array paths may be slow. A GLES or deko3d backend is the long-term
   answer if it is.
2. **Memory**: about 3.2 GB over a game. WoW 3.3.5a typically uses 1 to 1.5 GB, plus the GL driver's copies of textures.
   `OsAllocNear` (M2 data) needs allocations within 1.5 GB of their model; with a 3 GB heap that can fail late in a
   session and is reported as a failed load.
3. **pread / pwrite** are seek-read-seek under one lock. If MPQ reads from the loader threads turn out slow, read each
   file through its own descriptor.
4. **Controller mapping** is fixed. A small `WTF/Controller.txt` with per-button keys would let players change it.
5. Docked 1080p, CPU boost during loading (the system's "fast load" boost lowers the GPU clock, so it isn't used yet),
   and suspend / resume (the GL surface and audio after sleep) need checking on hardware.
