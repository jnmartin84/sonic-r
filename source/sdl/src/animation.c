/**
 * animation.c — Character animation state machine
 *
 * TickPlayerAnimation selects the animation ID based on player state,
 * then advances the animation frame pointer.
 * See TickPlayerAnimation_annotated.c for full documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

#define IABS(x) ({ int _v = (x); int _s = _v >> 31; (_v ^ _s) - _s; })

/* Animation IDs */
#define ANIM_IDLE           0
#define ANIM_JUMPING        1
#define ANIM_STILL          2
#define ANIM_TURN_LEFT      3
#define ANIM_TURN_RIGHT     4
#define ANIM_BRAKING        5
/* 6 and 7 are the outcome poses. The results screen is unambiguous about which
 * is which — it gives 7 to `racePosition != 1` and 6 to the winner — and
 * charsel uses 6 as its confirm pose. They used to be ANIM_WIN_POSE /
 * ANIM_LOSE_POSE, which had 7 (the slumped one) reading as a celebration.
 * Both double up: 6 is also the generic pre-race stance, and 7 also fires on
 * FINISH_DONE, so neither name is perfect — outcome is the reliable axis. */
#define ANIM_WIN_POSE       6
#define ANIM_LOSE_POSE      7
/* 8/9 are the hard-lean-on-one-leg poses the turn animations blend into, not
 * "tricks" as previously named — confirmed by the user from the game. */
#define ANIM_LEAN_LEFT      8
#define ANIM_LEAN_RIGHT     9
#define ANIM_LANDING        10
#define ANIM_SPECIAL        11
#define ANIM_BOOST          12
#define ANIM_CHAR_SPECIFIC  13
#define ANIM_FALLING        14
#define ANIM_HIGHSPEED      15
#define ANIM_INVALID        -1
#define ANIM_INTRO          17

/* externs */
extern intptr_t g_animDescTable[];  /* 0x004FF5A8 */
 
/* side-storage arrays for (player+0x9C, anim-data cursor).
 * (declared in globals_extra.c). */

/* g_animRegCount (0x00676FE8, track_anim_init.c) */
extern int g_animRegCount;        /* canonical in track_anim_init.c */
/* Overlay array at 0x676FF0: 40 bytes per entry
 * [+0x00] = target vertex pointer
 * [+0x04..+0x20] = 8 base offsets (4 pairs of x,y)
 * [+0x24] = pointer to animation data (anim[+0x24] = changed flag, [+0x1c]=dx, [+0x20]=dy)
 */
/* g_animOverlayArray is the same memory as g_animRegTable (0x00676FF0, track_anim_init.c) */

extern void RelocateModelVertices(int *arr, int count, int newBase, int oldBase, int fileBase);

/* statics */

/* Animation priority tables for Amy's char-specific arbiter (binary 0x4FAC2C / 0x4FAC40).
 * s_animPrioNew[newAnim] vs s_animPrioCur[curAnim]: if the CURRENT animation outranks the
 * tentative new one, the current animation is kept. This locks Amy's water pose (special=11
 * routes through these) and boost from being interrupted by idle/turning. */
static const unsigned char s_animPrioNew[18] = { 0,0,0,0,0,5,0,0,0,0,5,5,0,0,0,0,0,0 }; /* 0x4FAC2C — brake/landing/special */
static const unsigned char s_animPrioCur[18] = { 0,0,0,0,0,0,0,0,0,0,0,0,5,5,0,0,0,0 }; /* 0x4FAC40 — boost/char_specific */

#define E(f,t,a,b,c,d,e2,f2,g,h) {(f),(t),(a),(b),(c),(d),(e2),(f2),(g),(h)}
#define SENT {-1,0,0,0,0,0,0,0,0,0}

