#ifndef NET_INTERP_H
#define NET_INTERP_H

/* Network state interpolation for client-side rendering smoothing.
 *
 * On a client, full player state arrives via 0xFFF0002F packets at the
 * host's sim rate (~30 Hz) but with UDP jitter, and physics is gated off
 * (g_netWaitFlag == 1) so the player struct is purely server-driven.
 * Without interp the visible position snaps to each new packet.
 *
 * NetInterpRecord(i) — call once after each 0xFFF0002F apply, with the
 *   player index that was just overwritten in g_playerBase.  Captures the
 *   freshly-written state as the new "curr" snapshot and rotates the prior
 *   curr into "prev".
 *
 * NetInterpApply() — call once per race-loop iteration on the client, after
 *   network packet processing and before any code that reads player state
 *   for camera/render.  Lerps prev->curr into g_playerBase for posX/Y/Z and
 *   the four angle fields.  Renders one packet interval behind so we
 *   interpolate (smooth) rather than extrapolate (jittery on packet loss).
 *
 * Other player fields (ringCount, lapsCompleted, animId, etc.) are left
 * untouched — they snap at packet rate, which is fine visually.
 */
void NetInterpRecord(int playerIdx);
void NetInterpApply(void);

/* NetInterpReset() — drop all snapshots; call at race start (slots are
 *   reassigned by join order every race). */
void NetInterpReset(void);

#endif
