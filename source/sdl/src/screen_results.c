/**
 * screen_results.c — Results screen rendering (D3D/DrawTexturedQuad path)
 *
 * FUN_00489d18 — 2105 bytes — Results/Standings screen renderer
 * FUN_0048999c — 891 bytes — Portrait standings renderer
 * FUN_0048987c — 287 bytes — Button prompt renderer
 *
 * Translated from the DrawTexturedQuad path (renderMode != 2) of the binary.
 * Binary addresses: 0x48a161-0x48a550, 0x489b88-0x489d12, 0x48992f-0x48999a.
 *
 * DrawTexturedQuad (0x450c38) signature:
 *   EAX=xPos, EDX=yPos
 *   Stack: [depth, width, height, tpage, uvX, uvY, uvW, uvH, color]
 *
 * DrawGlyphString (0x4897dc) signature:
 *   EAX=x, EDX=y, EBX=glyphIds, ECX=maxCount — returns updated X in EAX
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "net_transport.h"
#include "sonicr_functions.h"

/* ROM glyph data tables — font sprite UV coordinates and widths */

/**
 * DrawGlyphString — FUN_004897dc — 157 bytes — VALIDATED
 *
 * Draws a sequence of glyphs from a font sprite sheet.
 * Iterates a glyph index array, looks up UV coordinates from ROM tables,
 * and calls DrawTexturedQuad for each glyph.
 *
 * EAX = starting screen X, EDX = screen Y
 * EBX = pointer to int array of glyph indices (-1 terminated)
 * ECX = max glyph count
 * Returns final X position in EAX.
 */
int DrawGlyphString(int startX, int y, int *glyphIds, int maxCount)
{
    int x = startX;

    if (maxCount <= 0) {
        goto done;
    }

    for (int i = 0; i < maxCount; i++) {
        int id = glyphIds[i];
        if (id == -1) {
            goto done;                                     /* sentinel */
        }

        int uvX, uvY, uvW;
        int lowId = id & 0xFFFF;

        if ((id & 0xFFFF0000) != 0) {
            /* High word set: look up in char map table (12-byte stride) */
            int *mapEntry = (int *)((char *)g_romCharMap + lowId * 12);
            int mapped = mapEntry[0];
            if (mapped == -1) {
                continue;                              /* skip unmapped */
            }
            uvX = mapped;
            uvY = mapEntry[1];
            uvW = mapEntry[2];
        }
        else {
            /* Low word only: look up in glyph table (16-byte stride) */
            int *glyphEntry = (int *)((char *)g_romGlyphTable + lowId * 16);
            uvX = glyphEntry[0];
            uvY = glyphEntry[1];
            uvW = glyphEntry[2];
        }

        int width = uvW * 2;
        DrawTexturedQuad(x, y,
                         0x40000000,               /* depth = float 2.0 */
                         width, 0x14,               /* width, height=20 */
                         g_uiTexPage + 1,           /* tpage */
                         uvX, uvY, uvW, 0xA,        /* UV rect (uvH=10) */
                         VERTEX_WHITE);               /* color */
        x += width;
    }

done:
    return x;
}

/* =====================================================================
 * External declarations
 * ===================================================================== */

/* g_netGameStarted at 0x68ACE4 — declared in sonicr_globals.h */

/* Results screen state globals */

/* Text buffer pointers */

/* Portrait animation and player data */
/* g_currentPlayerIdx at 0x68ACD8 — declared in sonicr_globals.h */
/* g_playerSlotIds/g_playerCharIds removed — fields inside g_netPlayerDecorations.
 * NET_DECO_* layout defines in net_transport.h. */

/* Button state for prompt animation */

