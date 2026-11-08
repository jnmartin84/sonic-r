/**
 * race_update.c — Per-frame race update functions
 *
 * Functions called from the WinMain race loop per frame.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "endian_util.h"

extern void UpdatePlayfieldGridPositions(void);
extern void UpdatePlayerMovement(Player *player);
extern void AdjustRubberBandAI(void);
extern void BuildFootShadowQuad(int playerIndex, int mode);

extern unsigned short g_randomRingBuffer[];  /* 0x0092498C */

/* Shared particle spawner helpers (defined below; also used by SpawnPlayerParticleEffects). */
static unsigned short StreamRead(void);

static void WriteParticleConstants(CollectEffect *e, int gravity, short widthP,
    short lifetime, short type, int animEnd, short af1, short af2,
    short bbSize, short animDiv, unsigned char uvX, unsigned char uvY,
    unsigned char tpage, short uvSpan);

/**
 * UpdateTrackObjects — 0x00496A48 — 128 bytes
 * Updates animated particle/effect entries.
 * Iterates 0x40 (64) entries, each 0x3C bytes (15 ints).
 * EAX = buffer base pointer (binary passes g_collectEffectBuf at 0x907F20).
 */
void UpdateTrackObjects(CollectEffect *buf)  /* EAX */
{
    CollectEffect *obj = buf;
    for (int i = 0; i < COLLECT_EFFECT_COUNT; i++, obj++) {
        if (obj->lifetime > 0) {
            obj->lifetime -= 1;
            obj->posX += obj->velX;
            int vy = obj->velY;
            obj->velY = vy + obj->accelY;
            obj->animFrameH -= 1;
            obj->posY += vy + obj->accelY;
            obj->posZ += obj->velZ;
            if (obj->animFrameH == 0) {
                obj->animFrameH = obj->animFrameW;
                int frame = obj->timer + obj->animDiv;
                obj->timer = frame;
                if (frame == obj->animEnd) {
                    obj->timer = 0;
                }
            }
        }
    }
}

/**
 * Update5PlayerRacePhysics — 0x004C9044 — 624 bytes
 * Per-frame 5-player collision/AI/physics update (NOT rendering).
 * Calls SweepPlayerCollision × 10 (all C(5,2) pairs), ResolvePlayerCollision (FUN_004d56b8) × 10,
 * then per-player: TrackSurfaceAI → PlayerPhysicsMain → UpdatePlayerMovement.
 *
 * The per-player loop uses extraout_EDX as a counter (Watcom register return).
 * In the original, TrackSurfaceAI/PlayerPhysicsMain/UpdatePlayerMovement advance
 * a global player pointer and return the player index in EDX.
 * For our translation, we explicitly loop through 5 players.
 */
void Update5PlayerRacePhysics(void)
{
    if (g_introCountdown == 0) {
        if (g_autoSteerFlag != 0) {
            g_autoSteerFlag--;
        }
        /* SweepPlayerCollision × 10 — all C(5,2) player pairs — 0x4c9078 */
        #define PP(i) (&g_playerBase[i])
        SweepPlayerCollision(PP(0), PP(1));             /* 0x4c9078 */
        SweepPlayerCollision(PP(0), PP(2));             /* 0x4c9088 */
        SweepPlayerCollision(PP(0), PP(3));             /* 0x4c9098 */
        SweepPlayerCollision(PP(0), PP(4));             /* 0x4c90a8 */
        SweepPlayerCollision(PP(1), PP(2));             /* 0x4c90b8 */
        SweepPlayerCollision(PP(1), PP(3));             /* 0x4c90c8 */
        SweepPlayerCollision(PP(1), PP(4));             /* 0x4c90d8 */
        SweepPlayerCollision(PP(2), PP(3));             /* 0x4c90e8 */
        SweepPlayerCollision(PP(2), PP(4));             /* 0x4c90f8 */
        SweepPlayerCollision(PP(3), PP(4));             /* 0x4c9108 */

        /* ResolvePlayerCollision × 10 — same pairs — 0x4c9118 */
        ResolvePlayerCollision(PP(0), PP(1));
        ResolvePlayerCollision(PP(0), PP(2));
        ResolvePlayerCollision(PP(0), PP(3));
        ResolvePlayerCollision(PP(0), PP(4));
        ResolvePlayerCollision(PP(1), PP(2));
        ResolvePlayerCollision(PP(1), PP(3));
        ResolvePlayerCollision(PP(1), PP(4));
        ResolvePlayerCollision(PP(2), PP(3));
        ResolvePlayerCollision(PP(2), PP(4));
        ResolvePlayerCollision(PP(3), PP(4));
        #undef PP
 
        if (g_raceSubMode == SUBMODE_TAG) {
            /* Tag mode: refresh tagged player's target waypoint — binary 0x4C91B6-0x4C91E7 */
            int *tagPlayer = ((int **)g_playerPtrTable)[g_raceOrder[5]];
            if (tagPlayer != NULL) {
                tagPlayer[0x13] = FindNearestWaypoint(
                    (int *)g_splineWaypoints,
                    tagPlayer[0] >> 12,
                    -(tagPlayer[1]) >> 12,
                    tagPlayer[2] >> 12,
                    g_posDataCount4);
            }
        }

        AdjustRubberBandAI();

        /* Per-player update loop: TrackSurfaceAI → PlayerPhysicsMain → UpdatePlayerMovement
         * Binary starts at edx=4 (player 1) — human player 0 is handled by
         * UpdateHumanPlayerPhysics, not this loop. */
        for (int i = 1; i < 5; i++) {
            Player *player = &g_playerBase[i];
            TrackSurfaceAI(player);
            PlayerPhysicsMain(player);
            UpdatePlayerMovement(player);
        }
    }
    else if (g_raceSubMode == SUBMODE_TAG && g_introCountdown < 0x3C) {
        if (g_autoSteerFlag != 0) {
            g_autoSteerFlag--;
        }
        /* Tag mode waypoint lookup — binary 0x4C9249-0x4C927E */
        int *tagPlayer = ((int **)g_playerPtrTable)[g_raceOrder[5]];
        if (tagPlayer != NULL) {
            tagPlayer[0x13] = FindNearestWaypoint(
                (int *)g_splineWaypoints,
                tagPlayer[0] >> 12,
                -(tagPlayer[1]) >> 12,
                tagPlayer[2] >> 12,
                g_posDataCount4);
        }
        AdjustRubberBandAI();
        
        /* Per-player loop — binary starts at edx=4 (player 1), not 0 */
        for (int i = 1; i < 5; i++) {
            Player *player = &g_playerBase[i];
            TrackSurfaceAI(player);
            PlayerPhysicsMain(player);
            extern void UpdatePlayerMovement(Player *player);
            UpdatePlayerMovement(player);
        }
    }
}

