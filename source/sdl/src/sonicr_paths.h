/**
 * sonicr_paths.h — File path definitions
 *
 * All game data file paths in one place. Paths use forward slashes
 * and ALL CAPS filenames to match the original game data layout.
 *
 * DATA_DIR is the base directory containing the game data folders
 * (GENERAL, ISLAND, CITY, RUIN, FACTORY, EMERALD, SAVE, GHOST, AI).
 */

#ifndef SONICR_PATHS_H
#define SONICR_PATHS_H

/* Base data directory — relative to cwd (main.c chdir's to argv[1] or this default) */
#ifndef DATA_DIR
#define DATA_DIR    "."
#endif

/* Path separator */
#ifdef SONICR_XBOX
#define SEP         "\\"
#else
#define SEP         "/"
#endif

/* General data */
#define PATH_GENERAL            DATA_DIR SEP "GENERAL" SEP
#define PATH_GENERAL_BIT        PATH_GENERAL "SONICR.BIT"
#define PATH_GENERAL_RAW        PATH_GENERAL "SONICR.RAW"

/* Port addition, DC only — 32x32 RGB565 tile repeated across the bottom
 * scanline row in the 448-line split-screen modes. Uppercase so it survives
 * ISO9660 on a burned or emulated image. */
#define PATH_PAD448             DATA_DIR SEP "PAD448.TEX"

/* Per-track data directories */
#define PATH_ISLAND             DATA_DIR SEP "ISLAND" SEP
#define PATH_CITY               DATA_DIR SEP "CITY" SEP
#define PATH_RUIN               DATA_DIR SEP "RUIN" SEP
#define PATH_FACTORY            DATA_DIR SEP "FACTORY" SEP
#define PATH_EMERALD            DATA_DIR SEP "EMERALD" SEP

/* Track geometry files (.BIN) — note the abbreviated/suffixed names */
#define PATH_ISLAND_BIN         PATH_ISLAND  "ISLAND_E.BIN"
#define PATH_CITY_BIN           PATH_CITY    "CITY_B.BIN"
#define PATH_RUIN_BIN           PATH_RUIN    "RUIN_D.BIN"
#define PATH_FACTORY_BIN        PATH_FACTORY "FACTRY_E.BIN"
#define PATH_EMERALD_BIN        PATH_EMERALD "EMRALD_B.BIN"

/* Option screen 3D model data */
#define PATH_OPTION3_BIN        PATH_OPTION "OPTION3.BIN"

/* Per-track texture files (256×256 RGB) */
#define PATH_RUIN00_RAW         PATH_RUIN    "RUIN00.RAW"
#define PATH_RUIN01_RAW         PATH_RUIN    "RUIN01.RAW"
#define PATH_RUIN03_RAW         PATH_RUIN    "RUIN03.RAW"
#define PATH_FACT00_RAW         PATH_FACTORY "FACT00.RAW"
#define PATH_FACT01_RAW         PATH_FACTORY "FACT01.RAW"
#define PATH_FACT02_RAW         PATH_FACTORY "FACT02.RAW"
#define PATH_ISLAND01_RAW       PATH_ISLAND  "ISLAND01.RAW"
#define PATH_ISLAND02_RAW       PATH_ISLAND  "ISLAND02.RAW"
#define PATH_ISLAND03_RAW       PATH_ISLAND  "ISLAND03.RAW"
#define PATH_ISLAND04_RAW       PATH_ISLAND  "ISLAND04.RAW"
#define PATH_CITY00_RAW         PATH_CITY    "CITY00.RAW"
#define PATH_CITY02_RAW         PATH_CITY    "CITY02.RAW"
#define PATH_CAS00_RAW          PATH_EMERALD "CAS00.RAW"

/* Track terrain/AI */
#define PATH_AI                 DATA_DIR SEP "AI" SEP

