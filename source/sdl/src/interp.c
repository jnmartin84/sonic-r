/**
 * interp.c — a race at 60 fps on the Xbox (opt-in: Options > Graphics).
 *
 * Method and most of this file come from the PSP port by ghurone
 * (github.com/ghurone/Sonic-R-PSP, src/psp/interp_psp.c): the game's logic
 * stays at 30 Hz (its physics and AI are per tick), and each tick is shown
 * twice: first a frame halfway from the last tick to this one, then this
 * tick's own. The halfway frame is the same RenderHUD with what the logic moved
 * put halfway (cameras, players, their poses, the track's decorations, the
 * particles, the rain and snow), and all of it put back after. A tick that
 * jumped (a camera cut, a player put back on the track, a particle born again
 * elsewhere) shows its own state twice.
 *
 * Xbox differences: the second frame is paced by the clock (half a tick after
 * the tick started) instead of by a vertical blank, and the halfway frame is
 * dropped for a while whenever the console cannot fit two frames in a tick
 * (heavy scenes, 3-4 players), so the game never runs slower than at 30 fps.
 */
#ifdef SONICR_XBOX
#include <string.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

extern void SetCameraStructPtr(int *cam);    /* view_matrix.c */
extern void BuildViewMatrix(void);
extern void ComputeCameraBasis(void);
extern void BuildD3DViewMatrix(void);
extern void RenderHUD(void);                 /* hud_full.c */
extern void FlipD3D(void);
#include "platform.h"

int g_xbInterpGen;   /* once a race (track_init.c): the models and the last pose are of another race */

extern int g_xb60;   /* save.c: 60 fps in a race (Options > Graphics, SONICR.INF); 0: 30, as the game */

#define INTERP_PLAYERS 8
#define INTERP_VIEWS   4
#define CAM_STRIDE     0xC8
/* a jump, not motion, between two ticks: in world units (<< 12) and 12-bit angles */
#define JUMP_DIST      (512 << 12)
#define JUMP_ANGLE     0x200

typedef struct {
    int x, y, z, pitch, yaw, roll, yOffset, pitchCombo;
    int animFrame, animId;      /* (the pose: Interp_PrevAnimFrame) */
} PlayerPose;

typedef struct {
    int x, y, z, yaw, pitch;
} CamPose;

/* a decoration: its pivot (world units) and angles */
#define INTERP_OBJS    1280         /* (Radical City has 1016) */
#define OBJ_JUMP       512
typedef struct {
    int x, y, z;
    short yaw, pitch, roll, deco;
} ObjPose;

/* a particle (positions in world units << 8) */
#define INTERP_PARTS   64           /* weather.c's MAX_PARTICLES */
#define INTERP_PRECIP  128          /* race_timing.c's PRECIP_MAX */
#define PART_JUMP      (256 << 8)
typedef struct {
    int x, y, z, life, on;
} PartPose;

extern OtherParticle g_particleArray[];   /* weather.c */

static struct {
    int valid, gen, players, views;
    PlayerPose pl[INTERP_PLAYERS];
    CamPose cam[INTERP_VIEWS];
    int objs;
    const void *objArray;
    ObjPose obj[INTERP_OBJS];
    PartPose part[INTERP_PARTS];
    PartPose precip[INTERP_PRECIP];
} s_last;

int g_xbInterpHalf;   /* (drawing the halfway frame now) */

#define HALF_TICK_MS    16   /* where in the 33 ms tick the own frame is shown */
#define HALF_BUDGET_MS  21   /* the logic and the halfway frame must end by then */
#define COOL_TICKS      30   /* ticks without halfway frames after a slow one */
static int s_cool;           /* ticks left without halfway frames */
static int s_halfMs = 6;     /* how long a halfway frame takes (smoothed, ms) */

/* (INTERP_EXTRA=0: the cameras and players alone halfway) */
#ifndef INTERP_EXTRA
#define INTERP_EXTRA 1
#endif