/**
 * SpawnFootShadows — 0x00483278 — 288 bytes
 * Per-frame: drops footstep shadows for each grounded, active player.
 * The animation frame value picks the foot — >= 0x2000 gives mode 1,
 * >= 0x1000 mode 2, anything lower leaves no print that frame.
 * Nothing here touches collision, despite the old name.
 */
void SpawnFootShadows(void)
{
    if ((int)g_numPlayers < 1) {
        return;
    }

    for (int i = 0; i < (int)g_numPlayers; i++) {
        Player *p = &g_playerBase[i];
        { extern int NetPlayerGone(int); if (NetPlayerGone(i)) continue; }   /* left the online race */
        /* Binary: 0x4832A7-C4 — stop at the ghost slot in plain Time Attack.
         * cmp ecx,1; cmp ecx,[0x8fd448](g_ghostToggle); cmp eax,2; cmp eax,[0x8fb954]; jg end
         * [0x8fb954] is g_raceSubMode, not a human count — Tag/Balloon run under
         * RACE_TIMEATTACK at subMode 2/3 and must NOT stop here. */
        if (i == 1 && g_ghostToggle == 1 &&
            g_raceType == RACE_TIMEATTACK && g_raceType > g_raceSubMode)
        {
            break;
        }

        /* Only check grounded, non-underwater, non-airborne, valid characters */
        if (p->groundedFlag == 0) {
            continue;
        }
        if (p->dynamicSpeedMode != 0) {
            continue;
        }
        if (p->_unk_0x78 != 0) {
            continue;
        }
        if (p->_unk_0x7A != 0) {
            continue;
        }
        if ((short)p->charId >= CHAR_COUNT) {
            continue;  /* charId >= CHAR_COUNT: invalid */
        }

        int hasCollisionData = p->_unk_0x1F0;
        if (hasCollisionData != 0) {
            /* Binary reads 32-bit pointer from player+0x9C. On 64-bit,
             * the frame stream pointer is in g_animDataPtrs[] side storage. */
            const short *animPtr = g_animDataPtrs[i];
            if (animPtr == NULL) {
                continue;
            }
            int frameVal = (int)(short)*animPtr;

            if (frameVal >= 0x2000) {
                /* High frame value: mode 1 — but skip Tails in water (charId==1, animId==0xC) */
                if (p->charId == CHAR_TAILS && p->animId == 0xC) {
                    continue;
                }
                BuildFootShadowQuad(i, 1);
            }
            else if (frameVal >= 0x1000) {
                /* Medium frame value: mode 2 */
                if (p->charId == CHAR_TAILS && p->animId == 0xC) {
                    continue;
                }
                BuildFootShadowQuad(i, 2);
            }
        }
    }
}

/**
 * UpdateRaceRings — 0x00496864 — 480 bytes
 * Per-frame: updates ring chase particles homing toward players.
 * in_EAX = ring chase array base (0x907B20, 32 entries × 8 ints).
 *
 * Each entry: [posX, posY, posZ, velX, velY, velZ, timer, targetPlayerPtr]
 * Spawned by UpdateRaceObjects when an item box is collected.
 * Spring physics: accelerate ±0x200 per axis toward target,
 * dampen velocity by half on overshoot. When all 3 axes within
 * 0x2000: collected — ring SFX + ringCount increment.
 * Multiplayer Normal (raceType==1 subMode==0) awards racePosition rings
 * (catch-up mechanic); other modes award 1.
 */
void UpdateRaceRings(void)
{
    int *entry = g_ringChaseArray;              /* EBX = EAX at entry */

    for (int i = 0; i < 32; i++, entry += 8) {     /* 0x496a00: 32 entries, 32 bytes each */
        int timer = entry[6];                   /* 0x496a05: [ebx + 0x18] */
        if (timer <= 0) {
            continue;               /* 0x496a0a: jle skip */
        }

        entry[6] = timer - 1;                   /* 0x496a0c-12: decrement timer */

        /* Binary stores a 32-bit pointer in entry[7] / [ebx + 0x1c] using
         * the sign bit as a path flag. We shadow the real ptr in
         * g_ringChaseTarget[i] (since 64-bit ptrs don't fit in the int slot)
         * and use entry[7] as a pure path flag: -1 = path A (no validation),
         * +1 = path B (validate target+0x80). */
        int pathFlag = entry[7];                /* 0x496a0f: [ebx + 0x1c] */
        Player *target = (Player *)g_ringChaseTarget[i];
        if (target == NULL) { 
            entry[6] = 0;
            continue;
        }   /* defensive — should not happen */

        if (pathFlag >= 0) {                    /* 0x496a17: path B (positive in binary) */
            /* Check target still has a speed boost / effect state keeping
             * the chase entry alive. Kill entry when the player's
             * itemEffectState has decayed below 2. */
            if (target->itemEffectState < 2) {  /* 0x496a26 */
                entry[6] = 0;                   /* 0x496a2f: kill entry */
                continue;
            }
        }
        /* path A (negative) — skip validation, use target as-is */

        int targetX = target->posX >> 4;                      /* 0x49687b-7d */
        int targetY = (target->posY >> 4) - 0x3000;           /* 0x4968da-e0 */
        int targetZ = target->posZ >> 4;                      /* 0x49693c-3f */

        int closeCount = 0;

        /* X axis chase 0x496880-4968d8 */
        int pos = entry[0];
        int vel = entry[3];
        int diff = pos - targetX;

        if (diff > 0) {                     /* 0x496893-95: chase right of target */
            vel -= 0x200;                   /* 0x496897 */
            if (diff < 0x2000) {
                closeCount = 1; /* 0x49689c-a4 */
            }
            int newPos = pos + vel;         /* 0x4968ad */
            if (newPos < targetX) {
                vel >>= 1; /* 0x4968af-d3: overshot → dampen */
            }
            entry[0] = newPos;
        }
        else {                            /* 0x4968b5: chase left of target */
            diff = -diff;
            vel += 0x200;                   /* 0x4968b7 */
            if (diff < 0x2000) {
                closeCount = 1; /* 0x4968bc-c4 */
            }
            int newPos = pos + vel;         /* 0x4968cd */
            if (newPos >= targetX) {
                vel >>= 1; /* 0x4968cf-d3: overshot → dampen */
            }
            entry[0] = newPos;
        }
        entry[3] = vel;                     /* 0x4968d5 */

        /* Y axis chase 0x4968d9-496939 */
        pos = entry[1];
        vel = entry[4];
        diff = pos - targetY;

        if (diff > 0) {
            vel -= 0x200;
            if (diff < 0x2000) {
                closeCount++;
            }
            int newPos = pos + vel;
            if (newPos < targetY) {
                vel >>= 1;
            }
            entry[1] = newPos;
        }
        else {
            diff = -diff;
            vel += 0x200;
            if (diff < 0x2000) {
                closeCount++;
            }
            int newPos = pos + vel;
            if (newPos >= targetY) {
                vel >>= 1;
            }
            entry[1] = newPos;
        }
        entry[4] = vel;

        /* Z axis chase 0x49693c-496996 */
        pos = entry[2];
        vel = entry[5];
        diff = pos - targetZ;

        if (diff > 0) {
            vel -= 0x200;
            if (diff < 0x2000) {
                closeCount++;
            }
            int newPos = pos + vel;
            if (newPos < targetZ) {
                vel >>= 1;
            }
            entry[2] = newPos;
        }
        else {
            diff = -diff;
            vel += 0x200;
            if (diff < 0x2000) {
                closeCount++;
            }
            int newPos = pos + vel;
            if (newPos >= targetZ) {
                vel >>= 1;
            }
            entry[2] = newPos;
        }
        entry[5] = vel;

        /* All 3 axes close → ring collected 0x496999 */
        if (closeCount != 3) {
            continue;
        }

        entry[6] = 0;                           /* 0x4969a3: consume entry */

        if (g_raceType == RACE_MULTIPLAYER && g_raceSubMode == SUBMODE_NORMAL) { /* 0x4969aa-b6: Multiplayer Normal */
            /* Catch-up: award racePosition rings (1st=1, 5th=5) */
            target->ringCount += target->racePosition;  /* 0x4969b8-c2 */
        }
        else {
            target->ringCount += 1;             /* 0x4969c8 */
        }

        /* Store collection position for visual effect */
        g_raceTimerB[0] = entry[0];             /* 0x4969cc-ce: chase posX */
        g_raceTimerB[1] = entry[1];             /* 0x4969d3-d6: chase posY */
        g_raceTimerB[2] = entry[2];             /* 0x4969db-e7: chase posZ */
        target->sfxTrigger = 0x1D;              /* 0x4969de: ring sound */
        target->renderState = 0x30001;          /* 0x4969ec */
    }
}

