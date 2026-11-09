/**
 * track_per_level.c — Per-track initialization functions
 *
 * Each track has its own init function that:
 *   1. Sets texture page assignments
 *   2. Loads textures via LoadTPageRGB (D3D path)
 *   3. Opens track geometry files
 *   4. Sets world position constants (floats for camera bounds)
 *   5. Calls core loaders: LoadTrack3, LoadTerrain, LoadAI
 *   6. Applies per-track lighting tint
 *
 * Texture filenames and tpage assignments verified from disassembly
 * of each Init* function using capstone.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"

/* Binary passes struct base at 0x713080 + charId*0x50, which is 0x24 bytes
   before g_modelMeta[]. LoadLightingTable only reads info[9] (vertexStart)
   and info[10] (vertexCount), mapping to g_modelMeta[charId].vertexStart/vertexCount.
   We use a temp struct to avoid negative-index pointer arithmetic. */
static const char *grdPaths[10] = {
    PATH_GRD_SONIC, PATH_GRD_TAILS, PATH_GRD_KNUCKLES, PATH_GRD_AMY,
    PATH_GRD_EGGMAN, PATH_GRD_METALSONIC, PATH_GRD_TAILSDOLL,
    PATH_GRD_METALKNUCKLES, PATH_GRD_EGGROBO, PATH_GRD_SUPERSONIC
};

/* Renderer helpers used by all per-track init functions */
extern void R_FreezeTexture(int tpage);
extern void R_UploadTextureSubRect(int, int, int, int, int);
extern void R_ClearTextureDirty(int tpage);
/* Marks a tpage so its system-RAM pixel buffer survives the eager-upload
 * free path (DC) — required for tpages that GenerateSkyGradientTpage and
 * other post-load mutators read AFTER first upload. */
extern void GL_KeepPixels(int tpage);

/* Additional tpage globals not in the main header */
extern int g_tpageUIAlt2;       /* 0x008F6C58 */

/* World position floats — camera/physics bounds */
float g_worldBoundsA;               /* _DAT_006D75C0 — world scale factor (20..30) */
float g_worldBoundsB;               /* _DAT_006D75CC */
float g_worldBoundsC;               /* _DAT_006D75D0 */
float g_worldBoundsD;               /* _DAT_006D75D4 */
float g_worldBoundsE;               /* _DAT_006D75D8 */

/* Forward declarations for loading helpers */
void SetupD3DTexturesBegin(void);       /* FUN_00438CA4 */
void FinalizeD3DTextures(void);         /* FUN_00438D10 */
void LoadTextureSubRect(const char *filename, int tpage,
                        int width, int height, int destX, int destY); /* FUN_0042A848 */
void OpenTrackFileA(void);              
void OpenTrackFileB(void);              
void SkipTrackHeader(void);

void GenerateSkyGradientTpage(void);    /* FUN_0046FB30 */
void GenerateSkyGradientD3D(void);      /* FUN_0046FF18 */
void BuildGridLODTex(void);             /* sdl/src/playfield_render_d3d.c — DC LOD texture */

void UpdatePalette(void);

/* g_modelMetaTable[] replaced by g_modelMeta[] (ModelMeta struct) in sonicr_globals.h */
void S3D_LoadAndScaleParallax(const char *filename, int destWidth, int destHeight);
void LoadLightingTable(const char *filename, int start, int count);

/* Translated functions defined later in this file */
void LoadCharacterGouraudTables(void);  /* FUN_00476750 */
void LoadParallaxIsland(void);          /* FUN_00473148 */
void LoadParallaxCity(void);            /* FUN_004737E0 */
void LoadParallaxRuin(void);            /* FUN_00473EE8 */
void LoadParallaxFactory(void);         /* FUN_004745D0 */
void SetWeatherTint(void);            /* FUN_004E0758 — sets g_weatherR/G/B from conditions */

extern void InitTrackObjectModes(void);  /* 0x0047256C — sets mode 2 for rotatable decorations */
extern void RemapBalloonTpages(void);    /* 0x00470564 — balloon faces -> g_tpageCharacters */

static const char *s_trackBinPaths[] = {
    NULL,
    PATH_ISLAND_BIN,
    PATH_CITY_BIN,
    PATH_RUIN_BIN,
    PATH_FACTORY_BIN,
    PATH_EMERALD_BIN,
};

/* =====================================================================
 * LoadResource64K — 0x0042d85c — 54 bytes
 * Load 64KB binary resource from file into shared buffer at 0x68B2C0.
 * Used for both sound data and tile map data (different callers, same dest).
 * EAX = filename (from caller).
 * Original: fopen(EAX, "rb"), fread(0x68b2c0, 0x10000, 1, fp), fclose
 * ===================================================================== */
void LoadResource64K(const char *filename)  /* EAX */
{
    FILE *fp = fOpen(filename, "rb");
    if (fp != NULL) {
        fRead((char*)g_tileMap, 0x10000, 1, fp);
        fClose(fp);
    }
}