/* Save data */
#define PATH_SAVE               DATA_DIR SEP "SAVE" SEP
#define PATH_SAVE_GAME          PATH_SAVE "SONICR.SAV"
#define PATH_SAVE_PADS          PATH_SAVE "PADS.CFG"

/* Ghost replay data */
#define PATH_GHOST              DATA_DIR SEP "GHOST"      /* no trailing / — BuildGhostPath adds its own */

/* Demo replay files — from strings in SONICR.EXE: "bin\demos\island.dem" */
#define PATH_DEMOS              DATA_DIR SEP "BIN" SEP "DEMOS" SEP

/* Terrain (.TER) files — from strings in SONICR.EXE: "bin\extras\island.ter" */
#define PATH_EXTRAS             DATA_DIR SEP "BIN" SEP "EXTRAS" SEP
#define PATH_ISLAND_TER         PATH_EXTRAS "ISLAND.TER"
#define PATH_CITY_TER           PATH_EXTRAS "CITY.TER"
#define PATH_RUIN_TER           PATH_EXTRAS "RUIN.TER"
#define PATH_FACTORY_TER        PATH_EXTRAS "FACTORY.TER"
#define PATH_EMERALD_TER        PATH_EXTRAS "EMERALD.TER"

/* Unlock / ending screens — from strings at 0x5075D4: "bin\end\<name>.raw" */
#define PATH_END                DATA_DIR SEP "BIN" SEP "END" SEP
#define PATH_END_EMERALDS       PATH_END "EMERALDS.RAW"
#define PATH_END_SONIC          PATH_END "SONIC.RAW"
#define PATH_END_TAILS          PATH_END "TAILS.RAW"
#define PATH_END_KNUCKLES       PATH_END "KNUCKLES.RAW"
#define PATH_END_AMY            PATH_END "AMY.RAW"
#define PATH_END_ROBOTNIK       PATH_END "ROBOTNIK.RAW"
#define PATH_END_MSONIC         PATH_END "MSONIC.RAW"
#define PATH_END_DTAILS         PATH_END "DTAILS.RAW"
#define PATH_END_MKNUCK         PATH_END "MKNUCK.RAW"
#define PATH_END_MROBOT         PATH_END "MROBOT.RAW"
#define PATH_END_SSONIC         PATH_END "SSONIC.RAW"
#define PATH_END_THE_END        PATH_END "THE_END.RAW"

/* AI pathfinding data — from strings in SONICR.EXE: "ai\aistuffi.bin" */
#define PATH_AI_ISLAND          DATA_DIR SEP "AI" SEP "AISTUFFI.BIN"
#define PATH_AI_CITY            DATA_DIR SEP "AI" SEP "AISTUFFC.BIN"
#define PATH_AI_RUIN            DATA_DIR SEP "AI" SEP "AISTUFFR.BIN"
#define PATH_AI_FACTORY         DATA_DIR SEP "AI" SEP "AISTUFFF.BIN"
#define PATH_AI_EMERALD         DATA_DIR SEP "AI" SEP "AISTUFFE.BIN"

/* Character model base meshes — BIN/OBJECTS/<CHAR>/<FILE>.BIN */
#define PATH_OBJECTS            DATA_DIR SEP "BIN" SEP "OBJECTS" SEP
#define PATH_MDL_SONIC          PATH_OBJECTS "SONIC"    SEP "SONIC_H.BIN"
#define PATH_MDL_TAILS          PATH_OBJECTS "TAILS"    SEP "TAILS_H.BIN"
#define PATH_MDL_KNUCKLES       PATH_OBJECTS "KNUCKLES" SEP "KNUCK_H.BIN"
#define PATH_MDL_AMY            PATH_OBJECTS "AMY"      SEP "AMY_H3.BIN"
#define PATH_MDL_EGGMAN         PATH_OBJECTS "ROBOTNIK" SEP "ROBOTZ.BIN"
#define PATH_MDL_METALSONIC     PATH_OBJECTS "MSONIC"   SEP "MSONICZ.BIN"
#define PATH_MDL_TAILSDOLL      PATH_OBJECTS "DTAILS"   SEP "DTAILSZ.BIN"
#define PATH_MDL_METALKNUCKLES  PATH_OBJECTS "MKNUCK"   SEP "MKNUCKZ.BIN"
#define PATH_MDL_EGGROBO        PATH_OBJECTS "MROBOT"   SEP "MROBOTZ.BIN"
#define PATH_MDL_SUPERSONIC     PATH_OBJECTS "SSONIC"   SEP "SSONICZ.BIN"

