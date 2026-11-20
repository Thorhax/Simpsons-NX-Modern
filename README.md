# Simpsons-NX-Modern

Standalone Nintendo Switch arcade port of Konami's **The Simpsons** (1991) powered by modern MAME (0.289) and libnx. Built from the same MAME-NX base as the Killer Instinct port: native hardware-accelerated rendering, multi-core CPU scheduling, clean hbmenu integration and native libnx pad input — here with full **four-player** support.

---

## Features

- **Direct Boot:** `simpsons.nro` boots straight into the game, no frontend menu.
- **Thorhax intro:** a short intro video plays at startup while the game boots behind it, so it ends close to the title screen. The first 3 seconds cannot be skipped; after that any button (A, B, X, Y, Plus, Minus, L, R, ZL, ZR) skips it, and Plus + Minus together skip it at any time.
- **Four Players, any character:** In the arcade game each cabinet position is one character (1 Marge, 2 Homer, 3 Bart, 4 Lisa). The first time a controller presses Minus, a popup lets it pick its character; the coin goes to that character, and Plus starts it. A character another controller has picked shows as taken.
- **Native Switch Pad API:** Low-latency libnx `pad` input (handheld, dual Joy-Con or Pro Controller).
- **Multi-Core Scheduling:** Emulation is pinned to core 0, while audio and worker threads run on cores 1 and 2.
- **Every frame drawn, low power:** auto-frameskip is off. While waiting for the next frame, the emulation thread sleeps and only spins for a short safety margin before the deadline. The margin follows how late the console's sleeps actually wake up (worst of the last ~16 seconds plus 0.2 ms, between 0.25 and 2 ms); on a test console it settles at 0.25 ms, leaving core 0 asleep about 56% of the time at full speed. Pass `-autoframeskip` or `-nosleep` as launch arguments to get the old behaviour.
- **Clean Exit:** `Plus + Minus` saves settings and the EEPROM (high scores) and returns to the HOME menu.
- **Persistent Configuration:** `.cfg` and `nvram` saved to the SD card.

---

## Control Layout

| Physical Button | Arcade Function | Port |
| :--- | :--- | :--- |
| **B** or **Y** | Attack | `BUTTON1` |
| **A** or **X** | Jump | `BUTTON2` |
| **D-Pad / L-Stick** | Movement | `JOYSTICK_*` |
| **Plus (+)** | Start your character | `START1`–`START4` |
| **Minus (-)** | First press: choose a character (popup), then insert a coin for it. Later presses: insert a coin | `COIN1`–`COIN4` |
| **L or R held + Minus** | Choose a different character (popup) | (OSD) |
| **Stick L Click (L3)** | Pause | `UI_PAUSE` |
| **Stick R Click (R3)** | MAME menu | `UI_MENU` |
| **L + R** | Show/hide the performance overlay | (OSD) |
| **Plus + Minus** | Quit to HOME menu | `UI_CANCEL` |

In the character popup: **Left/Right** (D-pad or stick) to choose, **A** to insert the coin, **B** to cancel. Pause, the MAME menu and quitting work from every controller, whichever character it plays.

Controls can be remapped in the MAME menu (R3 → Input (this Machine)); changes are saved to `cfg/simpsons.cfg`. MAME's joystick N is cabinet position N; the port routes each controller to the position of the character it picked. Choices last until the game is closed (or the controller disconnects). The UI controls belong to a "Switch UI" device that merges every controller, which is why the port starts MAME with `-keyboardprovider none`.

---

## Required ROM Set

- **SD Path:** `sdmc:/switch/simpsons-arcade/roms/simpsons.zip`
- **MAME Driver Set:** `simpsons` — *The Simpsons (4 Players World, set 1)*, parent set, MAME 0.289
- Program ROMs `072-g02.16c`, `072-g01.17c`, `072-j13.13c`, `072-j12.15c`; sound `072-e03.6g`; tiles `072-b07.18h`, `072-b06.16h`; sprites `072-b08.3n` … `072-b11.16l`; samples `072-d05.1f`, `072-d04.1d`; EEPROM `simpsons.12c.nv`.

