/**
 * sonicr_functions.h — All function forward declarations
 *
 * Generated from sonicr_annotated.c analysis.
 * Functions are grouped by system with their original addresses.
 */

#ifndef SONICR_FUNCTIONS_H
#define SONICR_FUNCTIONS_H

#include "sonicr_types.h"
#include "player_struct.h"
#include "collect_effect.h"

/* =====================================================================
 * Core / Entry
 * ===================================================================== */
void DebugLog(char *fmt, ...);                          /* 0x00431140 */
int  Random(void);                                      /* 0x004E1342 */
void Srand(unsigned int seed);                          /* Watcom srand */

/* =====================================================================
 * Initialization
 * ===================================================================== */
void InitSonicR_A(void);                                /* 0x004781C0 */
void LoadGameState(void);                               /* 0x004CD904 */
void InitSineTable(void);                               /* 0x004E1366 */
void InitSaveData(void);                                /* 0x004D07D4 — actually sound init, see note in init.c */
void InitPadTypes(void);                                /* 0x00477EFC */
void InitInputMappings(void);                           /* 0x00470CC0 */
void InitDirectInput(void);                             /* 0x004871F8 */
void InitCDPlayer(void);                                /* 0x004CBDC8 */
void InitLevel(void);                                   /* 0x0047571C */
void LoadCharacterModels(void);                         /* 0x00475D58 */
void LoadCharacterAnimations(void);                     /* 0x00475F90 */
void InitOtherParticles(void);                        /* 0x0046EBB8 */
int  ExamineMachine(void);                              /* 0x004D4D94 */

/* =====================================================================
 * Screen Functions (all return int — see state_machine.md)
 * ===================================================================== */
int  SegaLogoScreen(void);                              /* 0x004DC1E0 */
int  TravellersTalesLogoScreen(void);                   /* 0x004DC440 */
int  TitleScreen(void);                                 /* 0x004DCFCC */
int  MainMenuScreen(void);                              /* 0x004885BC */
int  CharacterSelectScreen(void);                       /* 0x0048C968 */
int  TimeAttackModeSelect(void);                        /* 0x0048BA80 */
int  MultiPlayerModeSelect(void);                       /* 0x0048C270 */
int  NetworkScreen(void);                               /* 0x0048A8AC */
int  CourseSelectScreen(void);                          /* 0x0048DE34 */
int  MultiPlayerScreen(void);                           /* 0x0048905C */
int  ResultsScreen(int screenType);                     /* 0x004C71F0 — EAX=screenType */
int  AutoSelectDemo(void);                             /* 0x004DC190 */
int  OptionsMenuScreen(void);                           /* 0x00493BDC */
int  LoadSaveScreen(void);                              /* 0x0048F984 */
int  TimeRankingScreen(void);                            /* 0x00490D24 */
int  CreditsScreen(void);                               /* 0x004DB9FC */

/* =====================================================================
 * Rendering — D3D
 * ===================================================================== */
void BeginFrame(void);                                  /* 0x0042299C */
void EndFrame(void);                                    /* 0x00422BA0 */
void FlipD3D(void);                                     /* 0x004356BC */
void RestartD3D(void);                                  /* 0x00432BC8 */
void ProcessTpageStates(void);                               /* 0x4323CC — 52-tpage state-machine processor */
void D3DAppClearBackBuffer(void);                       /* FUN_00435868 */
void RenderFadeOverlay(void);                           /* FUN_00461DF4 */
void SetCameraStructPtr(int *cam);                      /* helper — sets camera for matrix builds */
void BuildViewMatrix(void);                             /* 0x0042316C */
void BuildD3DViewMatrix(void);                          /* 0x0042320C */
void ComputeCameraBasis(void);                          /* 0x004230E0 */
void DrawTexturedQuad(int xPos, int yPos, int depth,
                      int width, int height, int tpage,
                      int uvX, int uvY, int uvW, int uvH,
                      unsigned int color);              /* 0x00450C38 — EAX=xPos, EDX=yPos */