/* ROM data: texture UV coordinates from PE .data section (16.16 fixed >> 16) */
#define ROM_P1_TEXU    200   /* [0x5024a6] >> 16 */
#define ROM_P1_TEXV     28   /* [0x5024a8] >> 16 */
#define ROM_P2_TEXU     40   /* [0x5024ae] >> 16 */
#define ROM_P2_TEXV     44   /* [0x5024b0] >> 16 */
#define ROM_P3_TEXU      0   /* [0x5024aa] >> 16 */
#define ROM_P3_TEXV     44   /* [0x5024ac] >> 16 */
#define ROM_BTN_TEXU   160   /* [0x5024a2] >> 16 */
#define ROM_BTN_TEXV    28   /* [0x5024a4] >> 16 */

/* Forward declarations */
static void RenderPortraitStandings_D3D(void);
static void RenderButtonPrompt_D3D(void);

/* =====================================================================
 * RenderResultsScreen — FUN_00489d18 — 2105 bytes
 *
 * D3D path: 0x48a161-0x48a550
 *
 * Watcom fastcall: EAX = screenMode (0/1/2), EDX = playerOffset
 *
 * Prologue (0x489d18-0x489d5a):
 *   esi = 0x1c2 (baseY), edi = 0xa (baseX)
 *   dataOffset = playerOffset * 12 (lea+sub+shl)
 *   ecx = g_uiTexPage + 1 (tpage)
 *   edx = (g_totalFrames & 0x3f) << 8 (sine table byte offset)
 *   Branch: renderMode != 2 → D3D path at 0x48a161
 * ===================================================================== */