static const int s_faceAnim_0_A0[][10] = {E(98,0,159,0,128,0,128,31,159,31),E(99,0,223,0,192,0,192,31,223,31),E(101,0,31,0,0,0,0,31,31,31),E(102,0,95,0,64,0,64,31,95,31),SENT};
static const int s_faceAnim_0_B0[][10] = {E(104,0,0,0,31,0,31,31,0,31),E(105,0,64,0,95,0,95,31,64,31),E(107,0,128,0,159,0,159,31,128,31),E(108,0,223,0,192,0,192,31,223,31),SENT};
static const int s_faceAnim_0_A1[][10] = {E(98,1,159,192,128,192,128,223,159,223),E(99,1,255,192,224,192,224,223,255,223),E(101,1,31,192,0,192,0,223,31,223),E(102,1,95,192,64,192,64,223,95,223),SENT};
static const int s_faceAnim_0_B1[][10] = {E(104,1,0,192,31,192,31,223,0,223),E(105,1,64,192,95,192,95,223,64,223),E(107,1,128,192,159,192,159,223,128,223),E(108,1,255,192,224,192,224,223,255,223),SENT};
static const int s_faceAnim_1_A0[][10] = {E(78,0,96,191,96,160,127,160,127,191),E(79,0,96,159,96,128,127,128,127,159),SENT};
static const int s_faceAnim_1_B0[][10] = {E(83,0,127,191,127,160,96,160,96,191),E(76,0,96,159,127,159,127,128,96,128),SENT};
static const int s_faceAnim_1_A1[][10] = {E(78,1,192,255,192,224,223,224,223,255),E(79,1,160,223,160,192,191,192,191,223),SENT};
static const int s_faceAnim_1_B1[][10] = {E(83,1,223,255,223,224,192,224,192,255),E(76,1,160,223,191,223,191,192,160,192),SENT};
static const int s_faceAnim_2_A0[][10] = {E(102,0,128,64,175,64,175,95,128,95),SENT};
static const int s_faceAnim_2_B0[][10] = {E(122,0,175,95,175,64,128,64,128,95),SENT};
static const int s_faceAnim_2_A1[][10] = {E(102,1,128,224,175,224,175,255,128,255),SENT};
static const int s_faceAnim_2_B1[][10] = {E(122,1,175,255,175,224,128,224,128,255),SENT};
static const int s_faceAnim_3_A0[][10] = {E(101,1,0,0,31,0,31,31,0,31),SENT};
static const int s_faceAnim_3_B0[][10] = {E(104,1,31,0,0,0,0,31,31,31),SENT};
static const int s_faceAnim_3_A1[][10] = {E(101,1,0,224,31,224,31,255,0,255),SENT};
static const int s_faceAnim_3_B1[][10] = {E(104,1,31,224,0,224,0,255,31,255),SENT};
static const int s_faceAnim_5_A0[][10] = {E(101,0,191,32,160,32,160,63,191,63),E(103,0,159,32,128,32,128,63,159,63),SENT};
static const int s_faceAnim_5_B0[][10] = {E(107,0,128,32,159,32,159,63,128,63),E(110,0,160,32,191,32,191,63,160,63),SENT};
static const int s_faceAnim_5_A1[][10] = {E(101,1,255,160,224,160,224,191,255,191),E(103,1,223,160,192,160,192,191,223,191),SENT};
static const int s_faceAnim_5_B1[][10] = {E(107,1,192,160,223,160,223,191,192,191),E(110,1,224,160,255,160,255,191,224,191),SENT};
static const int s_faceAnim_7_A0[][10] = {E(83,0,175,96,128,96,128,127,175,127),SENT};
static const int s_faceAnim_7_B0[][10] = {E(107,0,175,127,128,127,128,96,175,96),SENT};
static const int s_faceAnim_7_A1[][10] = {E(83,0,47,96,0,96,0,127,47,127),SENT};
static const int s_faceAnim_7_B1[][10] = {E(107,0,47,127,0,127,0,96,47,96),SENT};
static const int s_faceAnim_9_A0[][10] = {E(101,1,63,136,32,136,32,191,63,191),E(111,1,31,191,31,136,0,136,0,191),SENT};
static const int s_faceAnim_9_B0[][10] = {E(108,1,32,136,63,136,63,191,32,191),E(105,1,0,136,31,136,31,191,0,191),SENT};
static const int s_faceAnim_9_A1[][10] = {E(101,1,127,200,96,200,96,255,127,255),E(111,1,63,255,63,200,32,200,32,255),SENT};
static const int s_faceAnim_9_B1[][10] = {E(108,1,96,200,127,200,127,255,96,255),E(105,1,32,200,63,200,63,255,32,255),SENT};

#undef E
#undef SENT

static const int * const s_faceAnimTable[10][4] = {
    { s_faceAnim_0_A0[0], s_faceAnim_0_B0[0], s_faceAnim_0_A1[0], s_faceAnim_0_B1[0] },
    { s_faceAnim_1_A0[0], s_faceAnim_1_B0[0], s_faceAnim_1_A1[0], s_faceAnim_1_B1[0] },
    { s_faceAnim_2_A0[0], s_faceAnim_2_B0[0], s_faceAnim_2_A1[0], s_faceAnim_2_B1[0] },
    { s_faceAnim_3_A0[0], s_faceAnim_3_B0[0], s_faceAnim_3_A1[0], s_faceAnim_3_B1[0] },
    { NULL, NULL, NULL, NULL },
    { s_faceAnim_5_A0[0], s_faceAnim_5_B0[0], s_faceAnim_5_A1[0], s_faceAnim_5_B1[0] },
    { NULL, NULL, NULL, NULL },
    { s_faceAnim_7_A0[0], s_faceAnim_7_B0[0], s_faceAnim_7_A1[0], s_faceAnim_7_B1[0] },
    { NULL, NULL, NULL, NULL },
    { s_faceAnim_9_A0[0], s_faceAnim_9_B0[0], s_faceAnim_9_A1[0], s_faceAnim_9_B1[0] },
};

/* globals */

/* Per-character previous glow state. Shared (non-static): the race animators
 * in render_character_d3d.c (0x421968 / 0x4219FC / 0x421A90) shift from the
 * same values, and ResultsScreen
 * (0x4c72e8-0x4c7378) reads the same 0x4FBE48/4C/50 globals to reset the
 * glow UV frame on results-screen entry. */