void Draw3DModelD3D(int xOffset, int yOffset, int zBase, int angleA,
                    int rotation, int angleC, intptr_t objPtr,
                    int scale);                         /* 0x00454C70 */

/* =====================================================================
 * HUD
 * ===================================================================== */
void RenderHUD(void);                                   /* 0x004CD2A8 */
void DrawTimerAndStatus(int vpIndex);                    /* 0x004D2C68 — binary: EAX = viewport index */
void DrawFootShadows(void);                              /* 0x00458E6C */
void ClearRaceStateArrays(void);                         /* inlined at 0x47152C / 0x471B82 / 0x472505 */
void DrawMinimapWidget(char *vpPlayer);                  /* 0x004D2718 — EAX=player */
void SubmitGroundShadow(intptr_t param);                /* 0x00460BE0 */
void DrawPickupShadows(void);                           /* 0x004621EC */
void DrawGroundParticlesD3D(int vpIdx);                 /* 0x004626B0 */
void DrawMovingPickupShadow(int worldX, int worldY, int worldZ, int scale); /* 0x0045B15C */
void DrawPauseOverlay(void);                         /* 0x004D13E8 */

/* =====================================================================
 * Physics
 * ===================================================================== */
void PlayerPhysicsMain(Player *player);                 /* 0x004206B8 — EAX=player */
int  TrackHeightCheck(Player *player);                  /* 0x00420598 — EAX=player */
void SweepPlayerCollision(Player *pA, Player *pB);      /* 0x004201A0 — EAX=pA, EDX=pB */
void ResolvePlayerCollision(Player *pA, Player *pB);    /* 0x004D56B8 — EAX=pA, EDX=pB */
void CollisionResponse(int *obj1, int *obj2, int p3, int p4, int p5, int p6); /* 0x0041FFD0 */
int  IsOnTrackSurface(float x, float z);                /* 0x0047B794 — returns 1 if on-track, 0 if off */
void UpdatePlayerPhysicsA(Player *player);              /* 0x004819B0 — EAX=player */
void UpdatePlayerPhysicsB(Player *player);              /* 0x00481A2C — EAX=player */
void UpdateHumanPlayerPhysics(Player *player, unsigned short inputState); /* 0x00485768 — EAX=player, param_2=input */
void UpdatePerPlayerInput(void);                        /* 0x0047754C — polls input + ghost record/playback */
void TickPlayerAnimation(Player *player);                          /* 0x00421B3C */
void ComputeRacePositions(int playerIdx);              /* 0x004812A0 — EAX=playerIndex */
void UpdatePlayerLapSector(Player *player);                    /* 0x004816E4 — EAX=player */
void UpdatePlayerAnimation(Player *player);             /* 0x00481CD0 — EAX=player */
void CollectiblePickupCheck(void);                      /* 0x00479398 */

/* =====================================================================
 * AI / Track
 * ===================================================================== */
