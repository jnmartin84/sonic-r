# Sonic R on the original Xbox (experimental)

*Back to the [README](../README.md).*

> **Status: builds, not yet verified on real hardware or xemu.** The XBE links
> and the ISO is produced, but nobody has booted it yet. Expect to find bugs.
> Reports with the on-screen error or a serial/debug log are very welcome.

> **You supply your own game data.** This build is only the game engine. It
> contains none of Sonic R's assets. You must own the 1998 PC release.

## What this is

The desktop SDL2 sources in `source/sdl/src` are reused as they are, with a few
`#ifdef SONICR_XBOX` blocks. The Xbox-specific pieces live in `source/xbox`:

| Piece            | How it is done on Xbox                                                   |
|------------------|--------------------------------------------------------------------------|
| Toolchain        | [nxdk](https://github.com/XboxDev/nxdk) (clang + lld, SDL2 port)          |
| Graphics         | [pbgl](https://github.com/Ryzee119/pbgl): OpenGL 1.x on the NV2A GPU      |
| Textures         | RGBA8 upload (same path as the big-endian build; pbgl has no RGB565)     |
| Audio            | `src/xbox_mixer.c`, a minimal `SDL_mixer` subset on top of SDL audio      |
| Input            | SDL GameController (standard mapping)                                    |
| Game data path   | `D:\`, the folder `default.xbe` was launched from                         |
| Network play     | **Not included** (`src/xbox_net_stub.c`). Single player / split-screen only |

## Build

You need nxdk and its prerequisites (clang, lld, cmake, make, flex, bison).
Then clone pbgl into nxdk and build:

```sh
git clone --recursive https://github.com/XboxDev/nxdk.git
git clone https://github.com/Ryzee119/pbgl nxdk/lib/pbgl
eval "$(nxdk/bin/activate -s)"
make -C source/xbox NXDK_DIR=$PWD/nxdk -j8
```

Outputs: `source/xbox/bin/default.xbe` and `source/xbox/Sonic R.iso`
(engine only, no game data).

## Game data

Copy these folders from your PC copy of Sonic R: `GENERAL`, `ISLAND`, `CITY`,
`RUIN`, `FACTORY`, `EMERALD`, `AI`, `BIN` and `SOUND`. The `DATA/` folder in this
repo holds a few small files that belong in `GENERAL`, `BIN` and `SOUND`.

**On the Xbox hard drive:** put `default.xbe` and the folders together, for
example in `E:\Games\SonicR\`, and launch it from your dashboard.

**In xemu or from a disc image:** put the folders inside `source/xbox/bin/`
next to `default.xbe`, then rebuild the image:

```sh
nxdk/tools/extract-xiso/build/extract-xiso -c source/xbox/bin "Sonic R.iso"
```

Do not share images that contain game data.

### Which copy of the game

The engine was validated upstream against the **1998 PC release** (`SONICR.EXE`).
The upstream docs do not list supported regions or reprints, so treat other
versions as untested. Dreamcast or Saturn data will not work. Folder and file
names should be UPPERCASE.

### Music

The PC game plays its music from CD audio tracks, which are not files. Put them
in a `MUSIC` folder as `track2` ... `track21`. On Xbox only these formats work:
`.son` (raw 44.1 kHz 16-bit stereo PCM), `.adx` and `.adp`. The `.ogg`, `.mp3`,
`.flac` and `.wav` paths used by the desktop build need SDL_mixer decoders that
the Xbox build does not have, so convert your rips to `.son` first. Without
music the game still runs, just silent.

### Log

On startup the game writes errors to `SONICR.LOG` next to `default.xbe` (if the
folder is writable). If it fails to start, send that file.

### Saves

`SAVE` and `GHOST` are created next to `default.xbe` on startup. This only works
if that folder is writable, so run from the hard drive, not from a disc image.

## Video mode

The game renders at 640x480 (4:3). On startup the Xbox build picks:

1. **1280x720** if 720p is enabled in the dashboard's video settings. The 4:3
   image is pillarboxed in the middle (black bars left and right).
2. Otherwise **640x480**: 480p if the dashboard has 480p enabled (HD AV pack),
   480i if not.

To force 480 even when 720p is enabled, create a text file `VIDEO.TXT` next to
`default.xbe` whose first character is `4`.

## Known limitations

- No online play.
- Memory: the original targets the Dreamcast (16 MB), so 64 MB should be fine,
  but nothing has been measured at runtime yet.
- Rendering relies on pbgl's fixed-function path; any texture or blend mode it
  does not implement will look wrong rather than crash.