static inline short obj_short(const int *obj, int off)
{
    return *(const short *)((const char *)obj + off);
}

static inline void obj_set_short(int *obj, int off, int v)
{
    *(short *)((char *)obj + off) = (short)v;
}

/* Drawing the halfway frame: the animation frame player p's pose comes from
 * halfway from, -1 for this tick's alone (another animation, or a jump). */
int Interp_PrevAnimFrame(const Player *p)
{
    int i = (int)(p - g_playerBase);
    if (!g_xbInterpHalf || i < 0 || i >= s_last.players) {
        return -1;
    }
    const PlayerPose *a = &s_last.pl[i];
    if (a->animId != p->animId || a->animFrame == p->animFrameIdx || a->animFrame < 0) {
        return -1;
    }
    return a->animFrame;
}

static RenderCamera *view_cam(int vp)
{
    return (RenderCamera *)((char *)g_viewportConfigArray + vp * CAM_STRIDE);
}

static int counts(int *players, int *views)
{
    *players = g_numPlayers < INTERP_PLAYERS ? (int)g_numPlayers : INTERP_PLAYERS;
    *views = g_numHumans < INTERP_VIEWS ? g_numHumans : INTERP_VIEWS;
    return *players >= 0 && *views >= 1;
}

static int half(int a, int b)
{
    return a + (b - a) / 2;
}

/* halfway the short way round (the angles may or may not be kept to 12 bits) */
static int half_angle(int a, int b)
{
    int d = ((b - a + 0x800) & 0xFFF) - 0x800;
    return a + d / 2;
}

static int angle_jump(int a, int b)
{
    int d = ((b - a + 0x800) & 0xFFF) - 0x800;
    return d > JUMP_ANGLE || d < -JUMP_ANGLE;
}

static int dist_jump(int ax, int ay, int az, int bx, int by, int bz)
{
    int dx = bx - ax, dy = by - ay, dz = bz - az;
    return dx > JUMP_DIST || dx < -JUMP_DIST || dy > JUMP_DIST || dy < -JUMP_DIST || dz > JUMP_DIST ||
           dz < -JUMP_DIST;
}

/* After a tick's own frame: its state, the next halfway frame's start. */
void Interp_Save(void)
{
    int players, views;
    if (!counts(&players, &views)) {
        s_last.valid = 0;
        return;
    }
    for (int i = 0; i < players; i++) {
        const Player *p = &g_playerBase[i];
        PlayerPose *o = &s_last.pl[i];
        o->x = p->posX;
        o->y = p->posY;
        o->z = p->posZ;
        o->pitch = p->anglePitch;
        o->yaw = p->angleYaw;
        o->roll = p->angleRoll;
        o->yOffset = p->yOffset;
        o->pitchCombo = p->pitchCombo;
        o->animFrame = p->animFrameIdx;
        o->animId = p->animId;
    }
    for (int vp = 0; vp < views; vp++) {
        const RenderCamera *rc = view_cam(vp);
        CamPose *o = &s_last.cam[vp];
        o->x = rc->worldX;
        o->y = rc->worldY;
        o->z = rc->worldZ;
        o->yaw = rc->yaw;
        o->pitch = rc->pitch;
    }
    /* the decorations */
    const int *objs = (const int *)g_objectStructArray;
    s_last.objs = objs && g_totalObjects > 0 ? (g_totalObjects < INTERP_OBJS ? g_totalObjects : INTERP_OBJS) : 0;
    s_last.objArray = objs;
    for (int oi = 0; oi < s_last.objs; oi++) {
        const int *obj = objs + oi * 17;
        ObjPose *o = &s_last.obj[oi];
        o->deco = obj_short(obj, 0x2E) == 2;
        if (o->deco) {
            o->x = obj[8];
            o->y = obj[9];
            o->z = obj[10];
            o->yaw = obj_short(obj, 0x18);
            o->pitch = obj_short(obj, 0x1A);
            o->roll = obj_short(obj, 0x1C);
        }
    }
    /* the particles */
    for (int i = 0; i < INTERP_PARTS; i++) {
        const OtherParticle *p = &g_particleArray[i];
        PartPose *o = &s_last.part[i];
        o->on = p->active != 0 && p->lifetime != 0;
        o->x = p->posX;
        o->y = p->posY;
        o->z = p->posZ;
        o->life = p->lifetime;
    }
    for (int i = 0; i < INTERP_PRECIP; i++) {
        const PrecipParticle *p = &g_precipParticles[i];
        PartPose *o = &s_last.precip[i];
        o->on = p->posY >= 0;
        o->x = p->posX;
        o->y = p->posY;
        o->z = p->posZ;
    }
    s_last.players = players;
    s_last.views = views;
    s_last.gen = g_xbInterpGen;
    s_last.valid = 1;
}