void TrackSurfaceAI(Player *player);                     /* 0x0041E6DC — EAX=player */
int  FindNearestWaypoint(int *waypoints, int searchX, int searchY, int searchZ, int count); /* 0x00496544 */
void UpdateTrackWorld(void);                          /* 0x0047FAFC */
void AnimateTrackObjects(void);                           /* 0x0047FBC4 */
void AnimateTrackGeometry(int groupIndex, int *statePtr); /* 0x0047EF88 — EAX, EDX */
void SpawnEffectParticles(int *srcA, int *srcB, int *srcC); /* 0x0047E0EC */
int ProcessTrackTriggers(int *triggerList, int playerX, int playerY, int playerZ); /* 0x0047F13C */
void ProcessTrackTriggerResponse(int *triggerTable, int vpIdx, int modeFlag); /* 0x0047F1FC — EAX,EDX,EBX */
void UpdateWeather(void);                               /* 0x0046EC0C */
void TickIslandParticles(void);                              /* 0x0046ECDC */
void TickFactoryParticles(void);                             /* 0x0046E83C */
void TickEmeraldParticles(void);                             /* 0x0046EA48 */
void UpdateTrackWorld_Island(void);                     /* 0x0047DF3C */
void UpdateTrackWorld_City(void);                       /* 0x0047C3B8 */
void UpdateTrackWorld_Ruin(void);                       /* 0x00479B90 */
void UpdateTrackWorld_Factory(void);                    /* 0x0047A628 */
void UpdateTrackWorld_Emerald(void);                    /* 0x00479704 */
void AnimateTrackObjects_Island(void);                  /* 0x0047E00C */
void AnimateTrackObjects_City(void);                    /* 0x0047C9BC */
void AnimateTrackObjects_Ruin(void);                    /* 0x00479F44 */
void AnimateTrackObjects_Factory(void);                 /* 0x0047B9C8 */
void AnimateTrackObjects_Emerald(void);                 /* 0x004797A0 */
void TickObjectAngleCounters(void);                     /* 0x00479398 */
void TintTrackGourauds(int tintR, int tintG, int tintB); /* 0x00430A0C — EAX=R, EDX=G, EBX=B */
void TintCharacterGouraudTables(int tintR, int tintG, int tintB); /* 0x00430BDC */
void UpdateVertexLighting(Player *player);              /* 0x00430638 — EAX=player */

/* =====================================================================
 * Track Loading
 * ===================================================================== */
void LoadTrack3(void);                                  /* 0x00426DB8 */
void LoadTerrain(void);                                 /* 0x0042A3F8 */
void ParseTerrainHeader(void);                          /* 0x004D8904 */
void LoadAI(void);                                      /* 0x0042A44C */
void InitIsland(void);                                  /* 0x004732BC */
void InitCity(void);                                    /* 0x00473954 */
void InitFactory(void);                                 /* 0x00474744 */
void InitRuin(void);                                    /* 0x0047405C */
void InitEmerald(void);                                 /* 0x00474CE8 */

/* =====================================================================
 * Texture
 * ===================================================================== */
void TintBackgroundTPage(int tintR, int tintG, int tintB); /* 0x004884B4 */
void LoadTPageRGB(int tpage, const char *filename);     /* 0x0042AD10 — EAX=tpage, EDX=filename */
void LoadTPageRGB(int tpage, const char *filename);
void LoadPlopSprites(void);                             /* 0x0042A5EC call sites in track D3D inits — water plop rings into parallax tpage (224,0) */
void LoadTextureSubRect(const char *filename, int tpage,
                        int width, int height,
                        int destX, int destY);          /* 0x0042A848 — was "SkipAutoIncrementFile" */
void D3D_LoadPlayfieldTilesRGB(const char *filename);   /* 0x0042D418 — EAX=filename */
void ClaimTpage(int tpage, int w, int h, int state);    /* port-level: take a tpage slot for new content */
void FinalizeMenuTexturesD3D(void);                     /* 0x00438D10 — GL: marks tpages dirty */
void RenderWavingMenuBackground(void);                       /* 0x004C68B8 — menu wave overlay (software twin) */

/* =====================================================================
 * Sound
 * ===================================================================== */
int  OpenCDDevice(void);                                /* 0x004D0720 */
void CloseCDDevice(void);                           /* 0x004D0810 */
void PlaySoundEffect(int soundCmd, int distance, int freqParam); /* 0x00482280 — EAX, EBX, ECX */
void SFX_Play(int slot, int loop, int freq);                      /* SDL: play WAV in slot */
void SFX_Tick(void);                                    /* SDL: reap finished pitched one-shot voices (called lazily by SFX_Play) */
void SFX_Stop(int slot);                                /* SDL: stop playback */
void SFX_SetVolume(int slot, int dsVolume);             /* SDL: set volume (-10000..0) */
void SFX_SetPan(int slot, int dsPan);                   /* SDL: set pan (-10000..10000) */
void SFX_SetPosition(int slot, int pos);                /* SDL: rewind to start */
void LoadSoundEffect(const char *filename, int slot);   /* 0x004D064C — EAX=filename, EDX=slot */
void CloseDirectSound(void);                            /* 0x004D0AC4 */

