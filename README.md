# Simpsons-NX

Standalone Nintendo Switch arcade port of Konami's **The Simpsons** (1991) powered by modern MAME (0.289) and libnx. Built from the same MAME-NX base as the Killer Instinct port: native hardware-accelerated rendering, multi-core CPU scheduling, clean hbmenu integration and native libnx pad input — here with full **four-player** support.

---

## Features

- **Direct Boot:** `simpsons.nro` boots straight into the game, no frontend menu.
- **Four Players:** Each connected controller is a separate cabinet position (Player 1–4), each with its own Start and Coin.
- **Native Switch Pad API:** Low-latency libnx `pad` input (handheld, dual Joy-Con or Pro Controller).
- **Multi-Core Scheduling:** Emulation on core 0, audio and worker threads on cores 1 and 2.
- **Clean Exit:** `Plus + Minus` saves settings and the EEPROM (high scores) and returns to the HOME menu.
- **Persistent Configuration:** `.cfg` and `nvram` saved to the SD card.

---

## Control Layout

| Physical Button | Arcade Function | Port |
| :--- | :--- | :--- |
| **B** or **Y** | Attack | `BUTTON1` |
| **A** or **X** | Jump | `BUTTON2` |
| **D-Pad / L-Stick** | Movement | `JOYSTICK_*` |
| **Plus (+)** | Start (Player n on pad n) | `START1`–`START4` |
| **Minus (-)** | Insert Coin (Player n on pad n) | `COIN1`–`COIN4` |
| **Stick L Click (L3)** | Pause | `UI_PAUSE` |
| **Stick R Click (R3)** | MAME menu | `UI_MENU` |
| **Plus + Minus** | Quit to HOME menu | `UI_CANCEL` |

Controls can be remapped in the MAME menu (R3 → Input (this Machine)); changes are saved to `cfg/simpsons.cfg`.

---

## Required ROM Set

- **SD Path:** `sdmc:/switch/simpsons/roms/simpsons.zip`
- **MAME Driver Set:** `simpsons` — *The Simpsons (4 Players World, set 1)*, parent set, MAME 0.289
- Program ROMs `072-g02.16c`, `072-g01.17c`, `072-j13.13c`, `072-j12.15c`; sound `072-e03.6g`; tiles `072-b07.18h`, `072-b06.16h`; sprites `072-b08.3n` … `072-b11.16l`; samples `072-d05.1f`, `072-d04.1d`; EEPROM `simpsons.12c.nv`.

To boot a clone instead (e.g. `simpsons2p`), pass the set name as a launch argument or edit `args.push_back("simpsons")` in `src/osd/sdl/sdlmain.cpp`. All nine `simpsons` sets are compiled in.

---

## Installation

> [!NOTE]
> Launch with **title override** (full RAM mode, hold R while starting a game) rather than from the album applet.

```text
sdmc:/switch/simpsons/
├── simpsons.nro
├── cfg/
├── ini/
└── roms/
    └── simpsons.zip
```

> [!IMPORTANT]
> The arcade ROMs are copyrighted by Konami and are not included. Use your own legally obtained copy.

---

## Troubleshooting

- `simpsons.log` in `sdmc:/switch/simpsons/` records each session; the previous four are kept as `simpsons.1.log` … `simpsons.4.log`.
- For per-second performance data, create an empty `sdmc:/switch/simpsons/perf.txt`; delete it to turn logging off.
- If controls feel wrong after experimenting, delete `cfg/simpsons.cfg` to restore the defaults above.

---

## Building from Source

Requires devkitPro (`devkitA64`, `libnx`, `switch-sdl2`, `switch-mesa`, …) via the `devkitpro-mesa-rust` Docker image.

```bash
./build_switch.sh
```

Produces `simpsons.nro` and an SD-ready `../package/switch/simpsons/` folder.

The driver list is limited to `src/mame/konami/simpsons.cpp` (`build/generated/mame/mame.flt`); the GENie project files under `build/projects/sdl/mame/gmake-linux` carry the Switch link setup from the Killer Instinct port with the Simpsons driver/device object lists.

---

## Credits

- **MAME Team:** [mamedev/mame](https://github.com/mamedev/mame)
- **devkitPro & libnx:** [devkitPro](https://devkitpro.org)
- **MAME-NX base:** Thorhax (Killer Instinct NX)