/**
 * UpdateRaceObjects — 0x004965C0 — 674 bytes
 * Per-frame: handles ring and item box collection for all players.
 * Two collection paths based on player[0x80] >> 16:
 *   > 0: item box mode (500/1000 radius), spawns chase particle
 *   <= 0: ring mode (50/100 radius), increments ring count
 *
 * NULL guard on g_ringSpawnArray added (not in binary).
 *
 * Ring array: 4 ints per entry (X, Y, Z, timer). Timer > 0 = respawning.
 * in_EAX = g_playerBase (Watcom fastcall)
 */
void UpdateRaceObjects(void)
{
    int tmp_particleIdx = g_particleIdx;                     /* 0x9020A4 */

    /* Build per-player position arrays (stack locals) */
    int playerX[5], playerY[5], playerZ[5];      /* [esp+0x00], [esp+0x14], [esp+0x28] */
    if ((int)g_numPlayers > 0) {                 /* 0x4965DA: test edx, edx; jle */
        for (int p = 0; p < (int)g_numPlayers; p++) {
            Player *pl = &g_playerBase[p];
            playerX[p] = pl->posX >> 12;                                     /* 0x4965E9 */
            short cid = pl->charId;                                          /* 0x4965F1: movsx */
            int charHeight = g_modelMeta[cid].charHeight;             /* 0x4965FE */
            playerY[p] = (charHeight - pl->posY) >> 12;                      /* 0x496604-606 */
            playerZ[p] = pl->posZ >> 12;                                     /* 0x49660D */
        }
    }

    /* Ring/object collection loop */
    int *ring = g_ringSpawnArray;                /* edx = [0x712D50] */
    if (g_ringCount <= 0) {
        goto epilogue;         /* 0x496634: test eax; jle */
    }

    for (int r = 0; r < g_ringCount; r++, ring += 4) {                          /* 0x4967A4-B8 */
        int timer = ring[3];                     /* 0x4967BA: mov esi, [edx+0xc] */
        if (timer == 0) {
            /* Ring/item is active — check collection */
            if ((int)g_numPlayers <= 0) {
                continue;                            /* 0x49664C */
            }
            for (int p = 0; p < (int)g_numPlayers; p++) {
                Player *pl = &g_playerBase[p];
                int effectState = (int)pl->itemEffectState;                  /* 0x496670 */

                if (effectState > 0) {
                    /* Item box mode: 500/1000 radius */            /* 0x496681 */
                    int dx = ring[0] - playerX[p];
                    int dy = ring[1] - playerY[p];
                    int dz = ring[2] - playerZ[p];
                    if ((unsigned int)(dx + 500) >= 1000) {
                        continue;          /* 0x496688-694 */
                    }
                    if ((unsigned int)(dy + 500) >= 1000) {
                        continue;          /* 0x49669F-6AB */
                    }
                    if ((unsigned int)(dz + 500) >= 1000) {
                        continue;          /* 0x4966B6-6C2 */
                    }

                    /* Item collected — set respawn timer */
                    ring[3] = g_raceSpeedMult * 3;                           /* 0x4966C4: lea [eax+eax*2] */

                    /* Spawn chase particle entry */                          /* 0x4966CF-70C */
                    int *part = &g_ringChaseArray[tmp_particleIdx * 8];
                    part[0] = ring[0] << 8;                                  /* posX */
                    part[1] = -(ring[1]) << 8;                               /* posY (negated) */
                    part[2] = ring[2] << 8;                                  /* posZ */
                    part[3] = 0;                                             /* velX */
                    part[4] = 0;                                             /* velY */
                    part[5] = 0;                                             /* velZ */
                    part[6] = 0x12C;                                         /* timer = 300 */
                    /* Binary 0x4966FC: stores positive player ptr in [ebx+0x1c]
                     * (path B — UpdateRaceRings will validate target+0x80 each
                     * frame). On 64-bit we shadow the real ptr in
                     * g_ringChaseTarget[] and use part[7] as a path flag only. */
                    g_ringChaseTarget[tmp_particleIdx] = pl;
                    part[7] = +1;                                            /* path B flag */
                    tmp_particleIdx++;                                                   /* 0x49670B */
                    if (tmp_particleIdx == 0x20) {
                        tmp_particleIdx = 0;                                /* 0x49670F-718 */
                    }
                    break;
                }
                else {
                    /* Ring mode: 50/100 radius */                  /* 0x49671F */
                    int dx = ring[0] - playerX[p];
                    int dy = ring[1] - playerY[p];
                    int dz = ring[2] - playerZ[p];
                    if ((unsigned int)(dx + 50) >= 100) {
                        continue;            /* 0x496726-72C */
                    }
                    if ((unsigned int)(dy + 100) >= 200) {
                        continue;           /* 0x49673B-744 */
                    }
                    if ((unsigned int)(dz + 50) >= 100) {
                        continue;            /* 0x496753-759 */
                    }

                    /* Ring collected! */
                    pl->ringCount += 1;                                      /* 0x49675F-76E */
                    pl->renderState = 0x30001;                               /* 0x496763 */
                    g_ringCollectPos[0] = ring[0] << 8;                      /* 0x496772-777 */
                    g_ringCollectPos[1] = -(ring[1]) << 8;                   /* 0x49677C-784 */
                    g_ringCollectPos[2] = ring[2] << 8;                      /* 0x496789-79F */
                    ring[3] = 0x5A;                                          /* 0x49678C: respawn timer */
                    pl->sfxTrigger = 0x1D;                                   /* 0x496796: SFX */
                    break;
                }
            }
        }
        else {
            /* Ring is respawning — decrement timer */
            ring[3] = timer - 1;                                             /* 0x4967C5-C8 */
            if (timer - 1 == 0) {
                /* Timer just hit 0 — store respawn position for visual effect */
                g_ringRespawnPos[0] = ring[0] << 8;                          /* 0x4967CF-D4 */
                g_ringRespawnPos[1] = -(ring[1]) << 8;                       /* 0x4967D9-E1 */
                g_ringRespawnPos[2] = ring[2] << 8;                          /* 0x4967E6-EC */
            }
        }
    }

epilogue:
    /* Per-player timer decrements */                               /* 0x4967F3-851 */
    for (int i = 0; i < 5; i++) {
        Player *pl = &g_playerBase[i];
        if (pl->itemEffectState > 0) {                                   /* sar 0x10; test; jle — tick speed boost timer */
            pl->itemEffectState -= 1;                                    /* dec word */
        }
    }

    g_particleIdx = tmp_particleIdx;                                                     /* 0x496852 */
}