int g_glowPrevChar5 = 0x600000;  /* 0x004FBE48 */
int g_glowPrevChar7 = 0x600000;  /* 0x004FBE4C */
int g_glowPrevChar8 = 0x600000;  /* 0x004FBE50 */

/* functions */

/**
 * TickPlayerAnimation — 0x00421B3C — 2306 bytes
 *
 * Character animation state machine. Selects animation frame based on
 * player velocity, terrain, abilities, and game state.
 * Called once per player per frame.
 *
 * player = in_EAX (byte pointer to player struct)
 */
void TickPlayerAnimation(Player *player)
{
    short charId = player->charId;

    /* Look up character animation table */
    void **animTables = (void **)g_charAnimTables;
    if (animTables == NULL) {
        return;
    }

    /* 10 characters max */
    if (charId < 0 || charId >= CHAR_COUNT) {
        return;
    }

    void *animTable = animTables[charId * 2];

    /* Player slot — indexes g_animDataPtrs[] 64-bit side storage for the live
     * animation cursor (binary player+0x9c). Needed by the blend transitions
     * below, so resolve and bounds-check up front. */
    int playerSlot = (int)(player - g_playerBase);
    if (playerSlot < 0 || playerSlot > 4) {
        return;
    }

    int anim = player->animId;  /* P_INT(p, 0x96) >> 16 = short at 0x98 */

    int vehicleMode = player->_unk_0x78;

    if (vehicleMode == 0) {
        /* ON-FOOT CHARACTER */
        int grounded = player->groundedFlag;

        if (grounded == 1) {
            /* RUNNING — faithful to binary 0x421b92-0x421f37 including the blend
             * transitions. at[i] = animTable[i] = stream-base pointer for animId i;
             * the live cursor lives in g_animDataPtrs[playerSlot] (64-bit side
             * storage), so (cursor - base) pointer arithmetic is already in short
             * units (the binary computed (bytes)/2), and base + (K - frameIdx)
             * shorts == the binary's base + (K-frameIdx)*2 bytes. */
            const uintptr_t *at = (const uintptr_t *)animTable;
            int absVelX, absVelZ;

            /* Transition overrides (0x421b92) */
            if (anim == ANIM_JUMPING || anim == ANIM_HIGHSPEED ||
                (anim == ANIM_CHAR_SPECIFIC && (charId == CHAR_TAILS || charId == CHAR_KNUCKLES))) {
                player->_unk_0x9A = 0x201;
                anim = ANIM_IDLE;
            }

            /* Braking detection (0x421bc3) */
            absVelX = IABS(player->forwardSpeed);
            absVelZ = IABS(player->lateralSpeed);
            if (absVelZ < absVelX && player->forwardSpeed < -0x4000) {
                anim = ANIM_BRAKING;
            }

            /* Forward acceleration cancels braking (0x421be9) */
            if (anim != ANIM_IDLE && absVelZ < absVelX && player->forwardSpeed > 0) {
                player->_unk_0x9A = 0x201;
                anim = ANIM_IDLE;
            }

            /* Boost detection + boost-keep (0x421c16, non-Amy/Eggman) */
            if (charId < CHAR_AMY || charId > CHAR_EGGMAN) {
                int maxSpeed = g_charStatsTable[charId * 10]; /* +0x00 */
                if (anim != ANIM_BOOST &&
                    ((absVelZ < absVelX && (maxSpeed * 15) / 16 < player->forwardSpeed) ||
                     (player->_unk_0x6E != 0 && player->brakeCounter == -1))) {
                    anim = ANIM_BOOST;
                    player->_unk_0x9A = 0x201;
                }
                /* boost-keep (0x421cac): stay in boost while still fast enough */
                if (player->prevAnimId == ANIM_BOOST &&
                    (maxSpeed * 3) / 4 < player->forwardSpeed) {
                    anim = ANIM_BOOST;
                }
            }

            /* Long fall (0x421cf5) */
            if (player->brakeCounter > 4) {
                anim = ANIM_FALLING;
            }

            /* Standing still (0x421d05) */
            if (player->forwardSpeed == 0 && player->lateralSpeed == 0) {
                anim = ANIM_STILL;
            }

            /* Turn -> trick blend, left (0x421d16) — quick turn-and-release flick */
            if (player->prevAnimId == ANIM_LEAN_LEFT ||
                (player->prevAnimId == ANIM_TURN_LEFT && anim != ANIM_TURN_LEFT)) {
                if (player->prevAnimId == ANIM_TURN_LEFT) {
                    int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_TURN_LEFT]);
                    player->animId     = ANIM_LEAN_LEFT;
                    player->prevAnimId = ANIM_LEAN_LEFT;
                    g_animDataPtrs[playerSlot] = (const short *)at[ANIM_LEAN_LEFT] + (5 - fi);
                }
                anim = ANIM_LEAN_LEFT;
            }

            /* Turn -> trick blend, right (0x421d7a) */
            if (player->prevAnimId == ANIM_LEAN_RIGHT ||
                (player->prevAnimId == ANIM_TURN_RIGHT && anim != ANIM_TURN_RIGHT)) {
                if (player->prevAnimId == ANIM_TURN_RIGHT) {
                    int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_TURN_RIGHT]);
                    player->animId     = ANIM_LEAN_RIGHT;
                    player->prevAnimId = ANIM_LEAN_RIGHT;
                    g_animDataPtrs[playerSlot] = (const short *)at[ANIM_LEAN_RIGHT] + (5 - fi);
                }
                anim = ANIM_LEAN_RIGHT;
            }

            /* Hard turns (0x421de5 / 0x421e16) */
            if (absVelX < absVelZ && player->lateralSpeed > 0x4000 &&
                player->brakeCounter != -1) {
                anim = ANIM_TURN_LEFT;
            }

            if (absVelX < absVelZ && player->lateralSpeed < -0x4000 &&
                player->brakeCounter != -1) {
                anim = ANIM_TURN_RIGHT;
            }

            /* Runner spin/special + jump + landing blends (0x421e47, charId<3 only) */
            if (charId < CHAR_AMY) {
                int kc = (charId == CHAR_KNUCKLES) ? 6 : 5;   /* per-char blend offset */

                /* stay in special, or blend landing -> special (0x421e57) */
                if (player->prevAnimId == ANIM_SPECIAL) anim = ANIM_SPECIAL;
                if (player->prevAnimId == ANIM_LANDING && anim != ANIM_LANDING) {
                    int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_LANDING]);
                    player->animId = ANIM_SPECIAL;
                    player->prevAnimId = ANIM_SPECIAL;
                    anim = ANIM_SPECIAL;
                    g_animDataPtrs[playerSlot] = (const short *)at[ANIM_SPECIAL] + (kc - fi);
                }

                /* jump (0x421ed2) */
                if (player->_unk_0x86 >= 1) {
                    anim = ANIM_JUMPING;
                }

                /* landing (0x421ee5): special -> landing cursor blend; anim=landing either way */
                if (player->_unk_0x86 == 2) {
                    if (player->prevAnimId == ANIM_SPECIAL) {
                        int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_SPECIAL]);
                        player->animId = ANIM_LANDING;
                        player->prevAnimId = ANIM_LANDING;
                        g_animDataPtrs[playerSlot] = (const short *)at[ANIM_LANDING] + (kc - fi);
                    }
                    anim = ANIM_LANDING;
                }
            }

            /* Fallback */
            if (anim == ANIM_INVALID) {
                anim = ANIM_IDLE;
            }
        }
        else {
            /* NOT RUNNING — high speed ground check */
            if ((anim == ANIM_IDLE || anim == ANIM_BOOST) &&
                (charId < CHAR_AMY || charId > CHAR_METAL_SONIC) &&
                player->velY > 0x18000) {
                anim = ANIM_HIGHSPEED;
            }
            if (anim == ANIM_INVALID) {
                anim = ANIM_IDLE;
            }
        }

        /* Character-specific overrides */
        switch (charId) {
        case 1: /* Tails */
            if (player->abilityState == 1) {
                anim = ANIM_CHAR_SPECIFIC;
            }
            break;
        case 2: /* Knuckles */
            if (player->abilityState == 4) {
                anim = ANIM_CHAR_SPECIFIC;
            }
            break;
        case 3: { /* Amy — char-specific animation handler (binary 0x42203d-0x4221e6).
                   * Over water UpdatePlayerPhysicsB sets abilityState=2; Amy's car then
                   * transforms (wheels rotate flat like floats) by driving her special(11)/
                   * boost(12)/landing(10)/char_specific(13) animations, with mid-stream cursor
                   * blends so transitions don't pop. esi+i*4 in the binary = animTable[i] =
                   * the stream-base pointer for animId i; the live cursor lives in
                   * g_animDataPtrs[slot] (64-bit side storage), so cursor-minus-base pointer
                   * arithmetic is already in short units (binary did (bytes)/2). */
            const uintptr_t *at = (const uintptr_t *)animTable;

            /* abilityState==2 -> special(11); blend in from boost(12)  (0x42203d) */
            if (player->abilityState == 2) {
                anim = ANIM_SPECIAL;
                if (player->prevAnimId == ANIM_BOOST) {
                    int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_BOOST]);
                    player->prevAnimId = ANIM_SPECIAL;
                    player->animId = ANIM_SPECIAL;
                    g_animDataPtrs[playerSlot] = (const short *)at[ANIM_SPECIAL] + (0x10 - fi);
                }
            }
            /* abilityState==3 -> landing(10); blend in from char_specific(13)  (0x42209c) */
            if (player->abilityState == 3) anim = ANIM_LANDING;
            if (player->prevAnimId == ANIM_CHAR_SPECIFIC) {
                int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_CHAR_SPECIFIC]);
                player->prevAnimId = ANIM_LANDING;
                player->animId = ANIM_LANDING;
                g_animDataPtrs[playerSlot] = (const short *)at[ANIM_LANDING] + (0xc - fi);
            }
            /* prevAnimId==special(11), not already special -> boost(12) blend  (0x4220fb) */
            if (player->prevAnimId == ANIM_SPECIAL && anim != ANIM_SPECIAL) {
                int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_SPECIAL]);
                if (fi > 0x10) {
                    fi = 0x10;
                }
                anim = ANIM_BOOST;
                player->prevAnimId = ANIM_BOOST;
                player->animId = ANIM_BOOST;
                g_animDataPtrs[playerSlot] = (const short *)at[ANIM_BOOST] + (0x10 - fi);
            }
            /* prevAnimId==landing(10), not landing/special -> char_specific(13) blend (0x42215e) */
            if (player->prevAnimId == ANIM_LANDING &&
                anim != ANIM_LANDING && anim != ANIM_SPECIAL) {
                int fi = (int)(g_animDataPtrs[playerSlot] - (const short *)at[ANIM_LANDING]);
                if (fi > 0xc) {
                    fi = 0xc;
                }
                anim = ANIM_CHAR_SPECIFIC;
                player->prevAnimId = ANIM_CHAR_SPECIFIC;
                player->animId = ANIM_CHAR_SPECIFIC;
                g_animDataPtrs[playerSlot] = (const short *)at[ANIM_CHAR_SPECIFIC] + (0xc - fi);
            }
            /* priority arbiter (0x4221c6): keep the current animation if it outranks the new */
            int curAnim = player->animId;
            int curPrio = (curAnim >= 0 && curAnim < 18) ? s_animPrioCur[curAnim] : 0;
            int newPrio = (anim >= 0 && anim < 18) ? s_animPrioNew[anim] : 0;
            if (curPrio > newPrio) {
                anim = curAnim;
            }
            break;
        }
        case 4: /* Eggman */
            if (anim == ANIM_TURN_LEFT || anim == ANIM_TURN_RIGHT || anim == ANIM_BRAKING) {
                anim = player->prevAnimId;
            }
            break;
        case 6: /* Metal Sonic */
            if (player->abilityState == 10) {
                player->_unk_0x9A = 0x201;
                anim = ANIM_IDLE;
            }
            if (anim == ANIM_TURN_LEFT || anim == ANIM_TURN_RIGHT) {
                anim = player->prevAnimId;
            }
            break;
        }
    }
    else {
        /* VEHICLE MODE — limited animation */
        if (anim == 0) {
            player->_unk_0x9A = 0x201;
        }
        else {
            anim = ANIM_IDLE;
        }
    }

    /* Post-race / intro overrides (highest priority) */
    short finalAnim = (short)anim;

    if (g_postRaceCameraMode != 0) {
        if (g_isMultiRace == 0) {
            if (g_raceType == RACE_SPECIAL) {
                /* 0x422247: mov ecx, 0x11 — then `jmp 0x42227e`, which skips
                 * the placement test below. The challenge race has no
                 * placement yet, and the rival still carries racePosition 5
                 * from AdvanceGrandPrixTrack (race_setup.c), so running the
                 * test here put him in the losing pose for the whole
                 * PREPARE TO CHALLENGE scene while the human looked fine. */
                finalAnim = ANIM_INTRO;
            }
            else if (player->racePosition >= g_playerCount + 4) {
                finalAnim = ANIM_LOSE_POSE;   /* 0x422279: ecx = 7, signed jge */
            }
            else {
                finalAnim = ANIM_WIN_POSE;    /* 0x422260: ecx = 6 */
            }
        }
        else {
            finalAnim = (player->racePosition < 2) ? ANIM_WIN_POSE : ANIM_LOSE_POSE;
        }
    }

    /* Intro countdown — binary 0x42227E-0x4222AC.
     * Human player (player1) or multiplayer (raceType==1): only force
     * ANIM_INTRO during the flyover camera phase (introCountdown > 0x3C).
     * During READY/SET/GO (0 < countdown <= 0x3C), the computed animation
     * plays — so the player can see their spin dash charge animation.
     * AI players: force ANIM_INTRO for the entire countdown, except TAG. */
    if (player == g_playerBase || g_raceType == RACE_MULTIPLAYER) {
        if (g_introCountdown > 0x3C) {
            finalAnim = ANIM_INTRO;
        }
    }
    else {
        if (g_introCountdown > 0 && g_raceSubMode != SUBMODE_TAG) {
            finalAnim = ANIM_INTRO;
        }
    }

    /* Crossed finish line */
    if (player->finishState == FINISH_DONE) {
        finalAnim = ANIM_LOSE_POSE;
    }

    /* Apply animation and advance frame */
    player->animId = finalAnim;

    /* If animation changed, look up new frame stream.
     * animTable is a uintptr_t array (64-bit pointers to frame streams).
     * Store the frame stream pointer using intptr_t for 64-bit safety.
     * 
     * 64-bit pointer side table: can't store 8-byte pointers in 4-byte
     * player struct fields (p+0x9C overlaps p+0xA0). Use per-player
     * side storage indexed by player slot (playerSlot resolved at top).
     *
     * Binary 0x4222C4-0x422300: animation changed → lookup frame stream.
     * Uses animId (player+0x96 >> 16) to index the table, NOT finalAnim.
     * Binary has no bounds check (overflows into adjacent table, reads NULL).
     * 0x1F0 is set UNCONDITIONALLY afterwards (binary 0x422309). */
    if (finalAnim != player->prevAnimId) {
        uintptr_t *animPtrs = (uintptr_t *)animTable;
        int animIdx = player->animId;  /* P_INT(p, 0x96) >> 16 = short at 0x98 */
        int animCount = (int)(uintptr_t)((void **)g_charAnimTables)[charId * 2 + 1];
        if (animIdx >= 0 && animIdx < animCount) {
            uintptr_t frameAddr = animPtrs[animIdx];
            g_animDataPtrs[playerSlot] = (const short *)frameAddr;
            if (frameAddr == 0 && animIdx + 1 < animCount) {
                frameAddr = animPtrs[animIdx + 1];
                g_animDataPtrs[playerSlot] = (const short *)frameAddr;
            }
        }
        /* Binary OOB read returns NULL for ANIM_INTRO (17) on smaller tables;
         * frame pointer stays whatever it was — matches binary behavior. */
        player->prevAnimId = finalAnim;
    }

    player->_unk_0x1F0 = 1;   /* always set — binary 0x422309 */

    /* Speed-dependent animation rate — binary 0x422315-0x422368.
     * When animId == 0 (IDLE/running), _unk_0x9A accumulates
     * forwardSpeed/256 each frame. When it exceeds 0x200, one frame
     * advance happens and it wraps. Below 0x200, no frame advance
     * occurs. This makes running animation speed proportional to
     * movement speed. */
    if (player->animId == 0) {
        int accum = (int)player->_unk_0x9A + (player->forwardSpeed >> 8);
        player->_unk_0x9A = (short)accum;
        if (player->_unk_0x9A < 0) {
            player->_unk_0x9A = 0;
        }

        if (player->_unk_0x9A > 0x200) {
            *((unsigned char *)&player->_unk_0x9A + 1) &= 1;  /* 0x422355 */
        }
        else {
            player->_unk_0x1F0 = 0;
            goto epilogue;
        }
    }

    /* Read next frame from stream */
    const short *framePtr = g_animDataPtrs[playerSlot];
    if (framePtr == NULL) {
        return;
    }
    g_animDataPtrs[playerSlot] = framePtr + 1;
    int frame = (int)*framePtr;
    player->_unk_0x1C = frame;

    /* Frame with trigger data (upper bits > 0xFFF) */
    if (frame > 0xFFF) {
        short trigger = (short)(frame >> 12);
        player->_unk_0x1C &= 0xFFF;

        if (player->yOffset == 0 && player->sfxTrigger == (short)-1) {
            short soundId = trigger + 2;
            if (player->dynamicSpeedMode != 0) {
                soundId = trigger + 0x11;
            }
            player->sfxTrigger = soundId;
        }
    }

    /* Loop marker (-1) */
    if (player->_unk_0x1C == -1) {
        framePtr = g_animDataPtrs[playerSlot];
        framePtr = framePtr + -(*framePtr);
        g_animDataPtrs[playerSlot] = framePtr + 1;
        player->_unk_0x1C = (int)*framePtr;
    }

    /* End animation marker (-2) */
    if (player->_unk_0x1C == -2) {
        framePtr = g_animDataPtrs[playerSlot];
        framePtr = framePtr + -(*framePtr);
        g_animDataPtrs[playerSlot] = framePtr + 1;
        player->animId = (short)0xFFFF;
        player->_unk_0x1C = (int)*framePtr;
    }

    player->animFrameIdx = player->_unk_0x1C - 1;

