# Sonic R on the original Xbox

*Back to the [README](../README.md).*

> **Status: complete, tested on real hardware.** Menus, races, split-screen,
> music and sound, saves, online and LAN play, 480p and 720p all run on an
> original Xbox. Behaviour and visuals were compared against the 1998 PC release
> (`SONICR.EXE`) function by function.

> **You supply your own game data.** This build is only the game engine. It
> contains none of Sonic R's assets. You need your own copy of the 1998 PC
> release (its CD has the data and the music).

## Contents

- [Install](#install)
- [Music](#music)
- [Playing](#playing)
- [Online and LAN play](#online-and-lan-play)
- [Video modes](#video-modes)
- [Saves](#saves)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Technical notes](#technical-notes)
- [Credits](#credits)

## Install

1. Copy these folders from the Sonic R PC CD (or an installed copy) into one
   folder: `GENERAL`, `ISLAND`, `CITY`, `RUIN`, `FACTORY`, `EMERALD`, `AI`,
   `BIN` and `SOUND`. Folder and file names should be UPPERCASE.
2. Copy this repository's `DATA` folder contents over them (a few small files
   that belong in `GENERAL`, `BIN` and `SOUND`, including
   `BIN\OPTION\NET01.RAW`, the platform icons of the network screens).
3. Add `default.xbe` and, optionally, the `MUSIC` folder (see [Music](#music)).
4. Copy the folder to the Xbox hard drive, for example `E:\Games\Sonic R\`, and
   launch `default.xbe` from your dashboard.

```
E:\Games\Sonic R\
    default.xbe
    AI\  BIN\  CITY\  EMERALD\  FACTORY\  GENERAL\  ISLAND\  RUIN\  SOUND\
    MUSIC\track2.son ... (optional)
```

For xemu or a disc image, put the same files in `source/xbox/bin/` next to
`default.xbe` and build an image with `extract-xiso -c source/xbox/bin
"Sonic R.iso"`. Do not share images or packages that contain game data.

## Music

The PC game plays its soundtrack straight from the CD as audio tracks, which are
not files. The Xbox build plays `MUSIC\trackN.son` files: headerless 44.1 kHz,
16-bit stereo PCM, the same layout as CD audio, where `N` is the CD track number.
(`.adx` and `.adp` files with the same names also work.) Without music the game
runs normally, just without the soundtrack.

**`SonicR-Music.exe`** (Windows, in the release package; source in
`source/xbox/tools/sonicr_music/`) makes these files:

- **From the original CD:** put the Sonic R CD in the PC and double-click
  `SonicR-Music.exe`. It reads every audio track digitally (no quality loss) and
  writes `MUSIC\track2.son`, `track3.son`, ... next to the program. To choose
  the drive: `SonicR-Music.exe E:`.
- **From WAV files you already ripped:** drag the WAV files, or the folder that
  holds them, onto `SonicR-Music.exe`. Any PCM or float WAV works (8/16/24/32-bit,
  mono or stereo, any sample rate); it is converted to 44.1 kHz 16-bit stereo.
  The track number is taken from the file name (`track02.wav`,
  `Sonic R - 05.wav`, `07 Living in the City.wav`...). MP3/OGG/FLAC rips: convert
  them to WAV with any audio tool first.
- `-o DIR` chooses the output folder.

Copy the resulting `MUSIC` folder next to `default.xbe`. On Linux or macOS the
same source builds with `cc -O2 -o sonicr_music sonicr_music.c` and converts WAV
files (rip the CD with any ripper first).

## Playing

**Pad:** the default layout follows the PC game's pad support: A jump, B or X
accelerate, Y camera, L/R triggers and White/Black drift, Start pause, Back
cancel. **Options > Controls** remaps A, B, X, Y, Start, White, Black, the L/R
triggers and the stick clicks (Back cancels the remap, the D-pad always steers).
Up to four pads are supported for split-screen.

**Options > Graphics:**

- *Resolution:* 480P or 720P (the 4:3 picture stretched to fill a 16:9 screen).
  Applied the next time the game starts; 720p must be enabled in the dashboard.
- *Draw Distance:* Far is the PC maximum; Very Far reaches beyond it.

**Exit to Dashboard** is an entry in the options menu. **In-game reset:** hold
L + R triggers + Back + Black for half a second, anywhere.

**Saves and progress** are written automatically: the last Load/Save slot you
used is loaded at start-up, and progress is saved to it after every race.

The 3D scene uses bilinear texture filtering, like the retail PC version on a 3D
card; menus and the HUD stay sharp.

## Online and LAN play

The cross-platform online mode of this port works on the Xbox: Xbox and
Windows/macOS/Linux players can race each other (use builds of the same
version on every machine).

- **Network:** connect the Ethernet cable; the dashboard's network settings
  (DHCP or static) are used.
- **Sign-in (online list):** the first time, the network screen shows a code
  and a QR code: open the address on a phone or PC and sign in. The token is
  saved with the settings. LAN play needs no sign-in.
- **Buttons on the network screens:** A / Start host (or start the race), right
  trigger join, Y LAN-only host / LAN search, left trigger change mode, D-pad
  left/right character, D-pad up/down track, B back. The on-screen hints show
  these Xbox buttons.
- **Starting:** the host presses A; a 5-second "RACE STARTS IN N" countdown runs
  on every machine. A machine that has not finished loading after 30 s is left
  out instead of holding everyone on a black screen.
- **Pause:** Start pauses the race for everyone ("GAME PAUSED BY <name>"); after
  resuming, everyone gets a 3-2-1 countdown. A race paused for more than 120 s
  ends for all. Online, "Restart" in the pause menu just resumes and "Quit"
  leaves the session.
- **Players leaving:** if someone quits or loses the connection mid-race, the
  others keep racing ("<NAME> LEFT THE RACE"; that character disappears and is
  ranked last). If the host leaves, each client finishes the race offline.
- **After the race** everyone returns to the lobby on their own (the host
  re-hosts, clients rejoin).
- **Chat:** with a USB keyboard (through a controller-port USB adapter) the
  lobby chat line appears: type, Enter sends, Backspace erases. Without a
  keyboard the chat line is hidden.
- Two consoles/PCs in the same house can join each other from the online list
  (if the router cannot reach its own public address, the client falls back to
  finding the host on the LAN).

## Video modes

The game draws in 640x480 (4:3). By default the Xbox runs at 640x480: 480p when
the dashboard allows it (component/HD AV cable), 480i otherwise. 720p is chosen
in Options > Graphics (see above). Before a choice is saved, a text file
`VIDEO.TXT` next to `default.xbe` starting with `W` also selects 720p.

## Saves

Saves and settings go to `E:\UDATA\FFFF5352\` (shown in the dashboard's memory
manager as *Sonic R*): race saves, settings (`SONICR.INF`, `JOYSTICK.INF`, the
online token) and ghosts in separate folders. If that title ID is already used
by a different program, the next free ID is used.

## Troubleshooting

- **No music:** check `MUSIC\trackN.son` next to `default.xbe` (lower or upper
  case both work). Without the files the game is silent between sound effects.
- **Debug log:** create an empty file `DEBUG.TXT` next to `default.xbe`. The game
  then writes `SONICR.LOG` (start-up, video mode, network, timing, warnings) and
  enables the developer pad combos. Remove `DEBUG.TXT` for normal play: logging
  to the hard drive costs time.
- **Black screen when an online race starts:** after 30 s the race starts
  without the machine that is not ready. Check that everyone runs the same build.
- Sometimes a console starts with the audio hardware stuck from the previous
  program; the game detects that no audio is being mixed and restarts the audio
  DMA by itself.

## Building from source

Requirements: [nxdk](https://github.com/XboxDev/nxdk) and its prerequisites
(clang, lld, cmake, make, flex, bison), with
[pbgl](https://github.com/Ryzee119/pbgl) cloned into `nxdk/lib/pbgl`.

```sh
git clone --recursive https://github.com/XboxDev/nxdk.git
git clone https://github.com/Ryzee119/pbgl nxdk/lib/pbgl
eval "$(nxdk/bin/activate -s)"
make -C source/xbox NXDK_DIR=$PWD/nxdk -j8
```

The default goal produces `source/xbox/bin/default.xbe` and stamps the title ID
FFFF5352 into it (`tools/set_titleid.py`; cxbe always writes the generic
FFFF0002, which would move the saves). The music tool for Windows:

```sh
i686-w64-mingw32-gcc -O2 -static -s -o SonicR-Music.exe source/xbox/tools/sonicr_music/sonicr_music.c
```

## Technical notes

The desktop SDL2 sources in `source/sdl/src` are shared with the Windows, macOS,
Linux and Dreamcast builds, with `#ifdef SONICR_XBOX` where the console differs.
The Xbox pieces live in `source/xbox`:

| Piece     | On the Xbox |
|-----------|-------------|
| Toolchain | nxdk (clang + lld, pdclib, SDL2 port), whole build at `-O2` |
| Graphics  | pbgl (OpenGL 1.x on the NV2A), plus `xbox_fastdraw.c`, which writes the triangle fans straight into the pbkit push buffer (one `pb_end` per batch) |
| Display   | pbkit triple buffering with a guard against lost flips (the old black/blue flashes), scissor always on (pbgl never resets its window clip), full-framebuffer viewport with the 4:3 placement done in the projection |
| Audio     | `xbox_mixer.c`: the SDL_mixer subset the game uses, sound effects resampled while mixing (kept mono 22 kHz to save memory), music streamed by a producer thread into a ring buffer; start-up audio watchdog |
| Input     | SDL GameController; Black and the triggers remappable (extra bindings appended to `JOYSTICK.INF`); USB keyboards through libusbohci's HID driver (`xbox_kbd.c`) |
| Files     | `D:\` = the launch folder; saves redirected to `E:\UDATA\FFFF5352` with dashboard metadata (`xbox_entry.c`) |
| Network   | nxdk's lwIP behind small BSD socket shims (`source/xbox/include`), BearSSL without SSE2/AES-NI, seeded from cycle-counter jitter |
| Exit      | the AC97 is idled and the console fully rebooted, so the dashboard never inherits running audio |

Fidelity work against the 1998 EXE (all shared with the other platforms): the
water reflection, sky panorama, weather (rain, snow, splashes, lightning),
character jet/glow animation, particles, track IDs (the EXE numbers Factory 3
and Ruin 4; the decompilation swaps both consistently), fog and the 2-player
water scale were checked or corrected instruction by instruction. Small seams at
some geometry joins are part of the original game.

## Credits

- **Original game:** *Sonic R* (1997/1998) by **Traveller's Tales** and
  **Sonic Team**, published by **SEGA**. Sonic R and all related names are
  trademarks of SEGA. This project contains no game assets.
- **[jnmartin84](https://github.com/jnmartin84)**: the Sonic R decompilation and
  cross-platform engine (desktop, Dreamcast, online mode) this port is built on.
- **Xbox port:** all code of the Xbox port was written by **Claude** (Anthropic's
  AI model, through Claude Code).
- **XadPT:** did not write any code. XadPT gave Claude all the instructions and
  direction for the port, and tested everything that could be tested on a real
  original Xbox (and on Windows), reporting each problem until the port was
  faithful to the original and working.
- **[nxdk](https://github.com/XboxDev/nxdk)** and the **XboxDev** community:
  toolchain, kernel headers, pbkit, pdclib, the SDL2 port, lwIP integration,
  libusbohci and cxbe/extract-xiso.
- **[pbgl](https://github.com/Ryzee119/pbgl)** by **Ryzee119**: OpenGL on pbkit;
  Ryzee119 also maintains the Xbox USB stack used for pads and keyboards.
- **[SDL2](https://www.libsdl.org/)** (Sam Lantinga and contributors), **lwIP**
  (Adam Dunkels and contributors), **[BearSSL](https://bearssl.org/)** (Thomas
  Pornin), **libusbohci** (Nuvoton), **QR Code generator** (Project Nayuki).
- **Reality Jump matchmaker** (`sonicr.online`): the online session list and
  sign-in used by this port's online mode.
- Windows test builds: **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)**
  (Martin Storsjö), **SDL2_mixer**, **GLEW**, **miniupnpc** (Thomas Bernard).