/* Character Gouraud lighting tables (.GRD) */
#define PATH_GRD_SONIC          PATH_OBJECTS "SONIC"    SEP "SONIC_H.GRD"
#define PATH_GRD_TAILS          PATH_OBJECTS "TAILS"    SEP "TAILS_H.GRD"
#define PATH_GRD_KNUCKLES       PATH_OBJECTS "KNUCKLES" SEP "KNUCK_H.GRD"
#define PATH_GRD_AMY            PATH_OBJECTS "AMY"      SEP "AMY_H3.GRD"
#define PATH_GRD_EGGMAN         PATH_OBJECTS "ROBOTNIK" SEP "ROBOTNIK.GRD"
#define PATH_GRD_METALSONIC     PATH_OBJECTS "MSONIC"   SEP "MSONIC.GRD"
#define PATH_GRD_TAILSDOLL      PATH_OBJECTS "DTAILS"   SEP "DTAILS.GRD"
#define PATH_GRD_METALKNUCKLES  PATH_OBJECTS "MKNUCK"   SEP "MKNUCK.GRD"
#define PATH_GRD_EGGROBO        PATH_OBJECTS "MROBOT"   SEP "MROBOT.GRD"
#define PATH_GRD_SUPERSONIC     PATH_OBJECTS "SSONIC"   SEP "SSONIC.GRD"

/* Character textures */
#define PATH_PLAYER00_RAW       PATH_GENERAL "PLAYER00.RAW"
#define PATH_PLAYER01_RAW       PATH_GENERAL "PLAYER01.RAW"

/* Environment map textures (128×128 RGB).
 *
 * SONICR.RAW is the default env-map loaded once at startup
 * (binary 0x470805 → game_loop.c:517) into g_tpageParallax2 at (0,0).
 *
 * NO1..NO5.RAW are the per-TRACK env-map tiles (128x128 RGB), loaded
 * into g_tpageParallax1 at (0,0,128,128) by each track's Init function:
 *   InitIsland  0x004736e3 -> NO1.RAW
 *   InitCity    0x00473df8 -> NO2.RAW
 *   InitRuin    0x004744e0 -> NO3.RAW
 *   InitFactory 0x00474bc8 -> NO4.RAW
 *   InitEmerald 0x00474fb8 -> NO5.RAW
 * 3D objects on each track (starting-line flags, boost-trail ribbon,
 * shiny decorations) sample this 128x128 region via their mesh UVs.
 *
 * A prior translator's note said these were "per-finish-position"
 * gold/silver/bronze/etc. overlays. That interpretation came from
 * ResultsScreen (binary 0x4c750e..0x4c7755) which RE-BINDS the same
 * five files into g_tpageParallax2 indexed by (short)g_racePlacement,
 * so the results-screen trophy shimmer changes color with placement.
 * The files themselves are per-track, not per-finish-position. */
#define PATH_EMAP               DATA_DIR SEP "BIN" SEP "EMAP" SEP
#define PATH_EMAP_SONICR        PATH_EMAP "SONICR.RAW"
#define PATH_EMAP_NO1           PATH_EMAP "NO1.RAW"   /* Island env-map */
#define PATH_EMAP_NO2           PATH_EMAP "NO2.RAW"   /* City env-map */
#define PATH_EMAP_NO3           PATH_EMAP "NO3.RAW"   /* Ruin env-map */
#define PATH_EMAP_NO4           PATH_EMAP "NO4.RAW"   /* Factory env-map */
#define PATH_EMAP_NO5           PATH_EMAP "NO5.RAW"   /* Emerald env-map */