void RenderResultsScreen(int screenMode, int playerOffset)
{
    int baseY = 0x1c2;                              /* esi = 450 */
    int baseX = 0xa;                                 /* edi = 10 */
    int dataOffset = playerOffset * 12;              /* [ebp-0x20] */
    int tpage = g_uiTexPage + 1;                     /* ecx */
    int frameByteOff = (g_totalFrames & 0x3f) << 8;  /* edx */
    int textEndX;
    int sineValue, v;
    unsigned int animColor;

    /* Animated header — 0x48a161-0x48a1b2
     * sinTable byte-indexed: dword ptr [edx + 0x92568c]
     * idiv 0x10d (269), add 0xc0 (192) → v
     * animColor = 0xFF000000 | (v << 16) | (v << 8) | v */
    sineValue = g_sinTable[frameByteOff / 4]; /* EAX = [edx + 0x92568c] */
    v = (sineValue / 0x10d) + 0xc0;
    animColor = 0xFF000000
              | ((unsigned int)v << 16)
              | ((unsigned int)v << 8)
              | (unsigned int)v;

    DrawTexturedQuad(6, 6,           /* EAX=6, EDX=6 */
        0x43FA0000,                  /* depth = 500.0f */
        0x48, 0x1a,                  /* width=72, height=26 */
        tpage,
        0xdc, 0xdb,                  /* uvX=220, uvY=219 */
        0x24, 0xd,                   /* uvW=36, uvH=13 */
        animColor);

    /* Character banner — 0x48a1b7-0x48a1e6 */
    DrawTexturedQuad(0xe2, 0x20,     /* EAX=226, EDX=32 */
        0x43FA0000,
        0xbc, 0x18,                  /* width=188, height=24 */
        tpage,
        0xa0, dataOffset,            /* uvX=160, uvY=playerOffset*12 */
        0x5e, 0xc,                   /* uvW=94, uvH=12 */
        VERTEX_WHITE); // 0xFFE0E0E0);

    /* Player 1 portrait — 0x48a1eb-0x48a220 */
    DrawTexturedQuad(0x64, 0x9c,     /* EAX=100, EDX=156 */
        0x43FA0000,
        0x50, 0x20,                  /* width=80, height=32 */
        tpage,
        ROM_P1_TEXU, ROM_P1_TEXV,   /* uvX=[0x5024a6]>>16, uvY=[0x5024a8]>>16 */
        0x28, 0x10,                  /* uvW=40, uvH=16 */
        VERTEX_WHITE); // 0xFFE0E0E0);

    /* Multiplayer portraits — 0x48a225-0x48a2a2
     * Binary: cmp dword ptr [0x68ace4], 1; jne skip */
    if (g_netGameStarted == 1) {
        /* Player 2: 0x48a22e-0x48a263 */
        DrawTexturedQuad(0x1dc, 0x9c,  /* EAX=476, EDX=156 */
            0x43FA0000,
            0x50, 0x20,
            tpage,
            ROM_P2_TEXU, ROM_P2_TEXV, /* uvX=[0x5024ae]>>16, uvY=[0x5024b0]>>16 */
            0x28, 0x10,
            VERTEX_WHITE); // 0xFFE0E0E0);

        /* Player 3: 0x48a268-0x48a29d */
        DrawTexturedQuad(0x118, 0x9c,  /* EAX=280, EDX=156 */
            0x43FA0000,
            0x50, 0x20,
            tpage,
            ROM_P3_TEXU, ROM_P3_TEXV, /* uvX=[0x5024aa]>>16, uvY=[0x5024ac]>>16 */
            0x28, 0x10,
            VERTEX_WHITE); // 0xFFE0E0E0);
    }

    /* Text rendering — 0x48a2a2-0x48a2b6
     * EAX=edi(10), EDX=esi(450), EBX=0x689bbc, ECX=[0x68afd8]
     * Returns updated X in EAX */
    textEndX = DrawGlyphString(baseX, baseY,
        (int *)g_resultsTextBuffer, g_resultsTextLineCount);

    /* Blinking cursor — 0x48a2b6-0x48a2f3
     * if (g_totalFrames & 0xf) < 8: cursor at (textEndX-6, baseY-6) */
    if ((g_totalFrames & 0xf) < 8) {
        DrawTexturedQuad(textEndX - 6, baseY - 6, /* EAX=textEndX-6, EDX=esi-6 */
            0x3FF33333,              /* depth ≈ 1.9f */
            0x18, 0x20,              /* width=24, height=32 */
            tpage,
            0xe8, 0xf0,             /* uvX=232, uvY=240 */
            0xc, 0x10,              /* uvW=12, uvH=16 */
            0x90000000 | VERTEX_WHITE_RGB); // 0x90E0E0E0
    }

    /* Standings or press-start — 0x48a2f3-0x48a38a */
    if (g_resultsState == 0xa) {
        /* Press start prompt — 0x48a2fc-0x48a33d */
        if ((g_totalFrames & 0xf) < 8) {
            DrawTexturedQuad(baseX, baseY - 0x26, /* EAX=edi, EDX=esi-0x26 */
                0x40000000,          /* depth = 2.0f */
                0x100, 0x20,         /* width=256, height=32 */
                tpage,
                0, 0x9c,             /* uvX=0, uvY=156 */
                0x80, 0x10,          /* uvW=128, uvH=16 */
                VERTEX_WHITE); // 0xFFE0E0E0);
        }
    }
    else {
        /* Standings list — 0x48a33f-0x48a38a
         * Base ptr: 0x689bb8 + 0x108, stride 0x104, text x = 0xc */
        char *ptr;
        baseY -= 0x1a;                                     /* esi -= 0x1a */
        ptr = (char *)g_resultsPlayerData + 0x108;          /* edi = 0x689bb8 + 0x108 */
        for (int i = 1; i <= g_resultsPlayerCount; i++) {
            DrawGlyphString(0xc, baseY, (int *)ptr, 0x40);  /* EAX=0xc, EDX=esi */
            baseY -= 0x16;                                   /* esi -= 0x16 */
            ptr += 0x104;                                    /* edi += 0x104 */
        }
    }

    /* Mode-specific UI — 0x48a38c-0x48a4e8 */

    /* Mode 0: 0x48a38c-0x48a41b */
    if (screenMode == 0) {
        /* Blinking banner: 0x48a397-0x48a3cd */
        if ((g_totalFrames & 0xf) < 8) {
            DrawTexturedQuad(0xc0, 0xc0, /* EAX=0xc0, EDX=0xc0 */
                0x43FA0000,
                0x100, 0x20,         /* width=256, height=32 */
                tpage,
                0, 0x3c,             /* uvX=0 (push ebx=screenMode=0), uvY=60 */
                0x80, 0x10,          /* uvW=128, uvH=16 */
                VERTEX_WHITE); // 0xFFE0E0E0);
        }

        /* Position indicators loop: 0x48a3d2-0x48a41b
         * esi=0xf0, edi=0x4c; loop until edi==0x8c */
        {
            int loopY = 0xf0;        /* esi */
            int loopUvV = 0x4c;      /* edi */
            do {
                DrawTexturedQuad(0xc0, loopY,
                    0x43FA0000,
                    0x100, 0x20,     /* width=256, height=32 */
                    tpage,
                    0, loopUvV,      /* uvX=0, uvY=loopUvV */
                    0x80, 0x10,      /* uvW=128, uvH=16 */
                    VERTEX_WHITE); // 0xFFE0E0E0);
                loopY += 0x20;       /* esi += 32 */
                loopUvV += 0x10;     /* edi += 16 */
            } while (loopUvV != 0x8c);
        }
    }

    /* Mode 1: 0x48a41d-0x48a4e8 */
    if (screenMode == 1) {
        /* Blinking banner: 0x48a429-0x48a467 */
        if ((g_totalFrames & 0xf) < 8) {
            DrawTexturedQuad(0xc0, 0xc0,
                0x43FA0000,
                0x100, 0x20,
                g_uiTexPage + screenMode, /* add eax, ecx where ecx=screenMode=1 */
                0x80, 0x8c,          /* uvX=128, uvY=140 */
                0x80, 0x10,
                VERTEX_WHITE); // 0xFFE0E0E0);
        }

        /* Always-visible sprite: 0x48a46c-0x48a49c */
        DrawTexturedQuad(0xc0, 0x110,
            0x43FA0000,
            0x100, 0x20,
            tpage,
            0x80, 0x4c,             /* uvX=128, uvY=76 */
            0x80, 0x10,
            VERTEX_WHITE); // 0xFFE0E0E0);

        /* Conditional unlock: 0x48a4a1-0x48a4e3 */
        if (g_netCharSelectState == 3 || g_resultsUnlockFlag != 0) {
            DrawTexturedQuad(0xc0, 0x130,
                0x43FA0000,
                0x100, 0x20,
                tpage,
                0x80, 0x5c,         /* uvX=128, uvY=92 */
                0x80, 0x10,
                VERTEX_WHITE); // 0xFFE0E0E0);
        }
    }

    /* Mode 2: 0x48a4e8-0x48a53e — NO blinking check
     * texV = (netReady == 1) ? 0x6c : 0x7c */
    if (screenMode == 2) {
        int texV = (g_netGameStarted == 1) ? 0x6c : 0x7c;
        DrawTexturedQuad(0xc0, 0xe0,
            0x43FA0000,
            0x100, 0x20,
            tpage,
            0x80, texV,             /* uvX=128, uvY per netReady */
            0x80, 0x10,
            VERTEX_WHITE); // 0xFFE0E0E0);
    }

    /* Epilogue — 0x48a53e: always call helpers */
    RenderPortraitStandings_D3D();
    RenderButtonPrompt_D3D();
}

