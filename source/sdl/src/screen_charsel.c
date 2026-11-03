/**
 * screen_charsel.c — Character Select Screen
 *
 * CharacterSelectScreen — 0x0048C968 — 5324 bytes
 * Multi-player character selection with unlock system, per-player
 * navigation, 3D model rendering, and character icon strip.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "player_struct.h"
#include "r_state.h"

/* Forward declarations */
extern void SetupMenuTexturesD3D(void);      /* FUN_00438CD8 */
extern void FinalizeMenuTexturesD3D(void);
extern void platform_pump_events(void);
extern int  platform_poll_events(unsigned char *keystateOut, int keystateSize);
extern void WaitForFrameCap(void);
extern void UpdateVertexLighting(Player *player);
extern void UpdateFrameTimers(void);

/* RenderCharacterOnPodium — FUN_004437fc.
 * Binary calls this from CharacterSelectScreen at 0x48d725.
 * Verified by raw E8 scan of 0x48c968-0x48da6c. */
extern void RenderCharacterOnPodium(int xOff, int yOff, int zOffset,
                                     int angleC, int angleD, int angleE,
                                     Player *player);

/* Per-player confirm state */
extern int g_p1Confirmed, g_p2Confirmed, g_p3Confirmed, g_p4Confirmed;
extern int g_charSelectIndex[];     /* 0x0068AF84 — per-player char index, stride 4 */
extern int g_charSelection[];       /* 0x0068AFEC — per-player selection, stride 4 */
extern int g_charPositionTable[];   /* 0x005025FC — cursor Y per character */
extern int g_charIconUV[];          /* 0x005026A8 — icon UV pairs */
/* g_aiCharAssignment (0x502648) and g_charSelViewportPos (0x502660) are macro
 * views onto g_charSelDataBlock in sonicr_globals.h. They OVERLAP — ai[6..9]
 * is vp[0..3] — so they must not be re-declared as separate arrays here. */

/* Per-character Y position offset for RenderCharacterOnPodium (0x005026F0).
 * Negative values push the model down on screen (larger characters need this). */
static int s_charAngleTable[CHAR_COUNT] = {
    0,    /* Sonic */
    0,    /* Tails */
    0,    /* Knuckles */
    0,    /* Amy */
    -55,  /* Eggman */
    -15,  /* Metal Sonic */
    -8,   /* Tails Doll */
    -15,  /* Metal Knuckles */
    0,    /* Egg Robo */
    -10,  /* Super Sonic */
};

/* Per-viewport cursor state: charIndex, cursorY, cursorTargetY
 * Viewport stride = 0x28 (10 ints). Fields at +0x24, +0x28, +0x2C from base.
 * But since our array starts at 0x925290, the per-VP fields start at offset 0x24.
 * VP(n) charIndex  = g_stateBlock92528C[n*10 + 10]
 * VP(n) cursorY    = g_stateBlock92528C[n*10 + 11]  (crosses into next VP block)
 * VP(n) cursorTgtY = g_stateBlock92528C[n*10 + 12]
 * VP(n) animPos    = g_stateBlock92528C[n*10 + 13] */
#define VP_CHAR(n)    g_stateBlock92528C[(n)*10 + 10]
#define VP_CURSORY(n) g_stateBlock92528C[(n)*10 + 11]
#define VP_TARGET(n)  g_stateBlock92528C[(n)*10 + 12]
#define VP_ANIM(n)    g_stateBlock92528C[(n)*10 + 13]

/* Camera / LookBack bit in a player's 16-bit input word. Holding it is the
 * override that lets you lock in on a character somebody else already took. */
#define CHARSEL_DUPE_OVERRIDE  0x0040

/* The binary indexes the confirm flags as [playerIdx*4 + 0x68AFB8]; the port
 * declares them as four separate ints, so resolve the index here. */
static int *CharSelConfirmFlag(int playerIdx)
{
    switch (playerIdx) {
        case 0:  return &g_p1Confirmed;
        case 1:  return &g_p2Confirmed;
        case 2:  return &g_p3Confirmed;
        default: return &g_p4Confirmed;
    }
}

/**
 * CharSelSuperSonicToggle — binary 0x48D1D1-0x48D2C8
 *
 * Up/Down on Sonic's slot swaps between Sonic and Super Sonic once every
 * character is unlocked. This is NOT a player-1 privilege: every operand in
 * the binary is player-relative — cursor `[edx + 0x9252B4]`, input
 * `[ebp-0x50] + 0x9020C9`, confirm flag `[ebp-0x2C] + 0x68AFB8`, charId and
 * anim `[edi + 0x8FD5E6]` / `[edi + 0x8FD58C]`. Only the scroll state
 * (0x925294 / 0x925298 / 0x92529C) is global, which is right: there is one
 * shared portrait strip, so whoever flips it flips the view for everybody.
 *
 * Returns 1 if it acted, so the caller can reset the idle timer.
 */