epilogue:
    player->prevAnimId = player->animId;
}

/* =====================================================================
 * AnimateVehicleGlow — FUN_004217f8 — 365 bytes
 *
 * Frame-based UV animation for vehicle character glow effects.
 * Checks charId at player+0xF2 for characters 5 (Metal Sonic),
 * 7 (Metal Knuckles), 8 (Eggman). Each branch:
 *   - Selects glow size from g_totalFrames % 3 (0x10 or 0x20)
 *   - Computes UV base from g_totalFrames & 3
 *   - Looks up polygon start from g_modelMeta[]
 *   - Calls RelocateModelVertices to shift polygon UVs
 *
 * EAX = player pointer (Watcom fastcall).
 *
 * Per-character state stored in ROM-init globals:
 *   0x4FBE48 (char 5), 0x4FBE4C (char 7), 0x4FBE50 (char 8)
 * Model polygon data at 0x713E68 with stride 0x30 (48 bytes).
 * ===================================================================== */
void AnimateVehicleGlow(Player *player)
{
    short charId = player->charId;                                               /* movsx ecx, [eax+0xF2] */
    int phase = g_totalFrames % 3;                                               /* eax / 3, edx = remainder */

    int size = (phase == 0) ? 0x10 : 0x20;
    int newBase = (0x60 - size) << 16;
    int frameUV = (((g_totalFrames & 3) * 8) + 0x80) << 16;

    /* Metal Sonic: meta model 5, poly offset +0x30, count=4 */
    if (charId == CHAR_METAL_SONIC) {
        int polyStart = g_modelMeta[5].polyStart;                                /* [0x71323C] */
        int idx = polyStart + 0x30;
        int *arr = (int *)((char *)g_charFaceBase + idx * 0x30);
        int prevState = g_glowPrevChar5;
        RelocateModelVertices(arr, 4, frameUV, prevState, newBase);
        g_glowPrevChar5 = newBase;

    }
    /* Metal Knuckles: meta model 7, poly offset +0x1C, count=4 */
    else if (charId == CHAR_METAL_KNUCKLES) {
        int polyStart = g_modelMeta[7].polyStart;                                /* [0x7132DC] */
        int idx = polyStart + 0x1C;
        int *arr = (int *)((char *)g_charFaceBase + idx * 0x30);
        int prevState = g_glowPrevChar7;
        RelocateModelVertices(arr, 4, frameUV, prevState, newBase);
        g_glowPrevChar7 = newBase;

    }
    /* Eggman: meta model 8, poly offset +0x38, count=8 */
    else if (charId == CHAR_EGG_ROBO) {
        int polyStart = g_modelMeta[8].polyStart;                                /* [0x71332C] */
        int idx = polyStart + 0x38;
        int *arr = (int *)((char *)g_charFaceBase + idx * 0x30);
        int prevState = g_glowPrevChar8;
        RelocateModelVertices(arr, 8, frameUV, prevState, newBase);
        g_glowPrevChar8 = newBase;
    }
}