To boot a clone instead (e.g. `simpsons2p`), pass the set name as a launch argument or edit `args.push_back("simpsons")` in `src/osd/sdl/sdlmain.cpp`. All nine `simpsons` sets are compiled in.

---

## Installation

> [!NOTE]
> Launch with **title override** (full RAM mode, hold R while starting a game) rather than from the album applet.

1. Download `Simpsons-NX-Modern-v1.2.0.zip` from the [Releases](https://github.com/Thorhax/Simpsons-NX-Modern/releases) page.
2. Extract it to the root of your SD card and add your ROM, so the layout matches:

```text
sdmc:/switch/simpsons-arcade/
├── simpsons.nro
├── cfg/
├── ini/
└── roms/
    └── simpsons.zip
```

> [!NOTE]
> v1.1.0 used `sdmc:/switch/simpsons/`, which other homebrew also uses. If you are upgrading, move your `roms/`, `cfg/` and `nvram/` folders into `sdmc:/switch/simpsons-arcade/` and delete the old `simpsons.nro`.

> [!IMPORTANT]
> The arcade ROMs are copyrighted by Konami and are not included. Use your own legally obtained copy.

---

## Troubleshooting

- `simpsons.log` in `sdmc:/switch/simpsons-arcade/` records each session; the previous four are kept as `simpsons.1.log` … `simpsons.4.log`.
- To check performance in-game, press **L + R**. The overlay updates once per second and shows `SPD` (emulation speed), `FPS drawn/emulated` (60/60 means no frame was skipped), `SKIP` (frameskip level, 0 unless you enable `-autoframeskip`) and `CPU` (how busy the emulation thread is; under about 90% means there is headroom). It is drawn with a single fill call instead of MAME's own FPS text, which uses one blended texture draw per glyph and is slow on the Switch.
- For per-second performance data, create an empty `sdmc:/switch/simpsons-arcade/perf.txt`; delete it to turn logging off.
- For a CPU profile of the emulation thread, create an empty `sdmc:/switch/simpsons-arcade/profile.txt`. About 1000 samples per second are written to `profile_pc.txt` every 15 seconds, and the addresses can be resolved against the ELF's symbols. This has a small cost, so delete the file afterwards.
- If controls feel wrong after experimenting, delete `cfg/simpsons.cfg` to restore the defaults above.

---

## Building from Source

Requires devkitPro (`devkitA64`, `libnx`, `switch-sdl2`, `switch-mesa`, …) via the `devkitpro-mesa-rust` Docker image.

```bash
./build_switch.sh
```

Produces `simpsons.nro` and an SD-ready `../package/switch/simpsons-arcade/` folder.

The build runs 4 compile jobs by default; set `JOBS=n` to change that. The devkitPro container is limited to 22GB of RAM and no swap. If you copy `build/` from another port to save time, clean it first (delete `build/projects/sdl/mame/gmake-linux/obj/Release`). Optimization comes only from the `ARCH` flags, so objects compiled earlier without `-O3` are silently kept. Likewise, if a build is killed mid-way, delete any 0-byte objects and `obj/Release/emu.h.gch` before resuming. A precompiled header truncated by an OOM kill makes later files compile to empty objects or use tens of GB of memory.

The Thorhax intro lives in `src/osd/thorhax_intro/` (see its README; `make_intro.sh` remakes the video and sound from a source video). It is built from the `osd_sdl.make` entries for `thorhax_intro.o` and `pl_mpeg_impl.o`, without the precompiled header. While it plays, MAME starts and runs the game behind it, unseen and unheard and with untouched controllers; the SDL window is made when it ends (`src/osd/nxintro.h`).

The driver list is limited to `src/mame/konami/simpsons.cpp` (`build/generated/mame/mame.flt`); the GENie project files under `build/projects/sdl/mame/gmake-linux` carry the Switch link setup from the Killer Instinct port with the Simpsons driver/device object lists.

---

## Credits

- **MAME Team:** [mamedev/mame](https://github.com/mamedev/mame)
- **devkitPro & libnx:** [devkitPro](https://devkitpro.org)
- **MAME-NX base:** Thorhax (Killer Instinct NX)