static int CharSelSuperSonicToggle(int playerIdx, unsigned char bits)
{
    /* 0x48D1D1: fully unlocked, and 0x48D1E4: scroll has settled. */
    if (g_allCharsUnlocked != 2 || g_menuScrollTarget != g_menuScrollX) {
        return 0;
    }
    /* 0x48D1F0-0x48D1F8: only while the cursor is on Sonic's slot. */
    if (VP_CHAR(playerIdx) != 0) {
        return 0;
    }

    Player *pp = &((Player *)g_playerBase)[playerIdx];
    int *confirmed = CharSelConfirmFlag(playerIdx);

    /* UP at Super Sonic → Sonic (0x48D207) */
    if ((bits & 0x10) != 0 && (bits & 0x20) == 0 &&
        g_menuScrollX == g_menuMaxScroll)
    {
        *confirmed = 0;
        pp->charId = 0;                 /* Sonic */
        pp->prevAnimId = pp->animId;
        pp->animId = 2;                 /* idle pose */
        g_menuScrollTarget = 0;
        return 1;
    }
    /* DOWN at Sonic → Super Sonic (0x48D26D) */
    if ((bits & 0x20) != 0 && (bits & 0x10) == 0 && g_menuScrollX == 0) {
        *confirmed = 0;
        pp->charId = 9;                 /* Super Sonic */
        pp->prevAnimId = pp->animId;
        pp->animId = 2;                 /* idle pose */
        g_menuScrollTarget = g_menuMaxScroll;
        return 1;
    }
    return 0;
}

/**
 * CharSelConfirmAllowed — binary 0x48CF39-0x48CF61
 *
 * Two players may not settle on the same character by default. The binary
 * walks the other players and, for each one that is ALREADY CONFIRMED and
 * holding the same charId, refuses this player's confirm unless they are
 * holding the camera button — 0x48CF5A is the only read of that bit anywhere
 * in CharacterSelectScreen, so movement is deliberately unrestricted and only
 * locking in is gated.
 *
 * Two details the binary is specific about:
 *  - only a CONFIRMED player blocks (0x48CF3E); someone merely hovering the
 *    same character is no obstacle.
 *  - the compare is on charId (player+0xF2, 0x48CF47/0x48CF4E), NOT the menu
 *    cursor index — Super Sonic sits on Sonic's slot with VP_CHAR still 0 but
 *    charId 9, and those two must not collide.
 *
 * inputWord is this player's full 16-bit word, g_perPlayerInput[playerIdx]
 * (0x9020C8 + playerIdx*2) — the same word the binary indexes.
 */
static int CharSelConfirmAllowed(int playerIdx, unsigned short inputWord)
{
    const Player *pb = (const Player *)g_playerBase;
    short myChar = pb[playerIdx].charId;

    for (int other = 0; other < g_numViewports; other++) {
        if (other == playerIdx) {
            continue;                               /* 0x48CF39 */
        }
        if (*CharSelConfirmFlag(other) == 0) {
            continue;                               /* 0x48CF3E */
        }
        if (pb[other].charId != myChar) {
            continue;                               /* 0x48CF4E */
        }
        /* Taken. Only the camera button gets you through. */
        return (inputWord & CHARSEL_DUPE_OVERRIDE) != 0;   /* 0x48CF5A */
    }
    return 1;
}

/**
 * CharacterSelectScreen — 0x0048C968 — 5324 bytes
 *
 * Returns:
 *   SCREEN_OK (1) = characters selected, proceed to course select
 *   SCREEN_BACK (0) = user pressed back
 *   SCREEN_TITLE (-1) = idle timeout → return to title
 */
int CharacterSelectScreen(void)
{
    g_menuState = 5;

    /* Init: per-player confirm flags  */
    g_p1Confirmed = 0;
    g_p2Confirmed = 1;  /* only P1 needs to confirm in single-player */
    g_p3Confirmed = 1;
    g_p4Confirmed = 1;
    if (g_numViewports > 1) {
        g_p2Confirmed = 0;
        if (g_numViewports > 2) {
            g_p3Confirmed = 0;
            if (g_numViewports > 3) {
                g_p4Confirmed = 0;
            }
        }
    }

    /* Fade background: blue */
    g_bgTintG = 0;
    g_bgTintB = 0xFF;
    g_bgTintR = 0;

    /* Load textures (D3D path: 0x48cabb-0x48caff) */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();
    /* D3D path only loads g_uiTexPage and g_uiTexPage+1.
     * Character textures remain at tpages 14-15 from InitOptionStuff.
     * Face tpage bytes already point to g_tpageCharacters/g_tpagePlayfield1
     * via RemapCharacterTpages (FUN_00470460) called during init.
     * SOFTWARE path (not used) would load PLAYER00/PLAYER01 into tpages 0-1
     * and do face tpage remapping here — but that would overwrite
     * track textures (RUIN00/RUIN03) needed by the course select track model. */
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_CHARSEL);
    for (int tp = 0; tp < 52; tp++) {
        if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
            g_tpageStateArray[tp] = 4;
        }
    }
    ProcessTpageStates();
    FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
    ResumeCD();
