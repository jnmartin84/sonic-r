# Sonic R on the original Xbox

*Back to the [README](../README.md).*

> **Status: runs on real hardware** (tested by XadPT on an original Xbox).
> Menus, races, audio, saves and the in-game reset work. Performance is good,
> with a 30 fps cap even at the longest draw distance. Some visual bugs remain,
> see [Known limitations](#known-limitations). Reports with `SONICR.LOG` are very
> welcome.

> **You supply your own game data.** This build is only the game engine. It
> contains none of Sonic R's assets. You must own the 1998 PC release.

## What this is

The desktop SDL2 sources in `source/sdl/src` are reused as they are, with a few
`#ifdef SONICR_XBOX` blocks. The Xbox-specific pieces live in `source/xbox`:

| Piece            | How it is done on Xbox                                                   |
|------------------|--------------------------------------------------------------------------|
| Toolchain        | [nxdk](https://github.com/XboxDev/nxdk) (clang + lld, SDL2 port)          |
| Graphics         | [pbgl](https://github.com/Ryzee119/pbgl): OpenGL 1.x on the NV2A GPU      |
| Textures         | RGBA8 upload (pbgl has no RGB565); 16-bit formats are a possible later saving |
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

Saves go to the hard drive in `E:\UDATA\FFFF5352` (the title ID is patched
into the XBE after the build with `source/xbox/tools/set_titleid.py`).

## Xbox-specific features

- **Exit to Dashboard** is an entry in the options menu.
- **In-game reset (IGR):** hold left trigger + right trigger + Back + Black for
  about half a second, in any menu or race, to return to the dashboard.
- **Controls** page is gamepad only, with Xbox pad mapping. Back cancels a
  remap. White + Back cycles the debug render mode.
- **Graphics** page has a Draw Distance setting. Far is the PC maximum and Very
  Far goes beyond it.
- The whole build, including nxdk's libraries, is compiled with `-O2`, with a
  fast `memcpy`/`memset` and a faster texture upload.
- `SONICR.LOG` starts with the build time of the XBE, so you can tell which
  build produced it.
- **Water** uses the faithful retail Direct3D renderer (the mirrored sky and
  sun panorama rippled by multi-octave noise), as in the PC version. The
  desktop build defaults to a prototype that imitates the old software mode
  (`WATER_RIPPLE_SW` in `render_track_d3d.c`).

## Video mode

The game renders at 640x480 (4:3). By default the Xbox build uses **640x480**:
480p if the dashboard has 480p enabled (HD AV pack), 480i if not.

720p uses several MB more video memory and RAM is tight, so it is opt-in:
create a text file `VIDEO.TXT` next to `default.xbe` whose first character is
`7`. If the dashboard allows 720p the game then runs at 1280x720 with the 4:3
image pillarboxed in the middle; otherwise it falls back to 640x480.

## Known limitations

- No online play.
- Memory is the main constraint (64 MB total). Sound effects are kept in their
  original mono 22.05 kHz format (about 8 MB) and resampled while mixing;
  converting them to 44.1 kHz stereo used 32 MB and left no room for textures.
  `SONICR.LOG` shows the free memory at several points.
- Rendering relies on pbgl's fixed-function path; any texture or blend mode it
  does not implement will look wrong rather than crash.
- Known visual bug: a texture (the "dog") that sometimes disappears. Under
  investigation.
- Fixed: the black/blue flashes (a dark rectangle, mostly over the lower half of
  the picture, every few seconds, even while paused). pbkit's triple buffering
  can lose a flip when the frame-ready interrupt and a VBlank are serviced in
  the same DPC pass: the VBlank handler marks the buffer as shown although the
  display was never pointed at it, and the next frame then clears the buffer
  that is still on screen. The port now checks the display address while the
  next frame is drawn and repairs a lost flip right away; as a fallback, before
  drawing into a back buffer it waits for (or forces) the pending flip if that
  buffer is still on screen. The log counts the repairs ("lost flips fixed").
- Fixed: the streaky, stretched floor seen on Factory. The playfield grid (and
  water reflection) no longer write depth, so they cannot z-fight with the
  track surface.
- Fixed: after a 2-player (split-screen) race the whole screen stayed cropped to
  the right half, even in menus. pbgl only writes the GPU window clip while the
  scissor test is on and never resets it when it is turned off, so the Xbox
  backend now keeps the scissor test always on and treats "disabled" as a
  full-screen rectangle.

## Credits

- **jnmartin84**: the Sonic R decompilation and the cross-platform engine this
  port is built on.
- **XadPT**: the Xbox port. Hardware testing and bug reports on a real console,
  feature requests and direction.
- **[nxdk](https://github.com/XboxDev/nxdk)** (XboxDev) and its **pbgl**
  OpenGL layer: the toolchain and graphics backend.
- Code written with Claude Code (Anthropic), working from XadPT's test results.