/* =====================================================================
 * RenderPortraitStandings_D3D — FUN_0048999c — 891 bytes
 *
 * D3D path: 0x489b88-0x489d12
 *
 * Portrait UV for DrawTexturedQuad (matching binary push order):
 *   uvX param = (value % 5) * 16 + 0xb0   (remainder-based)
 *   uvY param = (value / 5) * 16 + 0xbb   (quotient-based)
 *
 * animCounter uses signed idiv; charId uses unsigned div.
 * Player arrays accessed at byte stride 0x64 from base addresses.
 * ===================================================================== */
static void RenderPortraitStandings_D3D(void)
{
    int animCounter = g_netFilteredProviders[0];     /* [0x68a6ec] */
    int playerCount = g_netPlayerCount;              /* [0x68aee8] */
    int tpage = g_uiTexPage + 1;
    int textY = 0xc6;                                /* esi = 198 */

    /* Prologue UV from animCounter — 0x4899a7-0x4899eb
     * Two idiv-by-5 operations: first gives quotient, second gives remainder */
    int animUvX = ((animCounter % 5) * 16) + 0xb0;  /* DrawTexturedQuad uvX param */
    int animUvY = ((animCounter / 5) * 16) + 0xbb;  /* DrawTexturedQuad uvY param */

    /* Single player: playerCount < 2 — 0x489b88-0x489bd6 */
    if (playerCount < 2) {
        /* Portrait: 0x489b93-0x489bbc
         * push order: color, texH, texW, eax(animUvY*), edx(animUvX*), tpage, ...
         * *NOTE: eax=quotient-based pushed as stack texV, edx=remainder-based as texU */
        DrawTexturedQuad(0x1c6, 0xc8,    /* EAX=454, EDX=200 */
            0x40000000,                   /* depth = 2.0f */
            0x10, 0x10,                   /* width=16, height=16 */
            tpage,
            animUvX, animUvY,             /* uvX=(rem*16)+0xb0, uvY=(quot*16)+0xbb */
            0x10, 0x10,                   /* uvW=16, uvH=16 */
            0x90000000 | VERTEX_WHITE_RGB); // 0x90E0E0E0

        /* Text: 0x489bc1-0x489bc8 — EAX=0x1dc, EDX=esi(textY) */
        DrawGlyphString(0x1dc, textY, (int *)g_portraitTextBuffer, 0x10);
        return;
    }

    /* Multiplayer loop: 0x489bd7-0x489d12 */
    {
        int i = 0;                       /* [ebp-0x28] */
        int slotOffset = 0;              /* edi — byte offset, stride 0x64 */
        int portraitY = 0xc8;            /* [ebp-0x20] = 200 */

        if (playerCount <= 0) {
            return;
        }

        for (;;) {
            int slotId = *(int *)(g_netPlayerDecorations + slotOffset + NET_DECO_DPID); /* [edi + 0x68ad4c] */
            int uvX, uvY;

            tpage = g_uiTexPage + 1;     /* reloaded each iteration: [0x8f6c48]+1 */

            if (g_currentPlayerIdx == slotId) {
                /* Current player: animated UV — 0x489c10-0x489c6d
                 * Signed idiv by 5 */
                int rem = animCounter % 5;
                int quot = animCounter / 5;
                uvX = (rem * 16) + 0xb0;
                uvY = (quot * 16) + 0xbb;

                DrawTexturedQuad(0x1c6, portraitY,
                    0x40000000, 0x10, 0x10, tpage,
                    uvX, uvY, 0x10, 0x10,
                    0x90000000 | VERTEX_WHITE_RGB); // 0x90E0E0E0

                /* Text: 0x489c72 — g_portraitTextBuffer */
                {
                    int endX = DrawGlyphString(0x1dc, textY, (int *)g_portraitTextBuffer, 0x10);
                    int textH = 0x14;
                    int iconH = 24;
                    int iconYOff = (textH - iconH) / 2;
                    int iconIdx = net_platform_icon(net_get_slot_platform(i),
                                                    net_get_slot_region(i));
                    int icoUvX, icoUvY;
                    net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                    DrawTexturedQuad(
                        endX + 4, textY + iconYOff,
                        0x40000000,
                        iconH, iconH,
                        TPAGE_PLATFORM_ICONS,
                        icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                        0xFFFFFFFFu);
                }
            }
            else {
                /* Other player: character ID UV — 0x489ca5-0x489d00
                 * UNSIGNED div by 5 (xor edx,edx / div) */
                unsigned int charId = (unsigned int)*(int *)(g_netPlayerDecorations + slotOffset + NET_DECO_LOBBYCHAR);
                unsigned int rem = charId % 5;
                unsigned int quot = charId / 5;
                uvX = (rem * 16) + 0xb0;
                uvY = (quot * 16) + 0xbb;

                DrawTexturedQuad(0x1c6, portraitY,
                    0x40000000, 0x10, 0x10, tpage,
                    uvX, uvY, 0x10, 0x10,
                    0x90000000 | VERTEX_WHITE_RGB); // 0x90E0E0E0

                /* Text: 0x489d05 — per-player name at 0x68acf8 + edi + 0x14 */
                {
                    int endX = DrawGlyphString(0x1dc, textY,
                        (int *)(g_netPlayerDecorations + slotOffset + 0x14), 0x10);
                    int textH = 0x14;
                    int iconH = 24;
                    int iconYOff = (textH - iconH) / 2;
                    int iconIdx = net_platform_icon(net_get_slot_platform(i),
                                                    net_get_slot_region(i));
                    int icoUvX, icoUvY;
                    net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                    DrawTexturedQuad(
                        endX + 4, textY + iconYOff,
                        0x40000000,
                        iconH, iconH,
                        TPAGE_PLATFORM_ICONS,
                        icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                        0xFFFFFFFFu);
                }
            }

            /* Advance: 0x489c7c-0x489c9a */
            textY += 0x16;           /* esi += 22 */
            slotOffset += 0x64;      /* edi += 100 */
            portraitY += 0x16;       /* [ebp-0x20] += 22 */
            i++;
            if (i >= playerCount) {
                return;
            }
        }
    }
}