#endif

    /* Init selection state */
    int prevVP = g_prevNumViewports;
    g_totalFrames = 0;
    g_fadeState = FADE_IN;
    g_menuExtraY = g_screenHeight + g_screenScale * -0x3C;
    g_modelRotation = 0;

    /* Reset special unlock if not valid */
    if (g_superSonicSeed != 0 && g_allCharsUnlocked == 0) {
        g_superSonicSeed = 0;
    }

    /* Per-player character init */
    for (int p = 0; p < g_numViewports; p++) {
        /* If character is locked or viewport count changed, reset to default */
        if (g_charUnlockTable[g_charSelection[p]] != 2 ||
            prevVP != g_numViewports || g_numViewports > 1) {
            g_charSelectIndex[p] = p;
            g_charSelection[p] = p;
        }
        if (g_charSelection[p] == 0 && g_superSonicSeed != 0) {
            g_charSelection[p] = CHAR_SUPER_SONIC;
        }
    }

    /* Init menu scroll and per-viewport cursor */
    g_menuScrollX = g_superSonicSeed;
    g_menuScrollTarget = g_superSonicSeed;
    g_menuMaxScroll = 0x28;

    for (int p = 0; p < g_numViewports; p++) {
        VP_CHAR(p) = g_charSelectIndex[p];
        VP_CURSORY(p) = g_charPositionTable[g_charSelectIndex[p]];
        VP_TARGET(p) = VP_CURSORY(p);
        VP_ANIM(p) = VP_CURSORY(p);
    }

    /* Per-player animation init — binary 0x48cd0a-0x48cd84
     * Sets animId=2 (ANIM_STILL), then directly looks up the animation table
     * to get the frame stream pointer and first frame index.
     * Binary does NOT call TickPlayerAnimation on charsel — static pose only. */
    Player *pb = (Player *)g_playerBase;
    for (int p = 0; p < g_numViewports; p++) {
        int charId = g_charSelection[p];
        pb[p].charId = (short)charId;
        *(short *)&pb[p].selectCharId = (short)charId;
        pb[p].charId = (short)charId;
        pb[p].modelCharId = 0;
        pb[p]._unk_0x1E0 = (short)charId;
        pb[p].animId = 2;                                    /* 0x48cd19: ANIM_STILL */

        /* 0x48cd50-0x48cd6d: look up anim table, read first frame */
        void **animTables = (void **)g_charAnimTables;
        uintptr_t *animPtrs = (uintptr_t *)animTables[charId * 2];
        if (animPtrs) {
            const short *frameStream = (const short *)(uintptr_t)animPtrs[pb[p].animId];
            if (frameStream) {
                g_animDataPtrs[p] = frameStream;
                pb[p].animFrameIdx = (int)*frameStream - 1;  /* 0x48cd69-0x48cd6d */
            }
        }
    }

    g_renderEnabled = 1;
    g_screenResult = SCREEN_QUIT;

    g_prevNumViewports = g_numViewports;

    uint32_t startTime = timeGetTime();
    unsigned int idleStartSec = startTime / 1000;

    do {
        platform_pump_events();
        g_currentTime = timeGetTime();
        uint32_t now = timeGetTime();
        unsigned int elapsed = now / 1000 - idleStartSec;
        unsigned int sign = (int)elapsed >> 31;
        int idleElapsed = (int)((elapsed ^ sign) - sign);

        int cdStatus = GetLogicalCDTrack();
        if (cdStatus != 5) {
            UpdateCDPlayback(5);
        }

        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -0x100) {
            return g_screenResult;
        }

        /* Read per-player character from player struct for change detection */
        int prevChars[4];
        Player *pb = (Player *)g_playerBase;
        for (int p = 0; p < g_numViewports; p++) {
            prevChars[p] = (int)pb[p].charId;
        }

        /* 0x48CE4E: character select drives itself from UpdatePerPlayerInput,
         * not ReadInput. That call is what routes each seat's device — the
         * g_pNJoystickPtr the lobby assigned — into g_perPlayerInput[0..3].
         * ReadInput only builds the one combined word, which is why the port
         * could not tell two players apart and had to read the raw keyboard
         * device words instead.
         *
         * ReadInput used to pump the event queue and refresh
         * g_diKeyboardState on the way in; UpdatePerPlayerInput does not, so
         * do it here. g_inputBits is the high byte of g_combinedInputState in
         * the binary (0x9020D9 overlays 0x9020D8); our globals are separate,
         * so sync it for the Back check at 0x48D13E. */
        platform_poll_events(g_diKeyboardState, 256);
        UpdatePerPlayerInput();
        g_inputBits = (unsigned char)(g_combinedInputState >> 8);

        /* Input handling */
        if (g_fadeState == FADE_VISIBLE) {
            /* Confirm — binary 0x48CE61-0x48CF9B, one pass per viewport over
             * g_perPlayerInput[p] (0x9020C8 + p*2). Every operand in that
             * loop is player-relative: input word, confirm flag, cursor pair,
             * player struct. It scales to four seats as written. */
            for (int p = 0; p < g_numViewports; p++) {
                unsigned short input = g_perPlayerInput[p];
                int *confirmed = CharSelConfirmFlag(p);

                if ((input & 0x0600) == 0) {            /* 0x48CE85 */
                    continue;
                }
                if (*confirmed != 0) {                  /* 0x48CE91 */
                    continue;
                }
                if (VP_CURSORY(p) != VP_TARGET(p)) {    /* 0x48CEB0 */
                    continue;
                }
                /* 0x48CEB7-0x48CED0: sitting on Sonic's slot while the
                 * portrait strip is still sliding toward Super Sonic. Refuse
                 * until the scroll settles, so a confirm cannot land on a
                 * character that is halfway off screen. */
                if (VP_CHAR(p) == 0 && g_menuScrollX != 0 &&
                    g_menuScrollX != g_menuScrollTarget) {
                    continue;
                }
                if (!CharSelConfirmAllowed(p, input)) { /* 0x48CF39-0x48CF61 */
                    continue;
                }

                *confirmed = 1;                         /* 0x48CF6A */
                pb[p].prevAnimId = pb[p].animId;        /* 0x48CF74 */
                pb[p].animId = 6;                       /* 0x48CF82: WIN_POSE */
                PlaySoundEffect(2, 0, 0);               /* 0x48CF8B */
            }

            /* All confirmed → finalize */
            if (g_p1Confirmed && g_p2Confirmed && g_p3Confirmed && g_p4Confirmed) {
                /* Set player character IDs for the race */
                Player *pb = (Player *)g_playerBase;
                for (int p = 0; p < g_numViewports; p++) {
                    g_charSelection[p] = (int)pb[p].charId;
                    g_charSelectIndex[p] = VP_CHAR(p);
                }

                /* AI opponent assignment — binary 0x48CFE1-0x48D0C4.
                 *
                 * TWO paths, chosen by whether the HUMAN picked an unlockable:
                 *   0x48cfe1  movsx eax, word [0x8fd5e6]   ; player charId
                 *   0x48cfe8  cmp   eax, 5
                 *   0x48cfeb  jge   0x48d02d
                 *
                 *   charId <  5 → 0x48cfed, base-character fill: AI gets
                 *                 charIds 0..4 minus the player's. Never
                 *                 touches g_aiCharAssignment, never reads the
                 *                 unlock table, cannot produce an unlockable.
                 *   charId >= 5 → 0x48d02d, the g_aiCharAssignment scan, which
                 *                 prefers unlocked characters.
                 *
                 * So unlocked opponents appear ONLY when the human is also on
                 * an unlocked character. This branch was previously absent and
                 * the table scan ran unconditionally, handing Super Sonic and
                 * friends to players racing as Sonic. The old comment here
                 * cited the range 0x48D02D-0x48D0C4, which is the table loop
                 * ALONE — the dispatch that decides whether it runs is twelve
                 * bytes earlier, and verifying only the cited range is how the
                 * omission survived several audits. */
                if (g_raceType != RACE_MULTIPLAYER) {
                    int selectedChar = (int)pb[0].charId;

                    if (selectedChar < 5) {
                        /* 0x48cfed-0x48d02b. charIdx walks 0..4, the player's
                         * own is skipped, each survivor goes to the next slot.
                         * Exactly four stores — one of the five always skips.
                         * 0x48d023 `mov word [edi+0x8fceca], ax`, with edi
                         * stepping 0x71c per store, lands on
                         * playerBase[1..4].charId (0x8FDD02, 0x8FE41E,
                         * 0x8FEB3A, 0x8FF256). No unlock check anywhere: this
                         * is why Eggman is always an AI opponent on a fresh
                         * save even though he is locked for human play. */
                        int slot = 1;
                        for (int charIdx = 0; charIdx < 5; charIdx++) {   /* 0x48d005 */
                            if (charIdx == selectedChar) {                /* 0x48d018 */
                                continue;
                            }
                            pb[slot].charId = (short)charIdx;             /* 0x48d023 */
                            slot++;                                       /* 0x48d01a */
                        }
                    }
                    else {
                        int superSonicAssigned = 0;
                        int aiIdx = 0;
                        int slot = 1;
                        while (slot < 5) {
                            int candidate = g_aiCharAssignment[aiIdx];
                            if (candidate == selectedChar) {
                                aiIdx++;
                                continue;
                            }
                            /* 0x48D0A4: cmp dword [edi*4 + 0x8fba64], 2 — that
                             * is g_charUnlockTable[candidate]. */
                            if (candidate > 4 && g_charUnlockTable[candidate] != 2) {
                                aiIdx++;
                                continue;
                            }
                            if (candidate == 0 && superSonicAssigned) {
                                aiIdx++;
                                continue;
                            }
                            /* Super Sonic player: also skip Sonic (0x48D038-0x48D04E) */
                            if (selectedChar == 9 && candidate == 0) {
                                aiIdx++;
                                continue;
                            }
                            pb[slot].charId = (short)candidate;
                            if (g_raceSubMode == SUBMODE_TAG && candidate == 9) {
                                superSonicAssigned = 1;
                            }
                            slot++;
                            aiIdx++;
                        }
                    }
                }

                g_screenResult = SCREEN_OK;
                g_fadeState = FADE_OUT;
                g_superSonicSeed = g_menuScrollX;
            }

            /* Back button — either player can go back */
            if ((g_inputBits & 1) != 0) {
                PlaySoundEffect(0, 0, 0);
                g_fadeState = FADE_OUT;
                g_screenResult = SCREEN_BACK;
            }

            /* Idle timeout */
            if (idleElapsed > 30) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_TITLE;
                g_fadeState = FADE_OUT;
            }

            /* Navigation — binary 0x48D18F-0x48D495, again one pass per
             * viewport. The fade state is re-read at 0x48D18F, so a Back or
             * an idle timeout above suppresses movement for the rest of the
             * frame. */
            if (g_fadeState == FADE_VISIBLE) {
                for (int p = 0; p < g_numViewports; p++) {
                    unsigned char bits = (unsigned char)(g_perPlayerInput[p] >> 8);
                    Player *pp = &pb[p];
                    int *confirmed = CharSelConfirmFlag(p);

                    if (VP_TARGET(p) != VP_CURSORY(p)) {        /* 0x48D1BF */
                        continue;
                    }

                    /* Sonic / Super Sonic toggle — 0x48D1D1-0x48D2C8. Every
                     * operand there is player-relative, so this is not a
                     * player-1 privilege. */
                    if (CharSelSuperSonicToggle(p, bits)) {
                        startTime = timeGetTime();
                        idleStartSec = startTime / 1000;
                    }

                    /* Left — 0x48D2C8-0x48D38F */
                    if ((bits & 0x40) != 0 && (bits & 0x80) == 0 &&
                        VP_CHAR(p) > 0)
                    {
                        int scan = VP_CHAR(p) - 1;                  /* 0x48D2F4 */
                        while (scan >= 0) {                         /* 0x48D31A */
                            if (g_charUnlockTable[scan] == 2) {     /* 0x48D302 */
                                VP_CHAR(p) = scan;                  /* 0x48D30E */
                                break;
                            }
                            scan--;                                 /* 0x48D319 */
                        }
                        /* 0x48D31E stores the scan result whether or not the
                         * scan hit: the cursor only moves on a hit, but the
                         * charId write, the confirm reset, the pose and the
                         * SFX are unconditional once the cursor is past 0. */
                        pp->charId = (short)scan;
                        /* 0x48D325-0x48D346: landing back on Sonic's slot with
                         * the strip still scrolled past halfway re-asserts
                         * Super Sonic. Written through edi — per-player. */
                        if (pp->charId == 0 &&
                            ((int)g_menuMaxScroll / 2) < (int)g_menuScrollX) {
                            pp->charId = 9;   /* Super Sonic */
                        }
                        *confirmed = 0;                             /* 0x48D354 */
                        pp->prevAnimId = pp->animId;
                        pp->animId = 2;                             /* idle pose */
                        PlaySoundEffect(1, 0, 0);
                        startTime = timeGetTime();
                        idleStartSec = startTime / 1000;
                    }

                    /* Right — 0x48D392-0x48D449. No Super Sonic re-assert on
                     * this side; the binary only has it on the left. */
                    if ((bits & 0x80) != 0 && (bits & 0x40) == 0 &&
                        VP_CHAR(p) < 8)
                    {
                        /* 0x48D3BF spills the old cursor to 0x92537C, which
                         * nothing else in the binary touches — a scratch. */
                        int oldChar = VP_CHAR(p);
                        int scan = oldChar + 1;                     /* 0x48D3C5 */
                        while (scan <= 8) {                         /* 0x48D3EC */
                            if (g_charUnlockTable[scan] == 2) {     /* 0x48D3D4 */
                                VP_CHAR(p) = scan;                  /* 0x48D3E0 */
                                break;
                            }
                            scan++;
                        }
                        /* 0x48D3FA: nothing unlocked to the right, cursor
                         * never moved, so nothing else happens either. */
                        if (oldChar != VP_CHAR(p)) {
                            pp->charId = (short)scan;               /* 0x48D402 */
                            *confirmed = 0;                         /* 0x48D40E */
                            pp->prevAnimId = pp->animId;
                            pp->animId = 2;
                            PlaySoundEffect(1, 0, 0);
                            startTime = timeGetTime();
                            idleStartSec = startTime / 1000;
                        }
                    }

                    VP_TARGET(p) = g_charPositionTable[VP_CHAR(p)];  /* 0x48D44C */
                }
            }
        }

        /* Cursor animation */
        for (int p = 0; p < g_numViewports; p++) {
            int cur = VP_CURSORY(p);
            int tgt = VP_TARGET(p);
            if (cur < tgt) {
                cur += 3;
                if (cur > tgt) {
                    cur = tgt;
                }
                VP_CURSORY(p) = cur;
            }
            else if (tgt < cur) {
                cur -= 3;
                if (cur < tgt) {
                    cur = tgt;
                }
                VP_CURSORY(p) = cur;
            }
        }

        /* Menu scroll */
        if (g_menuScrollX < g_menuScrollTarget) {
            g_menuScrollX += 2;
        }
        else if (g_menuScrollTarget < g_menuScrollX) {
            g_menuScrollX -= 2;
        }

        /* Copy cursorY to animPos each frame */
        g_menuAnimY = VP_CURSORY(0);
        VP_ANIM(0) = VP_CURSORY(0);
        for (int p = 1; p < g_numViewports; p++) {
            VP_ANIM(p) = VP_CURSORY(p);
        }
        g_modelRotation = (g_modelRotation + 0x20) & 0xFFF;
        UpdateFrameTimers();

        /* Per-player animation update — binary 0x48d58c-0x48d65b
         * Inline frame advance: if charId or animId changed, re-init frame stream.
         * Then always: read next frame, handle loop markers, update animFrameIdx. */
        pb = (Player *)g_playerBase;
        for (int p = 0; p < g_numViewports; p++) {
            int curChar = (int)pb[p].charId;

            /* 0x48d58c-0x48d5e0: if charId or animId changed, re-init */
            if (curChar != prevChars[p] ||
                pb[p].animId != pb[p].prevAnimId)
            {
                /* Update model index on char change */
                if (curChar != prevChars[p]) {
                    *(short *)&pb[p].selectCharId = (short)curChar;
                    pb[p].charId = (short)curChar;
                    pb[p].modelCharId = 0;
                    pb[p]._unk_0x1E0 = (short)curChar;
                }
                /* 0x48d5b7-0x48d5db: look up anim table, reset frame stream */
                void **animTables = (void **)g_charAnimTables;
                uintptr_t *animPtrs = (uintptr_t *)animTables[curChar * 2];
                if (animPtrs) {
                    const short *fs = (const short *)(uintptr_t)animPtrs[pb[p].animId];
                    g_animDataPtrs[p] = fs;
                    if (fs) pb[p].animFrameIdx = (int)*fs - 1;
                }
            }

            /* 0x48d5e1-0x48d636: advance frame stream */
            const short *fs = g_animDataPtrs[p];
            if (fs) {
                int frame = (int)*fs;
                g_animDataPtrs[p] = fs + 1;               /* 0x48d5e7: ptr += 2 bytes */

                /* 0x48d5f9: loop marker (-1) — rewind */
                if (frame == -1) {
                    const short *cur = g_animDataPtrs[p];
                    cur = cur - (*cur);                      /* 0x48d604-0x48d609 */
                    g_animDataPtrs[p] = cur + 1;
                    frame = (int)*cur;
                }

                frame &= 0xFFF;                              /* 0x48d629 */
                pb[p].animFrameIdx = frame - 1;              /* 0x48d635-0x48d636 */
            }

            /* 0x48d63c: copy animId to change tracker */
            pb[p].prevAnimId = pb[p].animId;
        }

        /* Rendering */
        ProcessTpageStates();
        BeginFrame();
        RenderBackground();
        EndFrame();
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }

        BeginFrame();

        /* Per-player 3D character models */
        pb = (Player *)g_playerBase;
        /* zOffset = g_numViewports * 32 + 0xE0 (from disasm 0x48DAB5-0x48DABD) */
        int modelZOffset = g_numViewports * 0x20 + 0xE0;
        for (int p = 0; p < g_numViewports; p++) {
            Player *playerPtr = &pb[p];
            /* 0x48db00: mov eax,[0x92528c] / 0x48db05: mov [edx+0x8fd504],eax
             * — the turntable angle is copied into the player's heading before
             * lighting, because UpdateVertexLighting picks its gouraud
             * direction row as 0x1F - (angleYaw >> 7). Without it the row is
             * whatever the slot last held and the lighting does not turn with
             * the model. 0x8FD504 is g_playerBase + 0x10 (stride 0x71C). */
            playerPtr->angleYaw = g_modelRotation;
            UpdateVertexLighting(playerPtr);
            int charId = (int)pb[p].charId;
            /* X offset from g_charSelViewportPos: indexed by numViewports*4 + p,
             * then divided by 3.  From disasm 0x48DACF-0x48DAF4:
             *   shl eax,4 → byte offset = numViewports * 16
             *   [eax + 0x502660] → array value
             *   idiv 3 → final X offset */
            int xOffset = g_charSelViewportPos[g_numViewports * 4 + p] / 3;
            RenderCharacterOnPodium(
                xOffset,                 /* EAX: X position offset */
                s_charAngleTable[charId],/* EDX: per-character Y offset */
                modelZOffset,            /* EBX: camera distance */
                0xF00,                   /* ECX: head orientation */
                g_modelRotation,         /* [ebp+8]: turntable rotation */
                0,                       /* [ebp+C]: angleE */
                playerPtr                /* [ebp+10]: player struct */
            );
        }
        

        /* Character icon strip — 9 icons */
        
        /* First icon: always the selected/scroll character */
        int firstCharPos = g_charPositionTable[0] * 2;
        DrawTexturedQuad(firstCharPos, 0x168, 0x43480000, 0x38, 0x50,
                         g_uiTexPage + 1, 0xAC, g_menuScrollX, 0x1C, 0x28, VERTEX_WHITE);

        /* Remaining 8 characters (lines 45433-45449)
         * Original: iVar5=8, iVar2=4(charIdx*4), loop 8 times.
         * UV from g_charIconUV[ci*2] and [ci*2+1] (offset by iVar5=ci*8). */
        /* Binary 0x48D780-0x48D890. Loop ci = 1..8 (0x48d853 `cmp edx,9`).
         * Two draws per icon:
         *
         * 1. Portrait, or a placeholder for never-seen characters. The branch
         *    is at the loop TOP, 0x48d858 `mov ebx,[esi+0x8fba64]` /
         *    `test ebx,ebx` / `jne 0x48d7a1`:
         *      unlock != 0 → 0x48d7a1, UV = g_charIconUV[ci*2], [ci*2+1]
         *      unlock == 0 → 0x48d866, UV = (0xE4, placeholderY)
         *    placeholderY is a RUNNING COUNTER, not a table lookup: seeded
         *    0xffffff60 (-0xA0) at 0x48d791 and stepped +0x28 per iteration at
         *    0x48d840, so it equals 0x28*ci - 0xC8. Both paths converge at
         *    0x48d7ce and share the DrawTexturedQuad call.
         *
         * 2. Lock overlay when unlock != 2 (0x48d7d5). UV chosen by 0x48d7de
         *    `cmp [ebp-0x48], 4`: ci == 4 → (0xC8, 0xC8), else (0xE4, 0xA0).
         *
         * Both were wrong before. The placeholder Y was taken from
         * g_charIconUV[ci*2+1], one 0x28 row BELOW the running counter for
         * every ci — so locked slots drew a neighbouring placeholder, and
         * Egg Robo (ci 8) asked for 0xA0, past the end of the placeholder
         * column, giving the black rectangle in the far-right slot. The
         * overlay was fixed at (0xE4, 0xC8), which is neither of the two the
         * binary uses. */
        for (int ci = 1; ci < 9; ci++) {
            int iconUvX, iconUvY;
            if (g_charUnlockTable[ci] == 0) {            /* 0x48d85e */
                /* Never-seen: grey silhouette from the 0xE4 column.
                 *
                 * The binary does NOT index g_charIconUV here (0x48d866 path).
                 * uvY is a running counter: seeded 0xffffff60 (-0xA0) at
                 * 0x48d791, stepped +0x28 per iteration at 0x48d840, so it is
                 * 0x28*ci - 0xC8. That is one 0x28 row ABOVE the portrait row,
                 * because the silhouette column has no entry for Eggman (he
                 * starts at unlock state 1, never 0).
                 *
                 * SCHAR00.RAW column 0xE4 holds silhouettes at Y = 0x00, 0x28,
                 * 0x50, 0x78 and then X-marks at 0xA0 (on black) and 0xC8 (on
                 * green). Reusing g_charIconUV[ci*2+1] here shifted every
                 * locked slot down one row and sent Egg Robo (ci 8) to 0xA0 —
                 * the black-backed X — which rendered as a black rectangle in
                 * the far-right slot. */
                iconUvX = 0xE4;                          /* 0x48d86f */
                iconUvY = 0x28 * ci - 0xC8;              /* 0x48d791 + 0x48d840 */
            }
            else {
                iconUvX = g_charIconUV[ci * 2];          /* 0x48d7ae */
                iconUvY = g_charIconUV[ci * 2 + 1];      /* 0x48d7a7 */
            }
            int charPos = g_charPositionTable[ci] * 2;
            DrawTexturedQuad(charPos, 0x168, 0x43480000, 0x38, 0x50,
                             g_uiTexPage + 1, iconUvX, iconUvY, 0x1C, 0x28, VERTEX_WHITE);

            /* Lock overlay for non-unlocked characters — 0x48d7d5.
             *
             * Two different X graphics, chosen at 0x48d7de by `cmp [ebp-0x48],4`
             * on the loop counter. The counter IS the charId — esi is seeded to
             * 4 at 0x48d782 and indexes g_charUnlockTable by byte offset — so
             * ci == 4 is Eggman, who gets the cyan X under the Egg Robo portrait
             * at (0xC8, 0xC8); everyone else gets the red X at (0xE4, 0xA0).
             *
             * Both sit on an OPAQUE BLACK background in SCHAR00.RAW and are
             * drawn ADDITIVE, which is what the call's 11th argument selects:
             * 0x20 here versus 0 for the portrait draw above. Blit2DSprite
             * stores it as the sprite's flags word (0x44c4db: or ah,0x80 /
             * mov [ebx+0x74],ax). Adding black contributes nothing, so only the
             * X reaches the portrait underneath.
             *
             * There is a third copy of the red X at (0xE4, 0xC8) on a green
             * colour-key background. Nothing in the binary reads it. */
            if (g_charUnlockTable[ci] != 2) {
                int xUvX = (ci == 4) ? 0xC8 : 0xE4;      /* 0x48d7e4 / 0x48d7f2 */
                int xUvY = (ci == 4) ? 0xC8 : 0xA0;
                R_PushState();
                R_SetBlendMode(R_BLEND_ADDITIVE);
                DrawTexturedQuad(charPos, 0x168, 0x433E0000, 0x38, 0x50,
                                 g_uiTexPage + 1, xUvX, xUvY, 0x1C, 0x28, VERTEX_WHITE);
                R_PopState();
            }
        }
        
        /* Header bars */
        DrawTexturedQuad(0, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0, 0xA0, 0x20, VERTEX_WHITE);
        DrawTexturedQuad(0x140, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0x20, 0xA0, 0x20, VERTEX_WHITE);

        /* Per-player indicators — blink when confirmed */
        int blinkPhase = (g_totalFrames >> 1) & 1;

        if (g_numViewports > 3 && (g_p4Confirmed == 0 || blinkPhase)) {
            int xp = (VP_ANIM(3) - 3) * 2;
            DrawTexturedQuad(xp, 0x148, 0x43340000, 0x44, 0x90,
                             g_uiTexPage + 1, 0x66, 0x40, 0x22, 0x48, VERTEX_WHITE);
        }
        if (g_numViewports > 2 && (g_p3Confirmed == 0 || blinkPhase)) {
            int xp = (VP_ANIM(2) - 3) * 2;
            DrawTexturedQuad(xp, 0x148, 0x43340000, 0x44, 0x90,
                             g_uiTexPage + 1, 0x44, 0x40, 0x22, 0x48, VERTEX_WHITE);
        }
        if (g_numViewports > 1 && (g_p2Confirmed == 0 || blinkPhase)) {
            int xp = (VP_ANIM(1) - 3) * 2;
            DrawTexturedQuad(xp, 0x148, 0x43340000, 0x44, 0x90,
                             g_uiTexPage + 1, 0x22, 0x40, 0x22, 0x48, VERTEX_WHITE);
        }
        if (g_p1Confirmed == 0 || blinkPhase) {
            int xp = (VP_ANIM(0) - 3) * 2;
            DrawTexturedQuad(xp, 0x148, 0x43340000, 0x44, 0x90,
                             g_uiTexPage + 1, 0, 0x40, 0x22, 0x48, VERTEX_WHITE);
        }

        RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin, 0x48DA2A */
        EndFrame();
        FlipD3D();

        g_totalFrames2++;
        g_totalFrames++;
        WaitForFrameCap();
    } while (1);

    #undef VP_CHAR
    #undef VP_CURSORY
    #undef VP_TARGET
    #undef VP_ANIM
}