/* =====================================================================
 * TickAnimation — 0x0047de0c — 126 bytes - VALIDATED.
 * Advances an animation frame for the given animation index.
 * ROM data at 0x4ff5a8, 40-byte stride per animation entry:
 *   [+0x00] frameIndex, [+0x04] frameCount, [+0x08] countdown,
 *   [+0x0c] countdownReset, [+0x10] dataOffset, [+0x14] dataBase,
 *   [+0x1c] currentKeyframe, [+0x20] currentDuration, [+0x24] changedFlag
 * EAX = animation index
 * ===================================================================== */
void TickAnimation(int animIndex)         /* EAX */
{
    /* Entry in g_animDescTable. */
    intptr_t *a = g_animDescTable + animIndex * 10;

    int countdown = (int)a[2] - 1;        /* [+0x08] -- */
    a[2] = countdown;

    if (countdown >= 0) {
        a[9] = 0;                         /* [+0x24] = 0 (no change) */
        return;
    }

    /* Countdown expired — advance frame */
    a[2] = a[3];                          /* [+0x08] = [+0x0c] reset value */
    intptr_t dataOff = a[4] + 4;          /* [+0x10] advance data pointer */
    int frameIdx = (int)a[0] + 1;         /* [+0x00] ++ */
    a[4] = dataOff;
    a[0] = frameIdx;

    if (frameIdx == (int)a[1]) {
        /* Wrap around */
        int count = (int)a[1];            /* [+0x04] frameCount */
        a[0] = 0;
        a[4] = dataOff - count * 4;
    }

    /* Read keyframe data: [dataOffset] is an index, look up in dataBase */
    int *dataPtr = (int *)a[4];
    int keyIndex = *dataPtr;
    int *base = (int *)a[5];              /* [+0x14] dataBase */
    int *keyframe = base + keyIndex * 2;  /* 8-byte stride */

    a[7] = keyframe[0];                   /* [+0x1c] currentKeyframe */
    a[8] = keyframe[1];                   /* [+0x20] currentDuration */
    a[9] = 1;                             /* [+0x24] = 1 (frame changed) */
}


