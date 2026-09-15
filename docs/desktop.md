# Playing Sonic R on Windows, macOS & Linux

![Resort Island Windows Screenshot](./sonic_r_windows.png)

*Looking for the Dreamcast instead? See [dreamcast.md](dreamcast.md). Back to the [README](../README.md).*

> **You supply your own game data.** This port is only the game engine — it does
> not include any of Sonic R's assets. You must own a copy of the game to provide
> them. See [Game data you supply](#game-data-you-supply).

---

## Get the game

**Windows — no build needed.** Download `SONICR.EXE` from the
[Releases page](https://github.com/jnmartin84/sonic-r/releases). It's a prebuilt,
statically-linked x86-64 executable that bundles all of its libraries, so there's nothing to install — put it in your data directory and
double-click it.

**macOS / Linux** — there's no prebuilt binary; build from source (it's quick,
just a couple of dependencies). See [Building from source](#building-from-source).

---

## Running the game

Launch the executable and point it at your data directory:

```sh
./sonicr /path/to/sonic-r-data          # macOS / Linux
```
```sh
SONICR.EXE C:\path\to\sonic-r-data      # Windows
```

If you launch with no argument, the game looks for the data folders next to the
executable, then in the current working directory — so the simplest setup is to
put the executable **inside** your data directory and just run it.

### Command-line options

| Option            | Effect                                                     |
|-------------------|------------------------------------------------------------|
| *(path)*          | Data directory to use (first non-option argument)          |
| `--fullscreen`    | Start in fullscreen instead of a window                    |
| `--unlock`        | Unlock all characters and tracks                           |
| `--host <ip>`     | Direct-connect network play to a host at `<ip>`            |
| `--port <n>`      | Network port (default **7847**)                            |
| `--username <name>` | Set your display name for online play                    |

Example — fullscreen, all content unlocked, from a data folder:

```sh
./sonicr --fullscreen --unlock ~/Games/SonicR      # macOS / Linux
```
```sh
SONICR.EXE --fullscreen --unlock C:\Games\SonicR   # Windows
```

---

## Game data you supply

This port contains **only the game engine**. It does **not** include Sonic R game
data. To play, you must supply the data files from a copy of the game you own.

### Required data

Copy these folders from an installed copy of the Sonic R PC release into a
single **data directory**:

| Folder     | Contents                                             |
|------------|------------------------------------------------------|
| `GENERAL`  | Shared textures, character sheets, weather sprites   |
| `ISLAND`   | Resort Island track + textures                       |
| `CITY`     | Radical City track + textures                        |
| `RUIN`     | Regal Ruin track + textures                          |
| `FACTORY`  | Reactive Factory track + textures                    |
| `EMERALD`  | Radiant Emerald track + textures                     |
| `AI`       | Opponent pathfinding data                            |
| `BIN`      | Models, menus, titles, demos, environment maps       |
| `SOUND`    | Sound effects                                        |

Two more folders are created automatically the first time you play, but you
can create them yourself if you prefer:

| Folder    | Contents                                    |
|-----------|---------------------------------------------|
| `SAVE`    | Save game (`SONICR.SAV`) and pad config     |
| `GHOST`   | Time Attack ghost recordings                |

> **Casing is handled automatically.** The engine builds its asset paths in
> UPPERCASE to match the original data layout. On Windows and macOS
> (case-insensitive filesystems) any casing works; on case-sensitive
> filesystems (Linux, case-sensitive macOS volumes) the desktop build falls
> back to a case-insensitive directory lookup, so a data folder copied
> straight from the PC release loads without renaming. The Dreamcast disc
> still needs uppercase ISO9660 names — see [dreamcast.md](dreamcast.md).

### Bundled extras — copy these in

A couple of assets the original PC release doesn't include ship in this repo
under `DATA/`:

- `BIN/OPTION/NET01.RAW` — the network-screen platform icons.
- `SOUND/SFX/REPLAY*.WAV` — the replay-race announcer voice, pre-split so it
  plays back at full quality.

After laying out your data directory, copy the contents of `DATA/` into it:

```sh
cp -R DATA/. /path/to/gamedata/          # macOS / Linux
```
```sh
robocopy DATA C:\path\to\gamedata /E     # Windows (merges DATA's contents in)
```

### Music

Put your music tracks in a `MUSIC` folder in the data directory, named by CD track
number — `MUSIC/track2.<ext>`, `MUSIC/track3.<ext>`, … through `track21`.

Supported formats, in the order the game looks for them (the first matching file
for a track number wins):

| Extension | Source                      | Format                                         |
|-----------|-----------------------------|------------------------------------------------|
| `.adx`    | *Sonic R Updater*           | CRI ADX ADPCM, decoded on demand               |
| `.son`    | some PC releases            | Raw headerless PCM (44.1 kHz, 16-bit, stereo)  |
| `.wav`    | rip your CD audio           | WAV / PCM                                       |
| `.adp`    | convert from any source     | Raw headerless Yamaha ADPCM (44.1 kHz, stereo) |
| `.ogg`    | rip / convert your CD audio | Ogg Vorbis                                      |
| `.mp3`    | rip / convert your CD audio | MP3                                            |
| `.flac`   | rip / convert your CD audio | FLAC                                           |

The easiest setup is to run Sonic R Updater and take the `.adx` files it installs
as music under its mods directory. Second easiest: if you had a PC version with
`.son` files, just copy those. Everything else works too — ripping/converting CD
audio to `.ogg` or `.wav` is just more time-consuming.

---

## Controls

### Keyboard (default)

Both players can play on one keyboard.

| Action              | Player 1      | Player 2 |
|---------------------|---------------|----------|
| Left / Right        | ← / →         | J / L    |
| Up / Down           | ↑ / ↓         | I / K    |
| Accelerate          | A             | U        |
| Jump                | Space         | O        |
| Sharp turn left     | Z             | N        |
| Sharp turn right    | X             | M        |
| Look back           | 1             | 2        |
| Start / confirm     | Enter         | P        |

Controls are **remappable in-game** from the Options → Controls screen. Your
bindings are saved to `KEYS.BIN` in the data directory.

### Gamepad

Any controller recognized by SDL's game-controller database works out of the box
(Xbox, PlayStation, Switch Pro, and most USB pads). Up to **four** pads are
supported for split-screen. Button assignments can be reconfigured per pad in the
Options screen and are saved with your configuration.

---

## Game modes

All original game modes are supported, including split-screen multiplayer.

---

## Network play

Online and LAN play use UDP on port **7847**. Everyone in a session must be
running the **same build and the same game data**.

- **LAN:** the host starts a session and clients discover it automatically over
  the local network via broadcast — no IP needed.
- **Direct connect:** a client can join a specific host with `--host <ip>`,
  useful across networks or when broadcast is blocked.
- **Matchmaker:** an internet matchmaking service lets players find each other
  online without exchanging IPs. Set your name with `--username <name>`.

**Cross-play with Dreamcast.** A Dreamcast speaks the identical protocol, so you
can share a session with Dreamcast players in any combination, as long as everyone
is on the same version and data.

### Signing in and out

Signing in to the matchmaker saves a small token — a file named `ONLINE.DAT` in
your data directory (your credential plus your username) — so you stay signed in
on later launches. To **sign out**, or if the matchmaker misbehaves (a stale
token, the wrong name, a sign-in that won't take), delete `ONLINE.DAT` and sign in
again. The next successful sign-in writes a fresh token.

---

## Save data

Everything the game saves lives in your data directory:

| What                  | File                             |
|-----------------------|----------------------------------|
| Progress / records    | `SAVE/SONICR.SAV`                |
| Options               | `SONICR.INF`                     |
| Gamepad configuration | `JOYSTICK.INF` / `SAVE/PADS.CFG` |
| Keyboard bindings     | `KEYS.BIN`                       |
| Time Attack ghosts    | `GHOST/`                         |
| Matchmaker sign-in    | `ONLINE.DAT`                     |

To reset progress, delete `SONICR.SAV`. To reset controls, delete the keyboard
(`KEYS.BIN`) or gamepad (`JOYSTICK.INF`) bindings and reconfigure in Options. To
sign out of the matchmaker, delete `ONLINE.DAT`.

---

## Troubleshooting

| Symptom                               | Fix                                                      |
|---------------------------------------|----------------------------------------------------------|
| "Cannot chdir to data directory"      | The path you passed doesn't exist — check it.            |
| Black screen / missing textures       | Data folders missing or incomplete — check every required folder is present. |
| Crash right after the logos           | Incomplete data set — make sure all folders are present. |
| No music                              | Add a `MUSIC` folder with the track files.               |
| Gamepad not detected                  | Check the pad works in another SDL game.                 |
| Controls feel wrong                   | Reset them in Options, or delete `KEYS.BIN` / `JOYSTICK.INF`. |
| Can't find a LAN game                 | Use `--host <ip>` to connect directly.                   |
| Matchmaker won't sign in / wrong name | Delete `ONLINE.DAT` and retry.                           |

---

## Building from source

Only needed on macOS/Linux, or to modify the game. (On Windows, just grab the
prebuilt `SONICR.EXE` from [Releases](https://github.com/jnmartin84/sonic-r/releases).)

**Dependencies:** a C compiler, `make`, and the **SDL2**, **SDL2_mixer**, and
**miniupnpc** development libraries. OpenGL is used for rendering — it's a system
framework on macOS and Windows, and needs the Mesa dev package on Linux. (BearSSL,
used by the online matchmaker for HTTPS, is bundled in this repo — nothing to
install.)

- **macOS:** `brew install sdl2 sdl2_mixer miniupnpc`
- **Linux (Debian/Ubuntu):** `sudo apt install build-essential libsdl2-dev libsdl2-mixer-dev libminiupnpc-dev libgl1-mesa-dev`
- **Windows:** [MSYS2](https://www.msys2.org/), UCRT64 shell. The Windows build
  links **statically**, so SDL2_mixer's audio-codec libraries must be installed
  too (they're what SDL2_mixer is linked against, not optional here):

  ```sh
  pacman -S make mingw-w64-ucrt-x86_64-{gcc,SDL2,SDL2_mixer,miniupnpc,opusfile,opus,libogg,libvorbis,flac,mpg123,libxmp,wavpack}
  ```

  The remaining Windows system libraries the static link needs (`opengl32`,
  `ws2_32`, `iphlpapi`, `imm32`, `ole32`, `setupapi`, `winmm`, …) ship with the
  MinGW-w64 toolchain — no separate install.

Build:

```sh
cd source/sdl
make
```

This produces the `sonicr` executable (a static `.exe` on Windows). Run it as
described in [Running the game](#running-the-game).

The macOS build enables AddressSanitizer/UBSan by default for development.