static void LoadTrackGeometry(void)
{
    if (g_trackId == TRACK_NONE) {
        g_nextLoadFilename = PATH_TITLES3_BIN;
    }
    else if (g_trackId >= TRACK_RESORT_ISLAND && g_trackId <= TRACK_RADIANT_EMERALD) {
        g_nextLoadFilename = s_trackBinPaths[g_trackId];
    }
    /* Port fix (as the PSP port): the binary opens SONICR.BIT, then the track's
     * BIN over the same handle, skips a byte of it and never closes either;
     * LoadTrack3 opens the BIN again for itself. Two handles leaked per race. */
    OpenTrackFileA();
    if (g_fileHandle != NULL) {
        fClose(g_fileHandle);
        g_fileHandle = NULL;
    }
    OpenTrackFileB();
    SkipTrackHeader();
    if (g_fileHandle != NULL) {
        fClose(g_fileHandle);
        g_fileHandle = NULL;
    }
}

static void LoadTrackData(void)
{
    LoadTrack3();
    LoadTerrain();
    LoadAI();
    LoadCharacterGouraudTables();
    /* Binary per-track inits (Island/City/Ruin/Factory) tint with g_weatherR/G/B.
     * Radiant Emerald (0x474ce8) uses (0,0,0). Both tint functions ADD to existing
     * values, so the weather color matters. */
    TintCharacterGouraudTables(g_weatherR, g_weatherG, g_weatherB);
    TintTrackGourauds(g_weatherR, g_weatherG, g_weatherB);
}

/* =====================================================================
 * LoadLightingTable — 0x0042e834 — 211 bytes
 * Opens a file (filename from EAX param), reads per-vertex RGB lighting:
 * 3 separate fread calls per entry (R, G, B as single bytes), converts
 * each to fixed-point (byte << 13 + 0x1000), stores as 3 ints.
 * Output to 0x7bc4a8 + (info[+0x24] * 0x180), count = info[+0x28] << 5.
 * EAX = filename string, EDX = info struct pointer
 * 
 * modified to not be so wasteful with input arguments
 * 
 * ===================================================================== */
void LoadLightingTable(const char *filename, int start, int count)
{
    FILE *fp = fOpen(filename, "rb");
    g_fileHandle = fp;
    if (fp == NULL) {
        goto done;
    }

    int tableIdx = start;           /* [+0x24] */
    int entryCount = count << 5;   /* [+0x28] << 5 */
    int *dst = g_charLightingTable + tableIdx * 0x60;  /* stride 0x180 bytes = 0x60 ints */

#ifdef SONICR_DC
    unsigned char *raw = (unsigned char *)malloc((size_t)entryCount * 3);
    if (raw != NULL) {
        size_t got = fRead(raw, 1, (size_t)entryCount * 3, fp);
        int entriesGot = (int)(got / 3);
        for (int i = 0; i < entriesGot; i++) {
            dst[0] = ((int)raw[i*3+0] << 13) + 0x1000;
            dst[1] = ((int)raw[i*3+1] << 13) + 0x1000;
            dst[2] = ((int)raw[i*3+2] << 13) + 0x1000;
            dst += 3;
        }
        free(raw);
    }
    else {
#endif
    for (int i = 0; i < entryCount; i++) {
        unsigned char r, g, b;
        fRead(&r, 1, 1, fp);
        fRead(&g, 1, 1, fp);
        fRead(&b, 1, 1, fp);
        dst[0] = ((int)r << 13) + 0x1000;
        dst[1] = ((int)g << 13) + 0x1000;
        dst[2] = ((int)b << 13) + 0x1000;
        dst += 3;
    }
#ifdef SONICR_DC
    }
#endif
done:
    fp = g_fileHandle;
    if (fp) {
        fClose(fp);
    }
}

/* =====================================================================
 * InitIsland — 0x004732BC — Resort Island (Track 1)
 *
 * 13 LoadTPageRGB calls (tpages 0-12).
 * Tpages 0-3, 5-6 have snow variants when g_weatherType==WEATHER_SNOW.
 * Verified from disassembly 0x473560-0x473670.
 * ===================================================================== */