/* Music ducking for the replay commentary (enhancement, not in the binary).
 * SFX_ClipDurationMs and Music_SetDucked are per-platform; the duck state
 * machine itself lives in the shared sound.c. */
int  SFX_ClipDurationMs(int slot);      /* length of the WAV in a slot, ms (0 if unknown) */
void Music_SetDucked(int ducked);       /* music volume: normal or ducked */

/* Music Volume slider (enhancement, not in the binary — the original option
 * is an on/off toggle). level is 0-8 on the same scale as g_optSfxVolume and
 * composes with the duck above. Per-platform. */
void Music_SetVolume(int level);
void SFX_DuckMusic(int durationMs);     /* duck now, restore after durationMs */
void SFX_DuckTick(void);                /* per-frame: restore when the clip is done */
void SFX_DuckStop(void);                /* force restore (replay cut short) */

/* Replay announcer sequencer (sound/replay_voice.c). The clips ship pre-split
 * so each piece fits the DC sound driver's 65534-frame sample cap; these play
 * the pieces of one clip back to back as a single line. */
void ReplayVoice_Load(int clipIndex);   /* load every piece of clip 0..5 */
void ReplayVoice_Start(void);           /* start the sequence and duck the music */
void ReplayVoice_Tick(void);            /* per-frame: start each piece when due */
void ReplayVoice_Stop(void);            /* silence the sequence, keep it loaded */

/* MODE A/B selector — bit 0 of the byte at 0x0068a8ca, toggled by F5 in the
 * network lobby (0x48B4D5) and shown as the "MODE A"/"MODE B" sprite. Lives in
 * byte 2 of g_netGameInfoDest[11], inside the config buffer UpdateNetworkSync
 * broadcasts, so a toggle ships with it and an incoming config overwrites it.
 * Bit clear = MODE A, set = MODE B. */
int  net_lobby_mode_b(void);
void net_lobby_set_mode_b(int on);
void net_lobby_set_mode_byte(unsigned char v);  /* whole byte — restore path */

/* =====================================================================
 * Ghost Replay
 * ===================================================================== */
void SaveGhostData(void);                               /* 0x0042E9A4 */
void LoadGhostData(void);                               /* 0x0042EBAC */
void LoadAllGhostTimes(void);                           /* 0x0042ED7C */

/* =====================================================================
 * Save / Load
 * ===================================================================== */
void SaveReplayLog(void);                               /* 0x004DBC1C */
int  LoadReplayLog(const char *filename);                /* 0x004DBED8 */
void SaveToSlot(void);                                  /* 0x0048F8CC */
void SaveGameSettings(void);                            /* 0x00424BB4 */
void SavePadTypesImpl(void);                            /* 0x00477D54 */

/* =====================================================================
 * Network
 * ===================================================================== */
void UpdateNetworkHost(void);                           /* 0x00487CE8 */
void SendNetworkHostData(void);                         /* 0x00487D64 */
int  net_should_run_physics(int playerIdx);
int  NetRaceEndPending(void);    /* port (#34): race over, but the agreed result is still due */
int  NetRaceResultFinal(void);   /* port (#34): standings are the agreed result; don't re-sort */
int  NetJoinRetryTick(void);     /* port (#7): resend JOIN_REQ until answered; 1 waiting, -1 gave up */
void UpdateNetworkClient(void);                         /* 0x00487EC0 */
void SendNetworkClientData(void);                       /* 0x00487F7C */
void UpdateNetworkSync(const void *data, int len);      /* 0x00487644 */
void DrawTextBox(int x1, int y1, int x2, int y2);      /* 0x00486B58 */
int  CreateNetworkSession(const char *name, const char *pw); /* 0x00486D78 */
int  JoinNetworkSession(const char *name, int enumIdx); /* 0x004870A8 */
void EnumDirectPlaySessions(void);                      /* 0x00486C5C */
int  BuildNetworkPhoneNumber(void);                     /* 0x0048A6D8 */
void CloseDirectPlaySession(void);                      /* 0x004875CC */
void NetRecvThread_Start(void);                         /* SDL: spawn recv pthread */
void NetRecvThread_Stop(void);                          /* SDL: stop recv pthread */
void ApplyNetworkPlayerState(void);                     /* 0x004D9568 — recv dispatcher */