/* =====================================================================
 * ApplyAnimationOffsets — 0x0047de8c — 146 bytes - VALIDATED.
 * Iterates animation overlay entries (40-byte stride at 0x676ff0,
 * count at 0x676fe8). For each active entry, adds keyframe offset
 * values to 4 vertex pairs (8 output ints) at the target pointer.
 * No parameters.
 * ===================================================================== */
void ApplyAnimationOffsets(void)
{
    int count = g_animRegCount;
    intptr_t *entry = g_animRegTable;               /* 10 intptr_t per entry */
    int i;

    for (i = 0; i < count; i++, entry += 10) {
        intptr_t *animData = (intptr_t *)entry[9];  /* [+0x24] = anim descriptor ptr */
        if (animData[9] == 0) {
            continue;                               /* animData[+0x24] = changed flag, skip if 0 */
        }

        int dx = (int)animData[7];                  /* [+0x1c] */
        int dy = (int)animData[8];                  /* [+0x20] */
        int *target = (int *)entry[0];              /* [+0x00] = target vertex pointer */

        target[0] = dx + (int)entry[1];             /* vtx0.x = anim.dx + offset[0] */
        target[1] = dy + (int)entry[2];             /* vtx0.y = anim.dy + offset[1] */
        target[2] = dx + (int)entry[3];             /* vtx1.x */
        target[3] = dy + (int)entry[4];             /* vtx1.y */
        target[4] = dx + (int)entry[5];             /* vtx2.x */
        target[5] = dy + (int)entry[6];             /* vtx2.y */
        target[6] = dx + (int)entry[7];             /* vtx3.x */
        target[7] = dy + (int)entry[8];             /* vtx3.y */
    }

    g_animRegCount = count;                         /* write back (unchanged) */
}