/**
 * SaveReplayLog — 0x004DBC1C — 698 bytes
 * Saves ghost replay data to a per-track .dem file.
 * Writes a 16-short header (game state + per-player charIds + frame count),
 * then per-player replay frame data (g_ghostWriteIndex shorts each).
 * Counterpart: LoadReplayLog (0x4DBED8) in save.c.
 */
void SaveReplayLog(void)
{
    /* Select filename by trackId (0x4DBC26-0x4DBC83) */
    const char *filename;
    switch (g_trackId) {
        case TRACK_RESORT_ISLAND:
            filename = DATA_DIR SEP "DEMOS" SEP "ISLAND2.DEM";
            break;
        case TRACK_RADICAL_CITY:
            filename = DATA_DIR SEP "DEMOS" SEP "CITY2.DEM";
            break;
        case TRACK_REGAL_RUIN:
            filename = DATA_DIR SEP "DEMOS" SEP "RUIN2.DEM";
            break;
        case TRACK_REACTIVE_FACTORY:
            filename = DATA_DIR SEP "DEMOS" SEP "FACTORY2.DEM";
            break;
        case TRACK_RADIANT_EMERALD:
            filename = DATA_DIR SEP "DEMOS" SEP "EMERALD2.DEM";
            break;
        default:
            return; /* 0x4DBC73 */
    }

    FILE *fp = fOpen(filename, "wb");                             /* 0x4DBC83 */
    if (fp == NULL) {
        return;                                       /* 0x4DBC94 */
    }

    /* Write 16-short header — matches LoadReplayLog read order */
    unsigned short val;

    val = (unsigned short)g_numViewports;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x6E9910 */

    val = (unsigned short)g_numPlayers;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x6E990C */

    val = (unsigned short)g_numHumans;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x6E9908 */

    val = (unsigned short)g_trackId;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FB8EC */

    val = (unsigned short)g_raceType;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FB950 */

    val = (unsigned short)g_raceSubMode;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FB954 */

    val = (unsigned short)g_difficultyConfig;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FD444 */

    val = (unsigned short)g_weatherType;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x94BCF4 */

    val = (unsigned short)g_timeOfDay;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x94BCF8 */

    /* Per-player character IDs (0x4DBDC3-0x4DBE63) */
    val = (unsigned short)g_playerBase[0].charId;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FD5E6 */

    val = (unsigned short)g_playerBase[1].charId;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FDD02 */

    val = (unsigned short)g_playerBase[2].charId;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FE41E */

    val = (unsigned short)g_playerBase[3].charId;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FEB3A */

    val = (unsigned short)g_playerBase[4].charId;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x8FF256 */

    /* Frame count (0x4DBE68-0x4DBE84) */
    val = (unsigned short)g_ghostWriteIndex;
    bswap16_inplace(&val);
    fWrite(&val, 2, 1, fp); /* 0x901CD8 */

    /* Per-player replay frame data (0x4DBE89-0x4DBEC1)
     * Each player has g_ghostWriteIndex shorts of input/position data. */
    for (int i = 0; i < g_numViewports; i++) { /* 0x4DBE91 */
        unsigned short *src = g_taGhostSource + i * g_ghostMaxFrames; /* 0x91498C + i*maxFrames*2 */
        bswap16_arr(src, g_ghostWriteIndex);
        fWrite(src, 2, g_ghostWriteIndex, fp); /* 0x4DBEB6 */
        bswap16_arr(src, g_ghostWriteIndex);
    }

    fClose(fp); /* 0x4DBEC8 */
}

/**
 * UpdateMissiles — 0x00480D74 — 963 bytes
 * Per-frame update for the vehicle characters' fired homing missiles.
 * Iterates g_bounceStateArray (0x00907AF0, 4 entries × 12 bytes:
 * [int timer, Player* target, CollectEffect* particle]). For each active
 * missile: homes the particle toward target->pos (Y offset -0x4000), and on
 * contact (dist² < 0x3000) launches the target into the air (velY=-0x18000,
 * airTimer, sfxTrigger=0xF, animId=5, renderState=0x30003) and deactivates it.
 * Missiles are fired in vehicle_physics.c (type 0x1C2). NOT ring loss — Sonic R
 * missiles just knock you airborne.
 *
 * Also, every frame a missile is alive, spawns a short-lived trail sparkle into
 * g_collectEffectBuf at rotating slot g_raceCounterA0 (wraps at 60), positioned
 * at the missile + random jitter from the shared stream (StreamRead). Binary
 * 0x480f31-0x48104e. g_raceCounterA0 / g_ringSpawnReadPtr are the same globals
 * the spawner uses, so they're updated in place (no local-copy writeback needed).
 */