/* =====================================================================
 * RenderButtonPrompt_D3D — FUN_0048987c — 287 bytes
 *
 * D3D path: 0x48992f-0x48999a
 *
 * Prologue (0x48987c-0x4898bb):
 *   edx = 0xf0, esi = 0x60, button state → eax (uvX)
 *   edi = edx + 0x28 = 0x118, esi -= 0x28 = 0x38, edx -= 0x1c = 0xd4
 *   ebx = g_uiTexPage + 1
 *   Branch: renderMode != 2 → D3D at 0x48992f
 * ===================================================================== */
static void RenderButtonPrompt_D3D(void)
{
    int screenX = 0x60 - 0x28;      /* esi = 56 */
    int screenY2 = 0xf0 + 0x28;     /* edi = 280 */

    /* The MODE A/B sprite (0x48992f, uv 0xb0/0xd8 at 0x9c) is not drawn: the
     * dispatch it fed is forced by role in main.c, so the indicator would show
     * a setting that has no effect. Its F5 toggle is disabled to match. */

    /* Additional prompt: 0x48994f-0x489990
     * if g_resultsState > 1 AND g_netGameStarted != 0 */
    if (g_resultsState <= 1) {
        return;
    }
    if (g_netGameStarted == 0) {
        return;
    }
#ifdef SONICR_XBOX
    /* The F5 prompt's action (MODE A/B) is disabled, and there is no F5 on
     * the pad: don't advertise it. */
    return;
#endif

    DrawTexturedQuad(screenX, screenY2,  /* EAX=56, EDX=280 */
        0x43FA0000,
        0x50, 0x20,                      /* width=80, height=32 */
        g_uiTexPage + 1,                 /* [0x8f6c48]+1 reloaded */
        ROM_BTN_TEXU, ROM_BTN_TEXV,     /* uvX=160, uvY=28 */
        0x28, 0x10,                      /* uvW=40, uvH=16 */
        VERTEX_WHITE); // 0xFFE0E0E0);
}