/* FUN_0047df20 */
void UpdateIslandAnimations(void)
{
    TickAnimation(1);
    TickAnimation(2);
    ApplyAnimationOffsets();
}

/* FUN_00479b80 */
void UpdateFactoryAnimations(void)
{
    TickAnimation(7);
    ApplyAnimationOffsets();
}

/* FUN_0047a5f8 */
void UpdateRuinAnimations(void)
{
    TickAnimation(8);
    TickAnimation(9);
    TickAnimation(10);
    TickAnimation(11);
    ApplyAnimationOffsets();
}

/* =====================================================================
 * UpdateFaceUVAnimation — FUN_0047fd04 — 228 bytes
 *
 * Processes face UV animation entries for a character model.
 * ===================================================================== */
void UpdateFaceUVAnimation(Player *player, int *animList)
{
    short charId = player->charId;
    int faceBase = g_modelMeta[charId].polyStart;

    while (1) {
        int faceIdx = faceBase + animList[0];
        unsigned char *face = (unsigned char *)g_charFaceBase + faceIdx * 0x30;

        face[0x28] = (animList[1] == 0) ? ((unsigned char)g_tpageCharacters) : ((unsigned char)g_tpagePlayfield1);

        int i;
        for (i = 0; i < 8; i++) {
            int val = animList[i + 2];
            int shifted = val << 16;
            if (val & 1) {
                shifted += 0xFFFF;
            }
            *(int *)(face + i * 4) = shifted;
        }

        if (animList[10] == -1) {
            break;
        }
        animList += 10;
    }
}