void UpdateMissiles(void)
{
    intptr_t *chase = g_bounceStateArray;

    for (int i = 0; i < 4; i++, chase += 3) {
        if (chase[0] == 0) {
            continue;  /* timer == 0: inactive */
        }

        int *target = (int *)(intptr_t)chase[1];
        Player *tgt = (Player *)target;
        int *particle = (int *)(intptr_t)chase[2];

        chase[0]--;  /* decrement timer */

        /* Accelerate particle toward target player position */
        /* X axis */
        /* Overshoot damping (all six branches): binary 0x480dd8 etc. halves the
         * velocity THEN advances by the NEW velocity — vel/=2; pos+=vel. Was
         * mistranslated as pos += vel/2 AFTER the halving (= vel/4): missiles
         * closed at half speed and hit frames late (Emerald demo desync at the
         * Metal Knuckles lock-on/overtake). */
        int tgtX4 = tgt->posX >> 4;
        if (particle[0] < tgtX4) {
            particle[3] += 0x80;
            if (tgtX4 <= particle[3] + particle[0]) {
                particle[3] /= 2;
                particle[0] += particle[3];
            }
        }
        if (tgtX4 < particle[0]) {
            particle[3] -= 0x80;
            if (particle[3] + particle[0] <= tgtX4) {
                particle[3] /= 2;
                particle[0] += particle[3];
            }
        }

        /* Y axis (with -0x4000 offset for height) */
        int tgtY4 = (tgt->posY >> 4) - 0x4000;
        if (particle[1] < tgtY4) {
            particle[4] += 0x80;
            if (tgtY4 <= particle[4] + particle[1]) {
                particle[4] /= 2;
                particle[1] += particle[4];
            }
        }
        if (tgtY4 < particle[1]) {
            particle[4] -= 0x80;
            if (particle[1] + particle[4] <= tgtY4) {
                particle[4] /= 2;
                particle[1] += particle[4];
            }
        }

        /* Z axis */
        int tgtZ4 = tgt->posZ >> 4;
        if (particle[2] < tgtZ4) {
            particle[5] += 0x80;
            if (tgtZ4 <= particle[5] + particle[2]) {
                particle[5] /= 2;
                particle[2] += particle[5];
            }
        }
        if (tgtZ4 < particle[2]) {
            particle[5] -= 0x80;
            if (particle[2] + particle[5] <= tgtZ4) {
                particle[5] /= 2;
                particle[2] += particle[5];
            }
        }

        /* Trail sparkle — one per active missile per frame (binary 0x480f31-0x48104e).
         * Missile particle pos is <<8 fixed-point, so the ±16 jitter is in unit
         * space. Reuses the shared particle spawner + rotating slot. */
        int jx = (int)(StreamRead() & 0x1F) - 0x10;
        int jy = (int)(StreamRead() & 0x1F) - 0x10;
        int jz = (int)(StreamRead() & 0x1F) - 0x10;
        CollectEffect *spark = &g_collectEffectBuf[g_raceCounterA0];
        spark->posX = ((particle[0] >> 8) + jx) << 8;
        spark->posY = ((particle[1] >> 8) + jy) << 8;
        spark->posZ = ((particle[2] >> 8) + jz) << 8;
        spark->velX = 0;
        spark->velY = 0;
        spark->velZ = 0;
        WriteParticleConstants(spark, -0x20, 0x10, 0x0A, 1, 0x40, 3, 3,
            0x20, 0x10, 0, 0x50, /* 0x481027: byte[0x8F6C28] */
            (unsigned char)g_tpageCharBase, 0x10);
        g_raceCounterA0++;
        if (g_raceCounterA0 >= 60) {
            g_raceCounterA0 = 0;
        }

        /* Check if particle reached the player (distance² < 0x3000) */
        int dx = (particle[0] >> 8) - (tgt->posX >> 12);
        int dy = ((particle[1] >> 8) - (tgt->posY >> 12)) + 0x1E;
        int dz = (particle[2] >> 8) - (tgt->posZ >> 12);
        if (dx * dx + dy * dy + dz * dz < 0x3000) {
            /* Hit! Launch player upward */
            tgt->velY = -0x18000;
            tgt->velX = particle[3] << 4;
            tgt->velZ = particle[5] << 4;
            tgt->airTimer = 1;
            tgt->groundedFlag = 0;
            tgt->sfxTrigger = 0xF;
            tgt->animId = 5;
            tgt->renderState = 0x30003;
            *(unsigned short *)((char *)particle + 0x1E) = 0;
            chase[0] = 0;  /* deactivate */
        }
    }
}

extern int g_splashDisableFlag;                /* 0x0090208C */
extern unsigned short g_randomRingBuffer[];    /* 0x0092498C — 513 shorts */

/* Read a 16-bit word from the particle random stream and advance.
 * Binary just reads forward forever into adjacent data. We wrap. */
static unsigned short StreamRead(void) {
    unsigned short val = *g_ringSpawnReadPtr;
    g_ringSpawnReadPtr++;
    if (g_ringSpawnReadPtr >= g_randomRingBuffer + 513) {
        g_ringSpawnReadPtr = g_randomRingBuffer;
    }
    return val;
}

static void WriteParticleConstants(CollectEffect *e, int gravity, short widthP,
    short lifetime, short type, int animEnd, short af1, short af2,
    short bbSize, short animDiv, unsigned char uvX, unsigned char uvY,
    unsigned char tpage, short uvSpan)
{
    e->accelY = gravity;
    e->halfW = widthP;
    e->lifetime = lifetime;
    e->spriteId = 0x210;
    e->type = type;
    e->timer = 0;
    e->animEnd = animEnd;
    e->animFrameW = af1;
    e->animFrameH = af2;
    e->billboardSize = bbSize;
    e->animDiv = animDiv;
    e->uvBaseX = uvX;
    e->uvBaseY = uvY;
    e->tpage = tpage;
    e->uvSpan = uvSpan;
}

#define ADVANCE_PARTICLE_IDX() do { \
    g_raceCounterA0++; \
    if (g_raceCounterA0 >= 60) { \
        g_raceCounterA0 = 0; \
    } \
} while(0)

/**
 * SpawnPlayerParticleEffects — 0x00483394
 * Per-player per-frame: spawns visual particle effects based on player state.
 * Handles dust (running), fire (state 3), airborne jump particles, landing
 * effects, burnout smoke, Super Sonic aura, water splash, and ring
 * collect/respawn sparkles.
 *
 * Writes entries into g_collectEffectBuf (60-byte entries, circular buffer of 60).
 * in_EAX = player pointer, in_EDX = effects enabled flag (1=show ring/item effects)
 */
