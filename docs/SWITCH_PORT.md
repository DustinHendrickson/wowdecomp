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
| Audio | FMOD Ex with the macOS POSIX OS / net layers and a new output on libnx's audio renderer (`fmodex/switch/src`). The game runs several FMOD systems at once (its own, one kept open to enumerate devices, voice chat) but the process has one renderer session, so there is one shared driver: each FMOD output gets its own stereo voice, memory pool and wave buffers in it, and one service thread refills them all every renderer frame (5 ms) from FMOD's ring, in 5 ms slices, six queued. It runs above FMOD's mixer on its own core; FMOD's threads run at audio priority (`Os/Switch/NxThreads.cpp`). Surround mixes fold to stereo. |
| Network | The macOS `poll()` loop for `WowConnection`. The Switch has no pipes and its `poll` only takes sockets, so the wake-up "pipe" is a loopback UDP socket connected to itself. Socket buffers are raised at start (`WoW/Source/Switch/NxMain.cpp`). |
| Input | `Os/Switch/Input.cpp` (game side) and `NxInput.cpp` (libnx side): controllers, the touch screen, USB keyboard and mouse, the software keyboard. See [Controls](#controls). |
| Clocks | While the game runs, the CPU is at 1785 MHz and the GPU at 768 MHz docked / 460.8 MHz handheld, the highest clocks Nintendo uses for games (homebrew otherwise gets the 1020 MHz CPU, too slow for the movie decoder and FMOD's software mixing). Memory is left alone; the original clocks come back at exit, and the clocks are applied again after docking, sleep and the HOME menu (`Os/Switch/NxClocks.cpp`). `boost=0` in `Debug.txt` leaves them alone. |
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

Closing the game from the HOME menu asks it to quit (the exit is locked until it has), so it logs out and saves the UI's
saved variables, key bindings and settings as Exit Game does; if quitting takes over 30 seconds the system ends it. (Files written through the Os layer, saved variables among
them, are gathered in memory and written in large pieces: a write per key and value is slow on the SD card, long
enough that quitting could run out of time partway through a file and leave it truncated.)

Without nxlink, stdout and stderr go to `Logs/Console.txt` in the game folder, written by a background thread so the
game never waits on the SD card (a card write can take tens of milliseconds); after a crash, output goes straight to
the file. Every 5 seconds a `Perf:` line gives the frame rate, the slowest frame, FMOD's CPU use and the audio
renderer's refills. Development settings go in `Debug.txt`
there, one `name=value` per line (the Switch has no environment variables): `screenshots=15,25` presses Print Screen
15 and 25 seconds after start, like `WOWDECOMP_SCREENSHOTS` on macOS; `boost=0` keeps the console's default clocks.

### NSP forwarder

The NRO is what homebrew runs as today. A home-screen icon that starts it is an NSP *forwarder*, built on your own
computer with your console's keys (for example with nro2nsp or a forwarder generator, pointing at
`sdmc:/switch/wow/WoW.nro`), and installed with sigpatches. Forwarders start as a game, so they get the full memory too.
The build doesn't make one: that needs keys this repository can't include.

## Controls

The game sees a keyboard and mouse; everything is mapped to those, so the in-game key bindings apply.

- **USB keyboard and mouse** (docked) work as on a computer.
- **Touch screen**: tap to left-click, drag to left-drag, touch and hold for half a second to right-click.
- **Controller** (handheld, Joy-Cons, Pro Controller), in two modes. A short press of **Minus** switches between
  them; **holding Minus** opens and closes the controls list. The login and character screens start in cursor mode; entering the world switches to play
  mode, and opening a menu or window (game menu, bags, character, spellbook, loot, popups, ...) in play mode switches to
  cursor mode. Closing the window that did it (and any opened from it, like a vendor from an NPC's talk window) goes
  back to play mode, even if other windows such as bags stay open; the **Back to play** option turns that off. Going
  back to play mode with windows open keeps play mode until another window opens.

| Play mode | | Cursor mode | |
|---|---|---|---|
| Left stick | move (W / S forward and back, Q / E strafe) | Left stick | move |
| Right stick | look (turns the camera and character, as right-drag) | Right stick | move the cursor |
| B | jump (Space) | A or ZR | left click (hold to drag) |
| A, X, Y | action keys 1, 2, 3 | B or ZL | right click |
| D-pad right, up, left, down | action keys 4, 5, 6, 7 | D-pad up / down | mouse wheel |
| L (tap) | action key 8 | Y | Enter (opens chat) |
| L (hold) + right stick up / down | zoom the camera | | |
| R | Tab (next enemy) | X | the controls list |
| ZR (hold) + R | Ctrl+Tab (nearest friend) | | |
| Right stick click | interact: loot the nearest body, talk to an NPC, use an object | Right stick click | interact |
| ZL, ZR (hold) | Shift, Ctrl, for more bindings | | |

Play mode hides the cursor in the middle of the screen, so mouseover tooltips and highlights follow what the camera
faces; cursor mode shows it where you last left it. Touching the screen or moving a USB mouse
shows it in either mode. In both modes **L3** (left stick click) is autorun (Num Lock) and **Plus** is Escape (game
menu, close windows, clear target). Shift and Ctrl with the face buttons and D-pad give 24 more bindings; set them in
Key Bindings.

Interact (right stick click) picks the nearest lootable or skinnable body, friendly NPC with something to offer, or
usable object (chests, herbs, ore, mailboxes) in reach, preferring what is in front of the character, targets it and
right-clicks it, as clicking it with the mouse would. With nothing in reach it says so in red at the top of the screen.
It is an ordinary key binding ("Interact" under Switch Controls in Key Bindings), so it can be moved to another button.

**Key Bindings** (game menu) works with the controller: pick a row (tap it, or point and press A), then press the
button to bind; hold ZL (Shift), ZR (Ctrl) or both for more. While a row waits, every button sends its play mode key
(A X Y 1 2 3, the D-pad 4 to 7, L 8, R Tab, B Space, L3 Num Lock, R3 F17), the left stick sends nothing, and Plus cancels
rather than binding Escape. The list shows bindings as Switch buttons.

**On screen** (the built-in SwitchControls add-on, packed in `WoW.nro` and installed into `Interface/AddOns` at every
start, so it always matches the build):

- A hint bar at the top of the screen names the mode (play, cursor, typing) and shows what the buttons do in it. It
  dims after a few seconds and comes back when the mode changes. Tapping it opens the controls list.
- Action buttons are labeled with the controller buttons that press them (A, ZL+X, D-pad, ...) instead of key names.
  Keys no controller button sends keep their usual labels.
- The controls list has three options: **Hint bar** on / off, **Invert camera** (the right stick's up and down; the
  game's own mouse invert doesn't reach the stick), and **Back to play** (on: closing a window that brought the cursor
  returns to play mode; off: the cursor stays until Minus). All are saved in `Registry.txt` the moment they change, so they
  survive closing the game from the HOME menu (the add-on's saved variables are written only on a clean logout).
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
- Video options: the first time in the world the effects are set to Blizzard's **Fair** preset, a good fit for the
  Tegra X1 at 720p; later changes are kept. Options that do nothing on the Switch (windowed mode, maximized, window
  lock, triple buffering, hardware cursor, fix input lag, gamma) are greyed out. Resolutions are 1280x720, plus
  1920x1080 while docked, at 60 Hz; vertical sync is on by default. The CPU (three A57 cores at 1785 MHz) is likely the
  limit in cities.

## Risks and next steps

1. **Mesa's compatibility profile on nouveau**: ARB assembly programs go through Mesa's generic translator, so they
   should work, but the immediate-mode and client-array paths may be slow. A GLES or deko3d backend is the long-term
   answer if it is.
2. **Memory**: about 3.2 GB over a game. WoW 3.3.5a typically uses 1 to 1.5 GB, plus the GL driver's copies of textures.
   `OsAllocNear` (M2 data) needs allocations within 1.5 GB of their model; with a 3 GB heap that can fail late in a
   session and is reported as a failed load.
3. **pread / pwrite** are seek-read-seek. Every read, write and lseek takes its descriptor's lock (linker `--wrap`,
   `Os/Switch/Posix.cpp`), so they can't interleave with another thread's reads. If MPQ reads turn out slow, read
   each file through its own descriptor.
4. **Controller mapping** is fixed. A small `WTF/Controller.txt` with per-button keys would let players change it.
5. Docked 1080p, CPU boost during loading (the system's "fast load" boost lowers the GPU clock, so it isn't used yet),
   and suspend / resume (the GL surface and audio after sleep) need checking on hardware.
