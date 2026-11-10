# Sonic R (latest release 2026/11/10)

Here you will find a reimplementation of *Sonic R* (the 1998 Sega PC release),
decompiled from the original executable and rebuilt as modern, portable C.
It runs natively on **Windows, macOS, and Linux** (via SDL2) and on the **Sega Dreamcast**
(via KallistiOS), sharing one game-logic codebase across all targets.

## AI Disclosure
This project is the product of several months of reverse-engineering and 
x86-to-C translation done with the use of Ghidra and Claude Code
(using Opus 4.6, Opus 4.8, Fable 5, Opus 5 and Opus 5.5). 

This has been extensively tested and validated against live WinDbg sessions running 1998 SONICR.EXE,
along with playtesting by many *detail-oriented* Sonic R fans (special thanks to Mittens, Tongara and Neo.charmy).

We ran out of identifiable differences in single-player gameplay behavior that could be attributed to anything other than switching from 80-bit x87 floating point to IEEE floating point math (or using modified game data).

If this use of AI is in conflict with your personal beliefs or values, please leave this page now. Thanks.

## Play

> **You supply your own game data.** This port is **only the game engine** — it
> does **not** include Sonic R's assets. You must own a copy of the game.

- **▶ Windows, macOS & Linux** → **[docs/desktop.md](docs/desktop.md)**
- **▶ Sega Dreamcast** → **[docs/dreamcast.md](docs/dreamcast.md)**

Each guide is self-contained: how to get or build it, the game data you supply,
controls, network play, saves, and troubleshooting for that platform.

## Legal & credits

Sonic R and its characters, trademarks, and game assets are the property of
Sega. This is a from-scratch reimplementation of the game engine — it contains
no original Sega code — and you must own a copy of Sonic R to supply the track,
model, texture, and music data needed to play.

A few small supporting assets are bundled under `DATA/` for convenience: the
custom network player platform icons (`NET01.RAW`) and the pre-encoded sound fx.
Everything else must come from your own copy of the game.

This is a non-commercial preservation project and is not affiliated with or
endorsed by Sega.

If you sell this, I hope all the bad things in life happen to you and only to you.