/* A camera's derived fields from its position and angles, as BuildCameraView makes them. */
static void camera_rebuild(RenderCamera *rc)
{
    const double posScale = 1.0 / 4096.0, angScale = 6.283185307179586 / 4096.0;
    rc->intX = rc->worldX >> 12;
    rc->intY = rc->worldY >> 12;
    rc->intZ = rc->worldZ >> 12;
    rc->camFloatX = (float)(rc->worldX * posScale);
    rc->camFloatY = (float)(rc->worldY * posScale);
    rc->camFloatZ = (float)(rc->worldZ * posScale);
    rc->yawFloat = (float)(rc->yaw * angScale);
    rc->pitchFloat = (float)(rc->pitch * angScale);
    SetCameraStructPtr((int *)rc);
    BuildViewMatrix();
    ComputeCameraBasis();
    BuildD3DViewMatrix();
}

/* Before a tick's own frame: the one halfway from the last tick, shown first. */
void Interp_Frame(void)
{
    int players, views;
    if (!g_xb60 || !s_last.valid || s_last.gen != g_xbInterpGen || !counts(&players, &views) ||
        players != s_last.players || views != s_last.views) {
        return;
    }
    /* Time: the tick began at g_currentTime. Two frames only fit if the logic and
     * the halfway frame (about as long as the tick's own) end within the first
     * 21 ms of its 33; otherwise this tick shows one frame, and the next
     * ticks too for a second, so that a heavy scene is not drawn twice at a
     * loss. (A halfway frame that itself ran long starts that pause.) */
    if (s_cool > 0) {
        s_cool--;
        return;
    }
    uint32_t t0 = platform_get_time_ms();
    if ((int)(t0 - g_currentTime) + s_halfMs > HALF_BUDGET_MS) {
        s_cool = COOL_TICKS;
        return;
    }

    static unsigned char savedCam[INTERP_VIEWS][CAM_STRIDE];
    PlayerPose savedPl[INTERP_PLAYERS];

    for (int vp = 0; vp < views; vp++) {
        RenderCamera *rc = view_cam(vp);
        const CamPose *a = &s_last.cam[vp];
        memcpy(savedCam[vp], rc, CAM_STRIDE);
        if (dist_jump(a->x, a->y, a->z, rc->worldX, rc->worldY, rc->worldZ) || angle_jump(a->yaw, rc->yaw) ||
            angle_jump(a->pitch, rc->pitch)) {
            continue;   /* (a cut: this tick's view) */
        }
        rc->worldX = half(a->x, rc->worldX);
        rc->worldY = half(a->y, rc->worldY);
        rc->worldZ = half(a->z, rc->worldZ);
        rc->yaw = half_angle(a->yaw, rc->yaw) & 0xFFF;
        rc->pitch = half_angle(a->pitch, rc->pitch) & 0xFFF;
        camera_rebuild(rc);
    }
    for (int i = 0; i < players; i++) {
        Player *p = &g_playerBase[i];
        const PlayerPose *a = &s_last.pl[i];
        PlayerPose *s = &savedPl[i];
        s->x = p->posX;
        s->y = p->posY;
        s->z = p->posZ;
        s->pitch = p->anglePitch;
        s->yaw = p->angleYaw;
        s->roll = p->angleRoll;
        s->yOffset = p->yOffset;
        s->pitchCombo = p->pitchCombo;
        if (dist_jump(a->x, a->y, a->z, p->posX, p->posY, p->posZ)) {
            continue;
        }
        p->posX = half(a->x, p->posX);
        p->posY = half(a->y, p->posY);
        p->posZ = half(a->z, p->posZ);
        p->anglePitch = half_angle(a->pitch, p->anglePitch);
        p->angleYaw = half_angle(a->yaw, p->angleYaw);
        p->angleRoll = half_angle(a->roll, p->angleRoll);
        p->yOffset = half(a->yOffset, p->yOffset);
        p->pitchCombo = half_angle(a->pitchCombo, p->pitchCombo);
    }

    /* the decorations: pivot and angles (kept to 12 bits: decoration_matrix
     * indexes the sine table with them as they are) */
    static ObjPose savedObj[INTERP_OBJS];
    int *objs = (int *)g_objectStructArray;
    int nObjs = objs == s_last.objArray ? s_last.objs : 0;
    nObjs = g_totalObjects < nObjs ? g_totalObjects : nObjs;
    nObjs = INTERP_EXTRA ? nObjs : 0;
    for (int oi = 0; oi < nObjs; oi++) {
        int *obj = objs + oi * 17;
        const ObjPose *a = &s_last.obj[oi];
        ObjPose *s = &savedObj[oi];
        s->deco = 0;
        if (!a->deco || obj_short(obj, 0x2E) != 2) {
            continue;
        }
        int yaw = obj_short(obj, 0x18), pitch = obj_short(obj, 0x1A), roll = obj_short(obj, 0x1C);
        int dx = obj[8] - a->x, dy = obj[9] - a->y, dz = obj[10] - a->z;
        if ((dx | dy | dz) == 0 && yaw == a->yaw && pitch == a->pitch && roll == a->roll) {
            continue;   /* (still) */
        }
        if (dx > OBJ_JUMP || dx < -OBJ_JUMP || dy > OBJ_JUMP || dy < -OBJ_JUMP || dz > OBJ_JUMP || dz < -OBJ_JUMP ||
            angle_jump(a->yaw, yaw) || angle_jump(a->pitch, pitch) || angle_jump(a->roll, roll)) {
            continue;
        }
        s->deco = 1;
        s->x = obj[8];
        s->y = obj[9];
        s->z = obj[10];
        s->yaw = (short)yaw;
        s->pitch = (short)pitch;
        s->roll = (short)roll;
        obj[8] = half(a->x, obj[8]);
        obj[9] = half(a->y, obj[9]);
        obj[10] = half(a->z, obj[10]);
        obj_set_short(obj, 0x18, half_angle(a->yaw, yaw) & 0xFFF);
        obj_set_short(obj, 0x1A, half_angle(a->pitch, pitch) & 0xFFF);
        obj_set_short(obj, 0x1C, half_angle(a->roll, roll) & 0xFFF);
    }

    /* the particles, and the rain and snow: one there both ticks, not moved
     * far (born again elsewhere, or round the camera) */
    static PartPose savedPart[INTERP_PARTS], savedPrecip[INTERP_PRECIP];
    for (int i = 0; i < INTERP_PARTS; i++) {
        OtherParticle *p = &g_particleArray[i];
        const PartPose *a = &s_last.part[i];
        PartPose *s = &savedPart[i];
        s->on = 0;
        if (!INTERP_EXTRA || !a->on || p->active == 0 || p->lifetime == 0 || p->posX - a->x > PART_JUMP || a->x - p->posX > PART_JUMP || p->posY - a->y > PART_JUMP ||
            a->y - p->posY > PART_JUMP || p->posZ - a->z > PART_JUMP || a->z - p->posZ > PART_JUMP) {
            continue;
        }
        s->on = 1;
        s->x = p->posX;
        s->y = p->posY;
        s->z = p->posZ;
        p->posX = half(a->x, p->posX);
        p->posY = half(a->y, p->posY);
        p->posZ = half(a->z, p->posZ);
    }
    for (int i = 0; i < INTERP_PRECIP; i++) {
        PrecipParticle *p = &g_precipParticles[i];
        const PartPose *a = &s_last.precip[i];
        PartPose *s = &savedPrecip[i];
        s->on = 0;
        if (!INTERP_EXTRA || !a->on || p->posY < 0 || p->posX - a->x > PART_JUMP || a->x - p->posX > PART_JUMP ||
            p->posY - a->y > PART_JUMP || a->y - p->posY > PART_JUMP || p->posZ - a->z > PART_JUMP ||
            a->z - p->posZ > PART_JUMP) {
            continue;
        }
        s->on = 1;
        s->x = p->posX;
        s->y = p->posY;
        s->z = p->posZ;
        p->posX = half(a->x, p->posX);
        p->posY = half(a->y, p->posY);
        p->posZ = half(a->z, p->posZ);
    }

    g_xbInterpHalf = INTERP_EXTRA;   /* (the poses: Interp_PrevAnimFrame) */
    RenderHUD();
    g_xbInterpHalf = 0;
    FlipD3D();

    for (int oi = 0; oi < nObjs; oi++) {
        const ObjPose *s = &savedObj[oi];
        if (s->deco) {
            int *obj = objs + oi * 17;
            obj[8] = s->x;
            obj[9] = s->y;
            obj[10] = s->z;
            obj_set_short(obj, 0x18, s->yaw);
            obj_set_short(obj, 0x1A, s->pitch);
            obj_set_short(obj, 0x1C, s->roll);
        }
    }
    for (int i = 0; i < INTERP_PARTS; i++) {
        if (savedPart[i].on) {
            g_particleArray[i].posX = savedPart[i].x;
            g_particleArray[i].posY = savedPart[i].y;
            g_particleArray[i].posZ = savedPart[i].z;
        }
    }
    for (int i = 0; i < INTERP_PRECIP; i++) {
        if (savedPrecip[i].on) {
            g_precipParticles[i].posX = savedPrecip[i].x;
            g_precipParticles[i].posY = savedPrecip[i].y;
            g_precipParticles[i].posZ = savedPrecip[i].z;
        }
    }

    for (int vp = 0; vp < views; vp++) {
        memcpy(view_cam(vp), savedCam[vp], CAM_STRIDE);
    }
    for (int i = 0; i < players; i++) {
        Player *p = &g_playerBase[i];
        const PlayerPose *s = &savedPl[i];
        p->posX = s->x;
        p->posY = s->y;
        p->posZ = s->z;
        p->anglePitch = s->pitch;
        p->angleYaw = s->yaw;
        p->angleRoll = s->roll;
        p->yOffset = s->yOffset;
        p->pitchCombo = s->pitchCombo;
    }
    /* The halfway frame is on its way to the screen; this tick's own follows half
     * a tick after the tick began (a vertical blank later at 60 Hz). */
    uint32_t t1 = platform_get_time_ms();
    int took = (int)(t1 - t0);
    s_halfMs = s_halfMs ? (s_halfMs * 3 + took) / 4 : took;
    if ((int)(t1 - g_currentTime) > HALF_BUDGET_MS) {
        s_cool = COOL_TICKS;   /* (too slow: no halfway frames for a while) */
        return;
    }
    while ((int)(platform_get_time_ms() - g_currentTime) < HALF_TICK_MS) {
        platform_pump_events();
        platform_sleep_ms(1);
    }
}
#endif /* SONICR_XBOX */