/* =====================================================================
 * Input
 * ===================================================================== */
void ReadInput(void);                                   /* 0x00477228 */
void ResetInputState(void);                             /* 0x0047059C */
void InitOptionStuff(void);                             /* 0x0047096C */
void PollAllInputDevices(void);                         /* 0x004769B0 */

/* =====================================================================
 * Fade / Misc
 * ===================================================================== */
void UpdateFade(void);                                  /* 0x004305D4 */
int  GetLogicalCDTrack(void);                           /* 0x004D0100 — current medley track: curr while playing, curr+1 once a non-looping track ends */
void StopCD(void);                                      /* 0x004D0264 — MCI_STOP */
void UpdateCDPlayback(int trackNum);                    /* 0x004D01AC — same as PlayCD */
#ifdef SONICR_DC
void PauseCD(void);
void ResumeCD(void);
int  RunningFromSlowMedia(void);                        /* platform_dc.c — boot-time read-speed probe (CD-R vs ODE/dcload) */
#endif
void WaitForFrameCap(void);                             /* helper — 30fps cap */
void Shutdown(void);                                    /* 0x004CA2FC */
void StopAmbientSounds(void);                                  /* 0x004D0458 */

/* =====================================================================
 * HUD Sub-functions
 * ===================================================================== */
void RenderParallaxStripsD3D(int vpIdx);                     /* 0x0045C444 — EAX = 0x68B210 + vpIdx*0x1C */
void RenderPlayfieldGridD3D(float *baseVerts, float *upperVerts, int *vpConfig); /* 0x0045E1BC */
void AnimateBalloons(void);                     /* 0x0046149C */
void DrawItemBoxD3D(void);                           /* 0x0044F98C */
void DrawCollectEffectsD3D(CollectEffect *buf);         /* 0x00450754 — EAX=particleBuf */
void ComputeParallaxAndPlayfieldState(int vpIdx, int *cam); /* 0x4979BC — EAX=vpIdx, EDX=cam */
void DrawOtherParticles(void);                     /* D3D weather */

/* =====================================================================
 * Title Screen Helpers
 * ===================================================================== */
void FindSonicRCD(void);                                /* 0x004D0F4C */
void InitParticleDisplayParams(void);                   /* 0x0046E250 */
void UpdateParticleSpawning(void);                      /* 0x0046E2B8 */
void FillVertexColorsByDepth(int r1, int g1, int b1,
                             int r2, int g2, int b2);   /* 0x004657A0 */
void RenderTitleLogo(void);                             /* 0x004DCB14 */
void RenderEnvMappedModel3D(int xOff, int yOff, int zBase, int rotA,
                            int rotB, int rotC, int modelIdx,
                            int zNearBias); /* 0x00468744 */
void RenderLogoQuads(void);                             /* 0x0046154C */
void DrawDebugOverlay(void);                            /* debug text overlay */
void DrawSplitBorder(void);                             /* split-screen border */
void RenderWaterReflectionD3D(int vpIdx);                /* 0x0045D5D4 — below-horizon water/reflection; EAX = 0x68B210 + vpIdx*0x1C */
void DrawCharacterSprites(void);                        /* D3D character sprites */
void DrawTimer(int xPos, int yPos, int depth, int timeInFrames, int displayMode); /* 0x4D1970 — EAX=xPos, EDX=yPos */
void DrawLapTime(int xPos, int yPos, int lapTimeValue);                    /* 0x4D20D0 — EAX=xPos, EDX=yPos, EBX=time */
void DrawReverseIndicator(Player *pl);                  /* 0x004D2600 — binary: EAX = player ptr */

#endif /* (close guard if needed) */