void InitIsland(void)
{
    DebugLog("InitIsland\n");

    /* Texture page assignments */
    g_tpagePlayfield1 = 9;
    g_tpagePlayfield2 = 8;
    g_tpageObjects = 10;
    g_tpageCharacters = 4;
    g_tpageParallax1 = 11;
    g_tpageParallax2 = 11;
    GL_KeepPixels(g_tpageParallax2);
    g_tpageCharBase = 7;
    g_tpageCount = 13;
    g_tpageUIAlt = g_tpageGroupShift + 13; /* binary: [0x8f6c50]+0xD */
    g_uiTexPage = 13;
    g_tpageUIAlt2 = g_tpageGroupShift + 15; /* binary: [0x8f6c50]+0xF */
    g_tpageExtra = 5;

    /* D3D texture loading — 13 calls verified from disassembly */
    SetupD3DTexturesBegin();
    LoadTPageRGB(0, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_04.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND04.RAW");
    LoadTPageRGB(1, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_01.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND01.RAW");
    LoadTPageRGB(2, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_02.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND02.RAW");
    LoadTPageRGB(3, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_00.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND00.RAW");
    LoadTPageRGB(4, DATA_DIR SEP "GENERAL" SEP "PLAYER00.RAW");
    LoadTPageRGB(5, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_03.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND03.RAW");
    LoadTPageRGB(6, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_05.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND05.RAW");
    LoadTPageRGB(7, DATA_DIR SEP "GENERAL" SEP "MISC00.RAW");
    LoadTPageRGB(8, DATA_DIR SEP "GENERAL" SEP "MISC01.RAW");
    LoadTPageRGB(9, DATA_DIR SEP "GENERAL" SEP "PLAYER01.RAW");
    LoadTPageRGB(10, DATA_DIR SEP "GENERAL" SEP "ICON00.RAW");
    LoadTPageRGB(11, DATA_DIR SEP "GENERAL" SEP "ICON01.RAW");
    LoadTPageRGB(12, DATA_DIR SEP "ISLAND" SEP "ISLAND06.RAW");
    LoadPlopSprites();                                           /* 0x47369C: plop2/flake2 → parallax tpage (224,0) */
    GenerateSkyGradientTpage();
    LoadParallaxIsland();
    /* GenerateSkyGradientD3D(); */
    /* 0x47371D: mov eax, "island\island.ply"; call 0x42d418 */
    /* 0x47371D: mov eax, "island\island.ply"; call 0x42d418 */
    D3D_LoadPlayfieldTilesRGB((g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISLAND_S.PLY"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND.PLY");
    ProcessTpageStates();
    FinalizeD3DTextures();

    /* Freeze ICON01 into GL — HUD sprites (timers, faces) live at rows 128+.
     * Load MAP into (128,0) — empty in ICON01 — and patch just that region.
     * This matches the binary's minimap UV (0x80, 0) without clobbering HUD data. */
    R_FreezeTexture(g_tpageParallax1);

    const char *weatherFile = (g_weatherType == WEATHER_SNOW) ? PATH_FLAKE_RAW : PATH_PLOP_RAW;
    LoadTextureSubRect(weatherFile, g_tpageParallax1, 256, 28, 0, 224);
    LoadTextureSubRect(PATH_MAP_ISLAND, g_tpageParallax1, 96, 80, 128, 0);
    LoadTextureSubRect(PATH_EMAP_NO1, g_tpageParallax1, 128, 128, 0, 0);  /* 0x473513 */

    R_UploadTextureSubRect(g_tpageParallax1, 128, 0, 96, 80);
    R_UploadTextureSubRect(g_tpageParallax1, 0, 0, 128, 128);
    R_ClearTextureDirty(g_tpageParallax1);

    LoadTrackGeometry();

    /* Load tile map — 0x47373B: mov eax, "island\island.map"; call 0x42d85c */
    LoadResource64K(DATA_DIR SEP "ISLAND" SEP "ISLAND.MAP");

    /* Resort Island stray-tile fixes: 8 mis-set tiles in the shipped ISLAND.MAP.
     * Verified as a byte diff against a corrected map. Applied at load time so
     * both weather modes and (on DC) the grid LOD texture pick them up.
     * Index = row*256 + col into g_tileMap. */
    static const struct { unsigned short idx; unsigned char val; } s_islandMapFix[] = {
        { 0x2328, 0x03 }, { 0x2428, 0x05 }, { 0x2528, 0x03 }, { 0x2628, 0x05 },
        { 0x3614, 0x34 }, { 0x3714, 0x34 }, { 0x3814, 0x34 }, { 0x4A18, 0x34 },
    };
    for (int mf = 0; mf < (int)(sizeof(s_islandMapFix) / sizeof(s_islandMapFix[0])); mf++) {
        g_tileMap[s_islandMapFix[mf].idx] = s_islandMapFix[mf].val;
    }

    if (g_weatherType == WEATHER_SNOW) {
        /* Snow: override one tile in the just-loaded map.
         * 0x473749: mov byte [0x68e40f], 0x55 → g_tileMap[0x68E40F - 0x68B2C0]
         * = g_tileMap[0x314F] (grid col 79, row 49). Done before BuildGridLODTex
         * (our DC LOD consumer) so the LOD texture reflects the patched tile. */
        g_tileMap[0x314F] = 0x55;
    }

#ifdef SONICR_DC
//    BuildGridLODTex();
#endif

    g_worldBoundsA = 25.0f;
    g_worldBoundsB = -476.802490234375f;
    g_worldBoundsC = 543.5864868164062f;
    g_worldBoundsD = 631.408447265625f;
    g_worldBoundsE = -564.6244506835938f;

    RemapBalloonTpages();                       /* 0x470564 */
    LoadTrackData();
    InitTrackObjectModes();                      /* 0x473790 */
}

/* =====================================================================
 * InitCity — 0x00473954 — Radical City (Track 2)
 *
 * 15 LoadTPageRGB calls (tpages 0-14).
 * Verified from disassembly 0x473C30-0x473DA0.
 * ===================================================================== */
void InitCity(void)
{
    DebugLog("InitCity\n");

    g_tpagePlayfield1 = 10;
    g_tpagePlayfield2 = 12;
    g_tpageObjects = 13;
    g_tpageCharacters = 5;
    g_tpageParallax1 = 14;
    g_tpageParallax2 = 14;
    GL_KeepPixels(g_tpageParallax2);
    g_tpageCharBase = 11;                    /* binary: ecx=0xb at 0x473977 */
    g_tpageCount = 15;                       /* binary: ebx=0xf at 0x4739d3 (== g_uiTexPage) */
    g_tpageUIAlt = g_tpageGroupShift + 15; /* binary: [0x8f6c50]+0xF */
    g_uiTexPage = 15;                        /* binary: ebx=0xf at 0x4739c3 */
    g_tpageUIAlt2 = g_tpageGroupShift + 17; /* binary: [0x8f6c50]+0x11 */
    g_tpageExtra = 4;                        /* binary: ecx=4 at 0x4739bc */

    SetupD3DTexturesBegin();
    LoadTPageRGB(0, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_06.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY06.RAW");
    LoadTPageRGB(1, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_00.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY00.RAW");
    LoadTPageRGB(2, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_01.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY01.RAW");
    LoadTPageRGB(3, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_02.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY02.RAW");
    LoadTPageRGB(4, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_03.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND03.RAW");
    LoadTPageRGB(5, DATA_DIR SEP "GENERAL" SEP "PLAYER00.RAW");
    LoadTPageRGB(6, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_05.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND05.RAW");
    LoadTPageRGB(7, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_03.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY03.RAW");
    LoadTPageRGB(8, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_06.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY06.RAW");
    LoadTPageRGB(9, DATA_DIR SEP "ISLAND" SEP "ISLAND02.RAW");
    LoadTPageRGB(10, DATA_DIR SEP "GENERAL" SEP "PLAYER01.RAW");
    LoadTPageRGB(11, DATA_DIR SEP "GENERAL" SEP "MISC00.RAW");
    LoadTPageRGB(12, DATA_DIR SEP "GENERAL" SEP "MISC01.RAW");
    LoadTPageRGB(13, DATA_DIR SEP "GENERAL" SEP "ICON00.RAW");
    LoadTPageRGB(14, DATA_DIR SEP "GENERAL" SEP "ICON01.RAW");
    LoadPlopSprites();                                           /* plop2/flake2 → parallax tpage (224,0) */
    GenerateSkyGradientTpage();

    LoadParallaxCity();                                          /* FUN_004737E0 */
    D3D_LoadPlayfieldTilesRGB((g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CITY_S.PLY"
                                                    : DATA_DIR SEP "CITY" SEP "CITY.PLY");
    ProcessTpageStates();
    FinalizeD3DTextures();

    R_FreezeTexture(g_tpageParallax1);

    const char *weatherFile = (g_weatherType == WEATHER_SNOW) ? PATH_FLAKE_RAW : PATH_PLOP_RAW;
    LoadTextureSubRect(weatherFile, g_tpageParallax1, 256, 28, 0, 224);
    LoadTextureSubRect(PATH_MAP_CITY, g_tpageParallax1, 96, 80, 128, 0);
    LoadTextureSubRect(PATH_EMAP_NO2, g_tpageParallax1, 128, 128, 0, 0);  /* 0x473BE9 */

    R_UploadTextureSubRect(g_tpageParallax1, 128, 0, 96, 80);
    R_UploadTextureSubRect(g_tpageParallax1, 0, 0, 128, 128);
    R_ClearTextureDirty(g_tpageParallax1);

    LoadTrackGeometry();

    /* Load tile map — 0x473E69: mov eax, "city\city.map"; call 0x42d85c */
    LoadResource64K(DATA_DIR SEP "CITY" SEP "CITY.MAP");

#ifdef SONICR_DC
//    BuildGridLODTex();
#endif

    g_worldBoundsA = 25.0f;
    g_worldBoundsB = -476.802490234375f;
    g_worldBoundsC = 543.488525390625f;
    g_worldBoundsD = 631.408447265625f;
    g_worldBoundsE = -564.722412109375f;

    RemapBalloonTpages();                       /* 0x470564 */
    LoadTrackData();
    InitTrackObjectModes();
}

/* =====================================================================
 * InitRuin — 0x0047405C — Regal Ruin (Track 4)
 *
 * 15 LoadTPageRGB calls (tpages 0-14).
 * Verified from disassembly 0x474320-0x474490.
 * ===================================================================== */
void InitRuin(void)
{
    DebugLog("InitRuin\n");

    g_tpagePlayfield1 = 12;
    g_tpagePlayfield2 = 11;
    g_tpageObjects = 13;
    g_tpageCharacters = 5;
    g_tpageParallax1 = 14;
    g_tpageParallax2 = 14;
    GL_KeepPixels(g_tpageParallax2);
    g_tpageCharBase = 10;                    /* binary: ecx=0xa at 0x47407f */
    g_uiTexPage = 15;                        /* binary: ebx=0xf at 0x4740cb */
    g_tpageExtra = 9;                        /* binary: ecx=9 at 0x4740c4 */
    g_tpageCount = 15;                       /* binary: ebx=0xf at 0x4740db (== g_uiTexPage) */
    g_tpageUIAlt = g_tpageGroupShift + 15; /* binary: [0x8f6c50]+0xF */
    g_tpageUIAlt2 = g_tpageGroupShift + 17; /* binary: [0x8f6c50]+0x11 */

    SetupD3DTexturesBegin();
    LoadTPageRGB(0, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "RUIN" SEP "SNOW" SEP "RUI_S_00.RAW"
                                                    : DATA_DIR SEP "RUIN" SEP "RUIN00.RAW");
    LoadTPageRGB(1, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "RUIN" SEP "SNOW" SEP "RUI_S_01.RAW"
                                                    : DATA_DIR SEP "RUIN" SEP "RUIN01.RAW");
    LoadTPageRGB(2, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "RUIN" SEP "SNOW" SEP "RUI_S_02.RAW"
                                                    : DATA_DIR SEP "RUIN" SEP "RUIN02.RAW");
    LoadTPageRGB(3, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "RUIN" SEP "SNOW" SEP "RUI_S_03.RAW"
                                                    : DATA_DIR SEP "RUIN" SEP "RUIN03.RAW");
    LoadTPageRGB(4, DATA_DIR SEP "RUIN" SEP "RUIN04.RAW");
    LoadTPageRGB(5, DATA_DIR SEP "GENERAL" SEP "PLAYER00.RAW");
    LoadTPageRGB(6, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_05.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND05.RAW");
    LoadTPageRGB(7, DATA_DIR SEP "ISLAND" SEP "ISLAND02.RAW");
    LoadTPageRGB(8, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_03.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY03.RAW");
    LoadTPageRGB(9, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_03.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND03.RAW");
    LoadTPageRGB(10, DATA_DIR SEP "GENERAL" SEP "MISC00.RAW");
    LoadTPageRGB(11, DATA_DIR SEP "GENERAL" SEP "MISC01.RAW");
    LoadTPageRGB(12, DATA_DIR SEP "GENERAL" SEP "PLAYER01.RAW");
    LoadTPageRGB(13, DATA_DIR SEP "GENERAL" SEP "ICON00.RAW");
    LoadTPageRGB(14, DATA_DIR SEP "GENERAL" SEP "ICON01.RAW");
    LoadPlopSprites();                                           /* plop2/flake2 → parallax tpage (224,0) */
    GenerateSkyGradientTpage();

    LoadParallaxRuin();                                          /* FUN_00473EE8 */
    D3D_LoadPlayfieldTilesRGB((g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "RUIN" SEP "SNOW" SEP "RUIN_S.PLY"
                                                    : DATA_DIR SEP "RUIN" SEP "RUIN.PLY");
    ProcessTpageStates();
    FinalizeD3DTextures();

    R_FreezeTexture(g_tpageParallax1);

    const char *weatherFile = (g_weatherType == WEATHER_SNOW) ? PATH_FLAKE_RAW : PATH_PLOP_RAW;
    LoadTextureSubRect(weatherFile, g_tpageParallax1, 256, 28, 0, 224);
    LoadTextureSubRect(PATH_MAP_RUIN, g_tpageParallax1, 96, 80, 128, 0);
    LoadTextureSubRect(PATH_EMAP_NO3, g_tpageParallax1, 128, 128, 0, 0);  /* 0x4742E1 */

    R_UploadTextureSubRect(g_tpageParallax1, 128, 0, 96, 80);
    R_UploadTextureSubRect(g_tpageParallax1, 0, 0, 128, 128);
    R_ClearTextureDirty(g_tpageParallax1);

    LoadTrackGeometry();

    /* Load tile map — 0x47454C: mov eax, "ruin\ruin.map"; call 0x42d85c */
    LoadResource64K(DATA_DIR SEP "RUIN" SEP "RUIN.MAP");

#ifdef SONICR_DC
//    BuildGridLODTex();
#endif

    g_worldBoundsA = 30.0f;
    g_worldBoundsB = -366.7467956542969f;
    g_worldBoundsC = 279.8381042480469f;
    g_worldBoundsD = 192.3921661376953f;
    g_worldBoundsE = -279.3009033203125f;

    RemapBalloonTpages();                       /* 0x470564 */
    LoadTrackData();
    InitTrackObjectModes();
}

/* =====================================================================
 * InitFactory — 0x00474744 — Reactive Factory (Track 3)
 *
 * 15 LoadTPageRGB calls (tpages 0-14).
 * Verified from disassembly 0x474A10-0x474B70.
 * ===================================================================== */
void InitFactory(void)
{
    DebugLog("InitFactory\n");

    g_tpagePlayfield1 = 11;
    g_tpagePlayfield2 = 10;
    g_tpageObjects = 12;
    g_tpageCharacters = 4;
    g_tpageParallax1 = 13;
    g_tpageParallax2 = 13;
    GL_KeepPixels(g_tpageParallax2);
    g_tpageCharBase = 9;                     /* binary: ecx=9 at 0x474767 */
    g_uiTexPage = 15;                        /* binary: ebx=0xf at 0x4747b3 */
    g_tpageExtra = 7;                        /* binary: ecx=7 at 0x4747ac */
    g_tpageCount = 15;                       /* binary: ebx=0xf at 0x4747c3 (== g_uiTexPage) */
    g_tpageUIAlt = g_tpageGroupShift + 15; /* binary: [0x8f6c50]+0xF */
    g_tpageUIAlt2 = g_tpageGroupShift + 17; /* binary: [0x8f6c50]+0x11 */

    SetupD3DTexturesBegin();
    LoadTPageRGB(0, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "FACTORY" SEP "SNOW" SEP "FAC_S_00.RAW"
                                                    : DATA_DIR SEP "FACTORY" SEP "FACT00.RAW");
    LoadTPageRGB(1, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "FACTORY" SEP "SNOW" SEP "FAC_S_01.RAW"
                                                    : DATA_DIR SEP "FACTORY" SEP "FACT01.RAW");
    LoadTPageRGB(2, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "FACTORY" SEP "SNOW" SEP "FAC_S_02.RAW"
                                                    : DATA_DIR SEP "FACTORY" SEP "FACT02.RAW");
    LoadTPageRGB(3, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "FACTORY" SEP "SNOW" SEP "FAC_S_03.RAW"
                                                    : DATA_DIR SEP "FACTORY" SEP "FACT03.RAW");
    LoadTPageRGB(4, DATA_DIR SEP "GENERAL" SEP "PLAYER00.RAW");
    LoadTPageRGB(5, DATA_DIR SEP "ISLAND" SEP "ISLAND02.RAW");
    LoadTPageRGB(6, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "CITY" SEP "SNOW" SEP "CIT_S_03.RAW"
                                                    : DATA_DIR SEP "CITY" SEP "CITY03.RAW");
    LoadTPageRGB(7, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_03.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND03.RAW");
    LoadTPageRGB(8, (g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "ISLAND" SEP "SNOW" SEP "ISL_S_05.RAW"
                                                    : DATA_DIR SEP "ISLAND" SEP "ISLAND05.RAW");
    LoadTPageRGB(9, DATA_DIR SEP "GENERAL" SEP "MISC00.RAW");
    LoadTPageRGB(10, DATA_DIR SEP "GENERAL" SEP "MISC01.RAW");
    LoadTPageRGB(11, DATA_DIR SEP "GENERAL" SEP "PLAYER01.RAW");
    LoadTPageRGB(12, DATA_DIR SEP "GENERAL" SEP "ICON00.RAW");
    LoadTPageRGB(13, DATA_DIR SEP "GENERAL" SEP "ICON01.RAW");
    LoadTPageRGB(14, DATA_DIR SEP "FACTORY" SEP "FACT04.RAW");
    LoadPlopSprites();                                           /* plop2/flake2 → parallax tpage (224,0) */
    GenerateSkyGradientTpage();

    LoadParallaxFactory();                                       /* FUN_004745D0 */
    D3D_LoadPlayfieldTilesRGB((g_weatherType == WEATHER_SNOW) ? DATA_DIR SEP "FACTORY" SEP "SNOW" SEP "FACTRY_S.PLY"
                                                    : DATA_DIR SEP "FACTORY" SEP "FACTORY.PLY");
    ProcessTpageStates();
    FinalizeD3DTextures();

    R_FreezeTexture(g_tpageParallax1);

    const char *weatherFile = (g_weatherType == WEATHER_SNOW) ? PATH_FLAKE_RAW : PATH_PLOP_RAW;
    LoadTextureSubRect(weatherFile, g_tpageParallax1, 256, 28, 0, 224);
    LoadTextureSubRect(PATH_MAP_FACTORY, g_tpageParallax1, 96, 80, 128, 0);
    LoadTextureSubRect(PATH_EMAP_NO4, g_tpageParallax1, 128, 128, 0, 0);  /* 0x4749C9 */

    R_UploadTextureSubRect(g_tpageParallax1, 128, 0, 96, 80);
    R_UploadTextureSubRect(g_tpageParallax1, 0, 0, 128, 128);
    R_ClearTextureDirty(g_tpageParallax1);

    LoadTrackGeometry();

    /* Load tile map — 0x474C34: mov eax, "factory\factory.map"; call 0x42d85c */
    LoadResource64K(DATA_DIR SEP "FACTORY" SEP "FACTORY.MAP");

#ifdef SONICR_DC
//    BuildGridLODTex();
#endif

    g_worldBoundsA = 25.0f;
    g_worldBoundsB = -450.8096008300781f;
    g_worldBoundsC = 424.0888977050781f;
    g_worldBoundsD = 445.75390625f;
    g_worldBoundsE = -472.4747009277344f;

    RemapBalloonTpages();                       /* 0x470564 */
    LoadTrackData();
    InitTrackObjectModes();
}

/* =====================================================================
 * InitEmerald — 0x00474CE8 — Radiant Emerald (Track 5)
 *
 * 13 LoadTPageRGB calls (tpages 0-12).
 * Verified from disassembly 0x474ED0-0x474F90.
 * ===================================================================== */
void InitEmerald(void)
{
    DebugLog("InitEmerald\n");

    g_tpagePlayfield1 = 10;
    g_tpagePlayfield2 = 8;
    g_tpageObjects = 11;
    g_tpageCharacters = 9;
    g_tpageCharBase = 7;
    g_tpageParallax1 = 12;
    g_tpageParallax2 = 12;
    GL_KeepPixels(g_tpageParallax2);
    g_uiTexPage = 13;                        /* binary: ebx=0xd at 0x474d55 */
    g_tpageExtra = 6;                        /* binary: ecx=6 at 0x474d4b */
    g_tpageCount = 13;                       /* binary: ebx=0xd at 0x474d5b (== g_uiTexPage) */
    g_tpageUIAlt = g_tpageGroupShift + 13; /* binary: [0x8f6c50]+0xD */
    g_tpageUIAlt2 = g_tpageGroupShift + 13; /* binary: same value for both */

    SetupD3DTexturesBegin();
    LoadTPageRGB(0, DATA_DIR SEP "EMERALD" SEP "CAS00.RAW");
    LoadTPageRGB(1, DATA_DIR SEP "EMERALD" SEP "CAS01.RAW");
    LoadTPageRGB(2, DATA_DIR SEP "ISLAND" SEP "ISLAND05.RAW");
    LoadTPageRGB(3, DATA_DIR SEP "EMERALD" SEP "CAS02.RAW");
    LoadTPageRGB(4, DATA_DIR SEP "ISLAND" SEP "ISLAND02.RAW");
    LoadTPageRGB(5, DATA_DIR SEP "CITY" SEP "CITY03.RAW");
    LoadTPageRGB(6, DATA_DIR SEP "ISLAND" SEP "ISLAND03.RAW");
    LoadTPageRGB(7, DATA_DIR SEP "GENERAL" SEP "MISC00.RAW");
    LoadTPageRGB(8, DATA_DIR SEP "GENERAL" SEP "MISC01.RAW");
    LoadTPageRGB(9, DATA_DIR SEP "GENERAL" SEP "PLAYER00.RAW");
    LoadTPageRGB(10, DATA_DIR SEP "GENERAL" SEP "PLAYER01.RAW");
    LoadTPageRGB(11, DATA_DIR SEP "GENERAL" SEP "ICON00.RAW");
    LoadTPageRGB(12, DATA_DIR SEP "GENERAL" SEP "ICON01.RAW");
    GenerateSkyGradientTpage();
    /* No D3D_LoadPlayfieldTilesRGB call — binary doesn't call it for Emerald */
    ProcessTpageStates();
    FinalizeD3DTextures();

    /* Binary D3D path 0x474FD8: loads parallax panorama (moon/stars background).
     * Software path 0x474EBB calls S3D_LoadAndScaleParallax with same file. */
    S3D_LoadAndScaleParallax(PATH_PAR_EMERALD "_E.RAW",
                             g_parallaxWidth, g_parallaxExtraX);
    SetWeatherTint();  /* binary 0x474ec0: call 0x4E0758 */

    R_FreezeTexture(g_tpageParallax1);
    /* Sub-rect textures packed into g_tpageParallax1 (tpage 12).
     * No ICON01 sub-rect here — binary doesn't load it (0x474FAC only has MAP+EMAP).
     * The full ICON01 is already loaded via LoadTPageRGB above (line 479). */
    LoadTextureSubRect(PATH_MAP_EMERALD, g_tpageParallax1, 96, 80, 128, 0);
    LoadTextureSubRect(PATH_EMAP_NO5, g_tpageParallax1, 128, 128, 0, 0);  /* 0x474E8B */

    R_UploadTextureSubRect(g_tpageParallax1, 128, 0, 96, 80);
    R_UploadTextureSubRect(g_tpageParallax1, 0, 0, 128, 128);
    R_ClearTextureDirty(g_tpageParallax1);

    LoadTrackGeometry();

    RemapBalloonTpages();                       /* 0x470564 */
    LoadTrackData();
    InitTrackObjectModes();
}

/* =====================================================================
 * LoadCharacterGouraudTables — 0x00476750 — 168 bytes
 * Loads per-vertex RGB gouraud lighting data for all 10 character models.
 * Each .GRD file is read by LoadLightingTable, which uses the model's
 * vertexStart (metaTable[+0x00]) as the lighting table index and
 * vertexCount (metaTable[+0x04]) as the entry count.
 *
 * Binary passes model struct base at 0x713080 + charId*0x50; the
 * LoadLightingTable info pointer is 0x24 bytes before g_modelMeta[].
 * ===================================================================== */
void LoadCharacterGouraudTables(void)  /* 0x00476750 */
{
    DebugLog("LoadCharacterGouraudTables\n");

    for (int i = 0; i < 10; i++) {
//        int info[11] = {0};
//        info[9]  = g_modelMeta[i].vertexStart;      /* vertexStart — table index */
//        info[10] = g_modelMeta[i].vertexCount;     /* vertexCount — entry count */
        LoadLightingTable(grdPaths[i], g_modelMeta[i].vertexStart, g_modelMeta[i].vertexCount);
    }
}

/* =====================================================================
 * Per-track parallax loaders — select parallax RAW file based on
 * g_timeOfDay (sunrise/day/sunset/night) and g_weatherType (clear/rain/snow),
 * then call S3D_LoadAndScaleParallax to load and scale it.
 *
 * Suffixes: (none)=1P, _n=3+P narrow, _dd=2P/4P double
 *           _r=rain, _s=snow, (none)=clear
 * ===================================================================== */

/* Helper: pick parallax path from 9 variants based on playerMode × weatherType */
static const char *PickParallaxPath(
    const char *p1_rain, const char *p1_snow, const char *p1_clear,
    const char *p3_rain, const char *p3_snow, const char *p3_clear,
    const char *px_rain, const char *px_snow, const char *px_clear)
{
    if (g_timeOfDay == TOD_DAY) {
        if (g_weatherType == WEATHER_RAIN) {
            return p1_rain;
        }
        if (g_weatherType == WEATHER_SNOW) {
            return p1_snow;
        }
        return p1_clear;
    }

    if (g_timeOfDay == TOD_NIGHT) {
        if (g_weatherType == WEATHER_RAIN) {
            return p3_rain;
        }
        if (g_weatherType == WEATHER_SNOW) {
            return p3_snow;
        }

        return p3_clear;
    }

    if (g_weatherType == WEATHER_RAIN) {
        return px_rain;
    }
    if (g_weatherType == WEATHER_SNOW) {
        return px_snow;
    }

    return px_clear;
}

/* LoadParallaxIsland — 0x00473148 — 159 bytes */
void LoadParallaxIsland(void)
{
    const char *path = PickParallaxPath(
        PATH_PAR_ISLAND "_I_R.RAW",    PATH_PAR_ISLAND "_I_S.RAW",    PATH_PAR_ISLAND "_I.RAW",
        PATH_PAR_ISLAND "_I_R_N.RAW",  PATH_PAR_ISLAND "_I_S_N.RAW",  PATH_PAR_ISLAND "_I_N.RAW",
        PATH_PAR_ISLAND "_I_R_DD.RAW", PATH_PAR_ISLAND "_I_S_DD.RAW", PATH_PAR_ISLAND "_I_DD.RAW");
    S3D_LoadAndScaleParallax(path, g_parallaxWidth, g_parallaxExtraX);
    SetWeatherTint();  /* binary 0x4731dd: call 0x4E0758 */
}

/* LoadParallaxCity — 0x004737E0 — 159 bytes */
void LoadParallaxCity(void)
{
    const char *path = PickParallaxPath(
        PATH_PAR_CITY "_C_R.RAW",    PATH_PAR_CITY "_C_S.RAW",    PATH_PAR_CITY "_C.RAW",
        PATH_PAR_CITY "_C_R_N.RAW",  PATH_PAR_CITY "_C_S_N.RAW",  PATH_PAR_CITY "_C_N.RAW",
        PATH_PAR_CITY "_C_R_DD.RAW", PATH_PAR_CITY "_C_S_DD.RAW", PATH_PAR_CITY "_C_DD.RAW");
    S3D_LoadAndScaleParallax(path, g_parallaxWidth, g_parallaxExtraX);
    SetWeatherTint();  /* binary 0x473875: call 0x4E0758 */
}

/* LoadParallaxRuin — 0x00473EE8 — 159 bytes */
void LoadParallaxRuin(void)
{
    const char *path = PickParallaxPath(
        PATH_PAR_RUIN "_R_R.RAW",    PATH_PAR_RUIN "_R_S.RAW",    PATH_PAR_RUIN "_R.RAW",
        PATH_PAR_RUIN "_R_R_N.RAW",  PATH_PAR_RUIN "_R_S_N.RAW",  PATH_PAR_RUIN "_R_N.RAW",
        PATH_PAR_RUIN "_R_R_DD.RAW", PATH_PAR_RUIN "_R_S_DD.RAW", PATH_PAR_RUIN "_R_DD.RAW");
    S3D_LoadAndScaleParallax(path, g_parallaxWidth, g_parallaxExtraX);
    SetWeatherTint();  /* binary 0x00473F7D: call 0x4E0758 */
}

/* LoadParallaxFactory — 0x004745D0 — 159 bytes */
void LoadParallaxFactory(void)
{
    const char *path = PickParallaxPath(
        PATH_PAR_FACTORY "_F_R.RAW",    PATH_PAR_FACTORY "_F_S.RAW",    PATH_PAR_FACTORY "_F.RAW",
        PATH_PAR_FACTORY "_F_R_N.RAW",  PATH_PAR_FACTORY "_F_S_N.RAW",  PATH_PAR_FACTORY "_F_N.RAW",
        PATH_PAR_FACTORY "_F_R_DD.RAW", PATH_PAR_FACTORY "_F_S_DD.RAW", PATH_PAR_FACTORY "_F_DD.RAW");
    S3D_LoadAndScaleParallax(path, g_parallaxWidth, g_parallaxExtraX);
    SetWeatherTint();  /* binary 0x474665: call 0x4E0758 */
}
