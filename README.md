# MAME4Wii

MAME for the Nintendo Wii, now based on **MAME 0.289**.

Earlier releases (up to 1.35.3, tag `v1.35.3`) were a port of MAME 0.135 from
2009. This version is a fresh port of current MAME: the emulation core, drivers
and user interface are upstream MAME 0.289; this repository adds a Wii OSD
(video, audio, input, storage), a Wii build target and the packaging for the
Homebrew Channel.

## Features

- MAME 0.289 core and its full user interface (system selection, settings,
  input mapping, cheats, save states, ...).
- GX video output: MAME's software renderer, scaled to the TV with bilinear
  filtering; 4:3 or 16:9 (follows the console setting by default).
- 48 kHz stereo audio.
- Controllers through [libwiiinput](https://github.com/quatric/libwiiinput):
  Wii Remote (sideways, or upright with a Nunchuk), Classic Controller,
  Wii U Pro Controller, GameCube controllers (console ports and the Wii U
  GameCube adapter), USB gamepads, USB keyboards and mice, and the Wii Remote
  pointer as a light gun.
- On-screen keyboard ([libwiiosk](https://github.com/quatric/libwiiosk)) for
  MAME's text fields (search, file names, ...).
- Update check at start-up ([libwiiupdate](https://github.com/quatric/libwiiupdate)).
- SD card and USB storage.

## Installing

1. Copy the `apps` and `mame4wii` folders from the release zip to the root of
   your SD card (or USB drive).
2. Put your ROM sets (`.zip` or `.7z`, MAME 0.289 sets) in `mame4wii/roms`.
3. Start one of the *MAME4Wii* entries from the Homebrew Channel.

MAME is too large to run every system from one executable on the Wii, so the
drivers are split into groups and each group is its own Homebrew Channel
entry. All of them share the `mame4wii` data folder (ROMs, configuration,
NVRAM, save states, ...).

| Entry | Contents |
| --- | --- |
| MAME4Wii (Classics) | Pac-Man, Galaxian, Galaga, Donkey Kong, Mario Bros., Popeye, Centipede, Asteroids, Missile Command, the Williams games, Phoenix, Zaxxon, Gyruss, Time Pilot, Arkanoid, Bubble Bobble, Bomb Jack and their variants |
| MAME4Wii (Capcom) | 1942, 1943, Commando, Ghosts'n Goblins, CPS-1, CPS-2 |
| MAME4Wii (Neo Geo) | Neo Geo MVS/AES |
| MAME4Wii (Sega, Konami, Irem & Technos) | Sega System 1, System 16A/16B, Double Dragon, Irem M72, TMNT, The Simpsons and related boards |

The data folder is looked for in this order: `sd:/mame4wii`, `usb:/mame4wii`,
`sd:/apps/mame4wii`, `usb:/apps/mame4wii`, then the folder the executable was
started from.

### Memory

The Wii has 88 MB of RAM. MAME itself takes about 30 MB of it, which leaves
roughly 40 MB for the emulated system. Most 1980s and early 1990s systems fit
easily; the largest Neo Geo and CPS-2 sets do not, and MAME reports an
out-of-memory error for them.

## Controls

MAME's own input configuration (*Input Settings* in the menu) can remap
everything; these are the defaults.

| | Wii Remote (sideways) | Wii Remote + Nunchuk | Classic / Pro | GameCube |
| --- | --- | --- | --- | --- |
| Move | D-pad | Stick / D-pad | Left stick / D-pad | Stick / D-pad |
| Buttons 1–4 | 2, 1, A, B | A, B, Z, C | B, A, Y, X | A, B, X, Y |
| Buttons 5–8 | – | 1, 2 | L, R, ZL, ZR | L, R |
| Coin | − | − | − | Z |
| Start | + | + | + | Start |
| MAME menu | HOME | HOME | HOME | Z + Start |
| On-screen keyboard | − and + together | − and + together | − and + together | Z + R |

In MAME's menus, button 1 selects and button 2 goes back. USB keyboards use
MAME's standard keyboard controls (Tab for the menu, 5 for coin, 1 for
start, ...), and Ctrl+K opens the on-screen keyboard.

## Options

All of MAME's options work, in `mame4wii/mame.ini` or per system as usual.
The Wii-specific ones are:

| Option | Default | Meaning |
| --- | --- | --- |
| `safearea` | 1.0 | Fraction of the TV picture to use (0.5–1.0; e.g. 0.92 to stay inside overscan) |
| `widescreen` | auto | `1` for 16:9, `0` for 4:3, `auto` to follow the console setting |
| `renderscale` | 0 | Software render scale relative to the native resolution (0–4, 0 = automatic) |

Files in the data folder that change start-up behaviour:

- `cmdline.txt`: extra command-line arguments, e.g. the short name of a system
  to start straight away (`pacman`), or options such as `-skip_gameinfo`.
- `noupdatecheck`: skip the update check at start-up.
- `debuglog`: write MAME's console output to `debug.log` (useful for bug
  reports).

## Building

Requirements: [devkitPro](https://devkitpro.org/wiki/Getting_Started) with
devkitPPC and libogc (`wii-dev`), `python3`, `git`, `make` and `zip`.

```sh
git clone --recurse-submodules --shallow-submodules https://github.com/quatric/MAME4Wii.git
cd MAME4Wii
./build.sh            # every group; the result is dist/mame4wii-<version>.zip
./build.sh classics   # or just one group
./build.sh --list     # the available groups
```

The first build compiles the whole MAME core and takes a while; further
groups only compile their own drivers.

### How the port is organised

- `mame/`: upstream MAME, pinned to the `mame0289` release (git submodule).
- `patches/`: changes to existing MAME files (the Wii build target, link
  layout and a few portability fixes).
- `overlay/`: new files copied into the MAME tree: the Wii OSD
  (`src/osd/wii`), its build scripts and the boot stub.
- `3rdparty/`: libwiiinput, libwiiupdate and libwiiosk (git submodules).
- `groups/`: the driver groups; each file lists MAME driver sources under
  `src/mame/`. Add a file to make a new group.

`./build.sh --prepare` applies the patches and the overlay to `mame/`. When
working on the port, edit the files inside `mame/` and run
`./build.sh --export` to copy the changes back into `patches/` and
`overlay/`.

### Memory layout

MAME's code is larger than the Wii's 24 MB MEM1, so it is linked to run from
MEM2 (`src/osd/wii/mame4wii.ld`). Writable data stays at the top of MEM1,
because libogc and the hardware DMA engines need it there. The Homebrew
Channel only loads executables into MEM1, so each `boot.dol` is a small boot
stub (`src/osd/wii/bootstub`) that carries the compressed MAME image, unpacks
it into MEM2 and starts it.

## Credits

- The MAMEdev team and all MAME contributors.
- Nebiun, for MAME4Wii up to 1.35.3, and Toad King, for MAME Wii.
- devkitPro and the libogc contributors.

## Licence

MAME is distributed under the GNU General Public Licence, version 2 or later
(individual source files are BSD-3-Clause or GPL-2.0+); see `LICENSE` and
`mame/COPYING`. The Wii OSD files are BSD-3-Clause.