void SpawnPlayerParticleEffects(Player *player, int effectsEnabled)
{
    CollectEffect *particles = g_collectEffectBuf;                  /* 0x907F20 */

    /* 0x483394: Determine particle color/width by lighting mode */
    unsigned char colorByte;
    int widthParam;                                                /* esi */

    if (g_trackId == TRACK_RADIANT_EMERALD || g_weatherType == WEATHER_CLEAR) { /* 0x4833A3-B4 */
        colorByte = 0x80;
        widthParam = 0x10;
    }
    else if (g_weatherType == WEATHER_RAIN) {                              /* 0x4833C2 */
        colorByte = 0x90;
        widthParam = 0;
    }
    else {                                                       /* 0x4833D0 */
        colorByte = 0xA0;
        widthParam = 0x10;
    }

    int stateFC = (int)player->abilityState;                        /* 0x4833DA — short at 0xFE */

    /* State dispatch */
    if (stateFC == 3) {
        goto state3_fire;                            /* 0x4833E6 */
    }

    /* 0x4833EC: Check dust particle conditions */
    if (player->animId == 2) {
        goto after_dust;                       /* 0x4741F8: [ebx+0x96]>>16 = animId */
    }
    if (player->groundedFlag != 1) {
        goto after_dust;                /* 0x483407 — player[0x70/4]>>16 */
    }
    if (player->dynamicSpeedMode != 0) {
        goto after_dust;             /* 0x474215: [ebx+0x3E]>>16 = dynamicSpeedMode */
    }
    short cid = player->charId;                                /* 0x48341B */
    if (cid == 4) {
        goto after_dust;                             /* 0x483425 */
    }
    
    int absVX;
    int absVZ;

    absVX = player->forwardSpeed;
    if (absVX < 0) {
        absVX = -absVX;                            /* cdq; xor; sub = abs */
    }
    absVZ = player->lateralSpeed;
    if (absVZ < 0) {
        absVZ = -absVZ;
    }
    if (absVX >= absVZ && player->forwardSpeed >= 0 &&         /* 0x48343F-5A */
        player->brakeCounter <= 4 &&                              /* 0x474250: [ebx+0x6A]>>16 */
        player->_unk_0x80 == 0)
    {
        goto after_dust;                  /* 0x47425A: [ebx+0x7E]>>16 */
    }

    /* 0x483460: Dust particle loop (3 iterations) */
    for (int di = 0; di < 3; di++) {                               /* ecx 0..2 */
        unsigned short w1 = StreamRead();                   /* 0x483473 */
        int xOff = (int)(w1 & 0xF) - 8;                           /* 0x48347A-86 */

        CollectEffect *e = &particles[g_raceCounterA0];             /* 0x483496 */
        e->posX = (xOff + (player->posX >> 12)) << 8;             /* 0x483499-9C */
        e->posY = ((player->posY >> 12) << 8) - 0x300;            /* 0x4834A2-B1 */

        unsigned short w2 = StreamRead();                   /* 0x4834BD */
        int zOff = (int)(w2 & 0xF) - 8;                           /* 0x4834C0-E2 */
        e->posZ = (zOff + (player->posZ >> 12)) << 8;             /* 0x4834E7-EA */

        e->velX = player->velX >> 5;                               /* 0x4834F0-F6: velX */
        e->velY = 0;                                               /* 0x4834FC: velY */
        e->velZ = player->velZ >> 5;                               /* 0x483509-21: velZ */
        WriteParticleConstants(e, -0x20, (short)widthParam, 0x0A, 1,
            0x40, 3, 3, 0x0A, 0x10, 0xC0, colorByte, g_tpageParticle2, 0x10);
        ADVANCE_PARTICLE_IDX();
    }

    /* 0x4835CE: Speed check + skid SFX */
    absVX = player->forwardSpeed;
    if (absVX < 0) {
        absVX = -absVX;
    }
    absVZ = player->lateralSpeed;
    if (absVZ < 0) {
        absVZ = -absVZ;
    }
    if (absVX >= 0x4000 || absVZ >= 0x4000) {                 /* 0x4835D6-EA */
        if (g_splashDisableFlag == 0 &&                        /* 0x4835F0 */
            player->sfxTrigger == -1)
        {                            /* 0x474409: [ebx+0xE8]>>16 = sfxTrigger */
            short cid = player->charId;                        /* 0x48360F */
            if (cid < 3) {                                      /* 0x483619 */
                player->sfxTrigger = 0x20;
            }
            else {
                player->sfxTrigger = 0x10;
            }
        }
    }

    goto after_dust;

/* 0x48369A: State 3 fire particles (4 iterations) */
state3_fire:
    if (player->groundedFlag != 1) {
        goto after_dust;                /* 0x4836A3 */
    }
    for (int fi = 0; fi < 4; fi++) {                               /* ecx 0..3 */
        int angleOff = (fi & 1) ? 0x5A8 : 0x258;                  /* 0x483848-59 */
        int angleIdx = (player->angleYaw - angleOff) & 0xFFF;     /* 0x4836B4 */
        int negCosVal = -g_cosTable[angleIdx];                      /* 0x4744C0-CE: X offset */
        int sinVal = g_sinTable[angleIdx];                         /* 0x4744C8: Z offset */

        unsigned short w1 = StreamRead();                   /* 0x4836D9 */
        int offA = (int)(w1 & 0xF) - 8;

        CollectEffect *e = &particles[g_raceCounterA0];
        e->posX = ((offA + (player->posX >> 12)) << 8) + negCosVal; /* 0x474510 */
        e->posY = ((player->posY >> 12) << 8) - 0xC00;            /* 0x483725 */

        unsigned short w2 = StreamRead();                   /* 0x483731 */
        int offB = (int)(w2 & 0xF) - 8;
        e->posZ = ((offB + (player->posZ >> 12)) << 8) + sinVal;  /* 0x474563 */

        e->velX = player->velX >> 6;                               /* velX */
        e->velY = 0;                                               /* velY */
        e->velZ = player->velZ >> 6;                               /* velZ */
        WriteParticleConstants(e, -0x20, (short)widthParam, 0x0A, 1,
            0x40, 3, 3, 0x0A, 0x10, 0xC0, colorByte, g_tpageParticle2, 0x10);
        ADVANCE_PARTICLE_IDX();
    }
    goto after_dust;

/* 0x483632: After dust — airborne particle check */
after_dust:
    if (player->_unk_0x86 <= 2) {
        goto surface_checks;                /* 0x47443E: [ebx+0x84]>>16 = _unk_0x86 */
    }

    /* 0x483644: Normal airborne particles (3 iterations) */
    int aIdx = (player->angleYaw - 0x400) & 0xFFF;                /* 0x483647-51 */
    int lowByte = *(unsigned char *)((char *)player + 0x86) & 0x7F; /* 0x483658-61 */
    int divisor = 10 - lowByte;                                    /* 0x47446F */
    int sinDivided = -(g_cosTable[aIdx] / divisor);                /* 0x474471-80: cosTable for X */
    int cosDivided = g_sinTable[aIdx] / divisor;                   /* 0x474483-92: sinTable for Z */

    for (int ai = 0; ai < 3; ai++) {                               /* 0x483868 */
        unsigned short w = StreamRead();
        int xOff = (int)(w & 0x1F) - 0x10;                        /* 5-bit, centered */

        CollectEffect *e = &particles[g_raceCounterA0];
        e->posX = (xOff + (player->posX >> 12)) << 8;             /* posX */
        e->posY = (player->posY >> 12) << 8;                      /* posY (no offset) */

        w = StreamRead();
        int zOff = (int)(w & 0x1F) - 0x10;
        e->posZ = (zOff + (player->posZ >> 12)) << 8;             /* posZ */

        /* Velocity from stream: 9-bit signed fields + sin/cos base */
        w = StreamRead();
        int vxRand = (int)(w & 0x1FF) - 0x100;
        e->velX = vxRand + sinDivided;                             /* velX */

        w = StreamRead();
        int vyRand = (int)(w & 0x1FF);
        e->velY = -(int)vyRand - 0x580;                           /* velY (negative + bias) */

        w = StreamRead();
        int vzRand = (int)(w & 0x1FF) - 0x100;
        e->velZ = vzRand + cosDivided;                             /* velZ */

        WriteParticleConstants(e, 0x80, (short)widthParam, 0x0A, 1,
            0x40, 3, 3, 0x0A, 0x10, 0xC0, colorByte, g_tpageParticle2, 0x10);
        ADVANCE_PARTICLE_IDX();
    }

    goto surface_checks;  /* fall through after airborne loop */

/* 0x483A3A: Surface state checks */
surface_checks:
    /* */ ;
    int groundState = player->yOffset;
    int jumpFlag = (int)player->dynamicSpeedMode;                   /* 0x474849: [ebx+0x3E]>>16 */
    int stateFC2 = (int)player->abilityState;

    /* 0x47483A-6C: Check for landing/grounded particle conditions */
    if (groundState == 0 || groundState == 0x60000) {              /* 0x47483F-47 */
        if (jumpFlag != 0) {                                       /* 0x474851 */
            int itemState = (int)player->itemEffectState;          /* 0x474853: [ebx+0x80]>>16 */
            if (itemState < 0) {
                goto doLanding;                     /* 0x47485E: jl */
            }
        }
        /* jumpFlag==0, or jumpFlag!=0 && itemState>=0 → check stateFC2 */
        if (stateFC2 == 2) {
            goto doLanding;                         /* 0x47486C */
        }
        goto splash_path;                                          /* 0x474A82 */
    }
    /* groundState != 0 && != 0x60000 → doLanding */

doLanding:
    /* */;
    /* 0x474872: Landing particles with optional sin/cos velocity */
    int landVelX = 0;
    int landVelZ = 0;
    if (stateFC2 == 2) {                                           /* 0x483A7E */
        short cid = player->charId;
        if (cid < 5) {                                             /* 0x483A8A */
            int lIdx = (player->angleYaw - 0x400) & 0xFFF;
            landVelX = (player->velX >> 4) - (g_cosTable[lIdx] >> 3); /* 0x4748A5-B2 */
            landVelZ = (player->velZ >> 4) + (g_sinTable[lIdx] >> 3); /* 0x4748B7-BD */
        }
    }

    for (int li = 0; li < 3; li++) {                               /* 0x483ACF */
        unsigned short w = StreamRead();
        int xOff = (int)(w & 0x1F) - 0x10;

        CollectEffect *e = &particles[g_raceCounterA0];
        e->posX = (xOff + (player->posX >> 12)) << 8;
        e->posY = (player->posY >> 12) << 8;

        w = StreamRead();
        int zOff = (int)(w & 0x1F) - 0x10;
        e->posZ = (zOff + (player->posZ >> 12)) << 8;

        /* Velocity from stream: 10-bit signed fields + landing base */
        w = StreamRead();
        int vx = (int)(w & 0x3FF) - 0x200;
        e->velX = vx + landVelX;

        w = StreamRead();
        int vy = (int)(w & 0x3FF);
        e->velY = -(int)vy - 0x580;

        w = StreamRead();
        int vz = (int)(w & 0x3FF) - 0x200;
        e->velZ = vz + landVelZ;

        WriteParticleConstants(e, 0x80, 0, 0x11, 1,
            0x60, 3, 3, 8, 0x10, 0, (unsigned char)0xE0, g_tpageParticle1, 0x10);
        ADVANCE_PARTICLE_IDX();
    }
    
    goto after_surface;

/* 0x483C82: Splash/impact single particle */
splash_path:
    if (g_raceOrder[6] != 0) {
        goto after_splash;                    /* 0x474A89: [0x902088] */
    }
    if (player->yOffset != 0x60000) {
        goto after_splash;             /* 0x483C96 */
    }

    unsigned short wSkip = StreamRead();                /* 0x483C9C: advance + test */
    if (!(wSkip & 1)) {                                        /* 0x483CAA-CAD */
        player->sfxTrigger = 0x14;                             /* SFX */
    }

    unsigned short w = StreamRead();
    int xOff = (int)(w & 0x1F) - 0x10;

    CollectEffect *e = &particles[g_raceCounterA0];
    e->posX = (xOff + (player->posX >> 12)) << 8;             /* posX */
    e->posY = (player->posY >> 12) << 8;                      /* posY */

    w = StreamRead();
    int zOff = (int)(w & 0x1F) - 0x10;
    e->posZ = (zOff + (player->posZ >> 12)) << 8;             /* posZ */

    e->velX = 0;
    e->velY = 0;
    e->velZ = 0;
    WriteParticleConstants(e, 0, 0x10, 0x1F, 1,
        0x40, 0x1C, 0x1C, 0x0C, 0x10, 0, (unsigned char)0xD0, g_tpageParticle1, 0x10);
    ADVANCE_PARTICLE_IDX();

after_splash:

    /* 0x474E04: Invincibility sparkle particles */
    /* Binary: mov eax,[ebx+0x86]; sar eax,0x10 → reads short at offset 0x88 = invincTimer */
    if (player->invincTimer != 0 && player->yOffset == 0) {       /* 0x474E0F-19 */
        for (int bi = 0; bi < 3; bi++) {                           /* 0x483E21 */
            unsigned short w = StreamRead();
            int xOff = (int)(w & 0x1F) - 0x10;

            CollectEffect *e = &particles[g_raceCounterA0];
            e->posX = (xOff + (player->posX >> 12)) << 8;
            e->posY = ((player->posY >> 12) << 8) - 0xC00;        /* higher Y offset */

            w = StreamRead();
            int zOff = (int)(w & 0x1F) - 0x10;
            e->posZ = (zOff + (player->posZ >> 12)) << 8;

            e->velX = player->velX >> 6;
            e->velY = 0;
            e->velZ = player->velZ >> 6;
            WriteParticleConstants(e, -0x20, 0x20, 0x0A, 1,
                0x40, 3, 3, 9, 0x10, 0, 0x60, g_tpageParticle1, 0x10);
            ADVANCE_PARTICLE_IDX();
        }
    }

    /* 0x483F80: Super Sonic aura particles (charId==9) */
    cid = player->charId;
    if (cid == CHAR_SUPER_SONIC && player->yOffset == 0) {                   /* 0x483F8A-94 */
        int yBias = 0x0A;                                      /* default height bias */
        if (g_postRaceCameraMode > 0x2F && g_raceType == RACE_GP && /* 0x483F9A-FB5 */
            player->racePosition < 5)
        {                        /* 0x474DB2: [ebx+0x5A]>>16 */
            yBias = 0x46;                                      /* taller aura during boost */
        }
        for (int si = 0; si < 2; si++) {                       /* 0x483FC7 */
            unsigned short w = StreamRead();
            int xOff = (int)(w & 0x3F) - 0x20;                /* 6-bit spread */

            CollectEffect *e = &particles[g_raceCounterA0];
            e->posX = (xOff + (player->posX >> 12)) << 8;

            /* posY: playerY - yBias - (random nibble * 3) */
            w = StreamRead();
            int yRand = (int)(w & 0x1F);                      /* 5-bit; 0x474E1D */
            int posYBase = (player->posY >> 12) - yBias - yRand * 3;
            e->posY = posYBase << 8;

            unsigned short w3 = StreamRead();
            int zOff = (int)(*(unsigned short *)((char *)&w3) & 0x3F) - 0x20;
            e->posZ = (zOff + (player->posZ >> 12)) << 8;

            e->velX = player->velX >> 6;
            e->velY = 0;
            e->velZ = player->velZ >> 6;
            WriteParticleConstants(e, -0x20, 0x20, 0x0A, 1,
                0x40, 3, 3, 9, 0x10, 0, 0x60, g_tpageParticle1, 0x10);
            ADVANCE_PARTICLE_IDX();
        }
    }

after_surface:
    /* 0x484146: Effect particles (ring collect, respawn sparkles) */
    if (effectsEnabled == 0) {
        return;                               /* 0x48414A */
    }

    /* 0x484150: Item burst effect (g_effectPosItemBurst at 0x901CB0) */
    if (g_effectPosItemBurst[0] != 0) {
        int bX = g_effectPosItemBurst[0] >> 8;                    /* 0x484160 */
        int bY = g_effectPosItemBurst[1] >> 8;                    /* 0x484170 */
        int bZ = g_effectPosItemBurst[2] >> 8;                    /* 0x48417C */
        for (int ei = 8; ei < 0x28; ei += 2) {                    /* 0x484163: ecx 8..38 step 2 */
            unsigned short w = StreamRead();
            int xOff = (int)(w & 0x3F) - 0x20;

            CollectEffect *e = &particles[g_raceCounterA0];
            e->posX = (xOff + bX) << 8;

            w = StreamRead();
            int yOff = (int)(w & 0x3F) - 0x20;
            e->posY = (yOff + bY) << 8;

            w = StreamRead();
            int zOff = (int)(w & 0x3F) - 0x20;
            e->posZ = (zOff + bZ) << 8;

            e->velX = 0;
            e->velY = 0;
            e->velZ = 0;
            e->accelY = 0;
            int af = ei / 3;                                       /* ecx/3 for expanding anim */
            WriteParticleConstants(e, 0, 0x20, (short)(af * 3), 1,
                0x40, (short)af, (short)af, 9, 0x10, 0, 0x60, g_tpageParticle1, 0x10);
            ADVANCE_PARTICLE_IDX();
        }
        g_effectPosItemBurst[0] = 0;                               /* 0x4842D9 */
    }

    /* 0x4842DF: Ring collect effect (g_ringCollectPos at 0x901C8C) */
    if (g_ringCollectPos[0] != 0) {
        int cX = g_ringCollectPos[0] >> 8;
        int cY = g_ringCollectPos[1] >> 8;
        int cZ = g_ringCollectPos[2] >> 8;
        for (int ri = 8; ri < 0x14; ri += 2) {                    /* ecx 8..18 step 2 */
            unsigned short w = StreamRead();
            int xOff = (int)(w & 0xF) - 8;

            CollectEffect *e = &particles[g_raceCounterA0];
            e->posX = (xOff + cX) << 8;

            w = StreamRead();
            int yOff = (int)(w & 0xF) - 8;
            e->posY = (yOff + cY) << 8;

            w = StreamRead();
            int zOff = (int)(w & 0xF) - 8;
            e->posZ = (zOff + cZ) << 8;

            e->velX = 0;
            e->velY = 0;
            e->velZ = 0;
            e->accelY = 0;
            int af = ri / 3;
            WriteParticleConstants(e, 0, 0x20, (short)(af * 3), 1,
                0x40, (short)af, (short)af, 9, 0x10, 0, 0x60, g_tpageParticle1, 0x10);
            ADVANCE_PARTICLE_IDX();
        }
        g_ringCollectPos[0] = 0;                                   /* 0x48446C */
    }

    /* 0x484472: Secondary effect (g_raceTimerB / effectPosB at 0x901C98) */
    if (g_raceTimerB[0] != 0) {
        int sX = g_raceTimerB[0] >> 8;
        int sY = g_raceTimerB[1] >> 8;
        int sZ = g_raceTimerB[2] >> 8;
        for (int si2 = 8; si2 < 0x10; si2 += 2) {                /* ecx 8..14 step 2 */
            unsigned short w = StreamRead();
            int xOff = (int)(w & 0xF) - 8;

            CollectEffect *e = &particles[g_raceCounterA0];
            e->posX = (xOff + sX) << 8;

            w = StreamRead();
            int yOff = (int)(w & 0xF) - 8;
            e->posY = (yOff + sY) << 8;

            w = StreamRead();
            int zOff = (int)(w & 0xF) - 8;
            e->posZ = (zOff + sZ) << 8;

            e->velX = 0;
            e->velY = 0;
            e->velZ = 0;
            e->accelY = 0;
            int af = si2 / 3;
            WriteParticleConstants(e, 0, 0x20, (short)(af * 3), 1,
                0x40, (short)af, (short)af, 9, 0x10, 0, 0x60, g_tpageParticle1, 0x10);
            ADVANCE_PARTICLE_IDX();
        }
        g_raceTimerB[0] = 0;                                       /* 0x4845FE */
    }

    /* 0x484606: Ring respawn effect (g_ringRespawnPos at 0x901CA4) */
    if (g_ringRespawnPos[0] != 0) {
        int rX = g_ringRespawnPos[0] >> 8;
        int rYraw = g_ringRespawnPos[1] >> 8;
        int rY = (rYraw << 8) + 0xC00;                            /* 0x48461C-1F */
        int rZ = g_ringRespawnPos[2] >> 8;
        for (int rri = 8; rri < 0x14; rri += 3) {                 /* ecx 8,11,14,17 step 3 */
            unsigned short w = StreamRead();
            int xOff = (int)(w & 0x1F) - 0x10;

            CollectEffect *e = &particles[g_raceCounterA0];
            e->posX = (xOff + rX) << 8;
            e->posY = rY;                                          /* pre-computed Y */

            w = StreamRead();
            int zOff = (int)(w & 0x1F) - 0x10;
            e->posZ = (zOff + rZ) << 8;

            e->velX = 0;
            e->velY = 0;
            e->velZ = 0;
            int af = rri / 3;
            WriteParticleConstants(e, -0x40, 0x20, (short)(af * 3), 1,
                0x40, (short)af, (short)af, 9, 0x10, 0, 0x60, g_tpageParticle1, 0x10);
            ADVANCE_PARTICLE_IDX();
        }
        g_ringRespawnPos[0] = 0;                                   /* 0x484783 */
    }
}