/* Per-track minimap textures (96×80 RGB) */
#define PATH_MAP_ISLAND         PATH_ISLAND  "MAP_I.RAW"
#define PATH_MAP_CITY           PATH_CITY    "MAP_C.RAW"
#define PATH_MAP_RUIN           PATH_RUIN    "MAP_R.RAW"
#define PATH_MAP_FACTORY        PATH_FACTORY "MAP_F.RAW"
#define PATH_MAP_EMERALD        PATH_EMERALD "MAP_E.RAW"

/* Per-track parallax textures (1664×128 RGB) — resolution/weather variants */
#define PATH_PAR_ISLAND         PATH_ISLAND  "PARALLAX" SEP
#define PATH_PAR_CITY           PATH_CITY    "PARALLAX" SEP
#define PATH_PAR_RUIN           PATH_RUIN    "PARALLAX" SEP
#define PATH_PAR_FACTORY        PATH_FACTORY "PARALLAX" SEP
#define PATH_PAR_EMERALD        PATH_EMERALD "PARALLAX" SEP

/* Weather particle textures (256×28 RGB) — software/D3D variants */
#define PATH_FLAKE_RAW          PATH_GENERAL "FLAKE.RAW"
#define PATH_FLAKE2_RAW         PATH_GENERAL "FLAKE2.RAW"
#define PATH_PLOP_RAW           PATH_GENERAL "PLOP.RAW"
#define PATH_PLOP2_RAW          PATH_GENERAL "PLOP2.RAW"
#define PATH_RAINBOW_RAW        PATH_GENERAL "RAINBOW.RAW"

/* Icon texture */
#define PATH_ICON01_RAW         PATH_GENERAL "ICON01.RAW"

/* Menu sprite sheets (256×256 RGB) — in BIN/OPTION/ */
#define PATH_OPTION             DATA_DIR SEP "BIN" SEP "OPTION" SEP
#define PATH_MENU_MODESEL       PATH_OPTION "SMODE00.RAW"
#define PATH_MENU_TIMEATTACK    PATH_OPTION "STAMOD00.RAW"
#define PATH_MENU_MULTIMODE     PATH_OPTION "SMPMOD00.RAW"
#define PATH_MENU_CHARSEL       PATH_OPTION "SCHAR00.RAW"
#define PATH_MENU_COURSE0       PATH_OPTION "SCRSE00.RAW"
#define PATH_MENU_COURSE1       PATH_OPTION "SCRSE01.RAW"
#define PATH_MENU_RANKING       PATH_OPTION "RANK00.RAW"
#define PATH_MENU_OPTIONS0      PATH_OPTION "OPT00.RAW"
#define PATH_MENU_OPTIONS1      PATH_OPTION "OPT01.RAW"
#define PATH_MENU_CONTROLS      PATH_OPTION "CTRL00.RAW"
#define PATH_MENU_MULTIPLAYER   PATH_OPTION "MP00.RAW"
#define PATH_MENU_OPTION00      PATH_OPTION "OPTION00.RAW"
#define PATH_MENU_LOADSAVE      PATH_OPTION "LOAD00.RAW"

/* Title/logo screen textures */
#define PATH_TITLES             DATA_DIR SEP "BIN" SEP "TITLES" SEP
#define PATH_SEGALOGO_RAW       PATH_TITLES "SEGALOGO.RAW"
#define PATH_TTLOGO_RAW         PATH_TITLES "TTLOGO.RAW"
#define PATH_TITLES_RAW         PATH_TITLES "TITLES.RAW"
#define PATH_TITLES00_RAW       PATH_TITLES "TITLES00.RAW"
#define PATH_TITLES3_BIN        PATH_TITLES "TITLES3.BIN"

#endif /* SONICR_PATHS_H */