/* =====================================================================
 * DispatchFaceUVAnimation — FUN_0047fea0 — 163 bytes — VALIDATED
 *
 * Selects face UV animation lists per character from ROM table,
 * calls UpdateFaceUVAnimation for each set.
 * ===================================================================== */
void DispatchFaceUVAnimation(Player *player)
{
    short charId = player->charId;

    if (charId == CHAR_EGGMAN || charId == CHAR_TAILS_DOLL || charId == CHAR_EGG_ROBO) {
        return;
    }

    if (charId < 0 || charId >= CHAR_COUNT) {
        return;
    }

    unsigned char flags = ((unsigned char *)&player->renderState)[2];

    int *listA = (int *)s_faceAnimTable[charId][(flags & 1) ? 2 : 0];
    if (listA) {
        UpdateFaceUVAnimation(player, listA);
    }

    int *listB = (int *)s_faceAnimTable[charId][(flags & 2) ? 3 : 1];
    if (listB) {
        UpdateFaceUVAnimation(player, listB);
    }
}

/* =====================================================================
 * AdvancePlayerAnimation — 0x004d9d48 — 136 bytes
 * Steps through a sequence of 12-bit angle values for a player.
 * Player stride = 1820 bytes. Reads shorts from data pointer at
 * player[+0x590], advances by 2. On -1 sentinel, wraps using count
 * at current position. Stores (value & 0xFFF) to [+0x510],
 * (value - 1) to [+0x6e0].
 * EAX = player index
 * ===================================================================== */
void AdvancePlayerAnimation(int playerIndex)                 /* EAX */
{
    /* g_playerBase declared in sonicr_globals.h */
    Player *pl = &g_playerBase[playerIndex];

    /* Binary offsets (absolute → struct offset):
     * 0x8FD590 → +0x9C  (anim data cursor pointer — 64-bit via g_animDataPtrs)
     * 0x8FD510 → +0x1C  (current angle value)
     * 0x8FD6E0 → +0x1EC (animFrameIdx = angle - 1) */

    /* Read data pointer from 64-bit side-storage, advance by one short */
    const short *dp = g_animDataPtrs[playerIndex];
    short val = *dp;
    dp++;
    g_animDataPtrs[playerIndex] = dp;

    pl->_unk_0x1C = (int)val;                                /* 0x8FD510 */

    if (val == -1) {
        /* Wrap: count at current position, rewind by count shorts */
        short count = *dp;
        dp -= count;
        g_animDataPtrs[playerIndex] = dp;
        val = *dp;
        dp++;
        g_animDataPtrs[playerIndex] = dp;
        pl->_unk_0x1C = (int)val;                            /* 0x8FD510 */
    }

    /* Mask to 12-bit angle, store current and previous */
    int angle = pl->_unk_0x1C & 0xFFF;                       /* 0x8FD510 */
    pl->_unk_0x1C = angle;
    pl->animFrameIdx = angle - 1;                            /* 0x8FD6E0 */
}