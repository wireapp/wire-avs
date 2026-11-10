/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * cfrt -- Cloudflare Realtime SFU session management
 *
 * Two Cloudflare sessions (= PeerConnections) per call:
 *   PUB -- sendonly, publishes our tracks and our catalog data channel
 *   SUB -- recvonly, pulls remote tracks and remote catalog channels
 *
 * cfrt only builds request paths and JSON bodies. The application
 * performs the HTTP request (adding API base, app id and auth) and
 * returns the result with cfrt_response(). Paths are relative to
 * <api>/apps/<appId>, e.g. "/sessions/new".
 *
 * Requests of one leg are serialized; one request is in flight per leg.
 * After an SFU offer (pulling tracks) the leg is blocked until our
 * answer was sent with cfrt_answer().
 */

#ifndef AVS_CFRT_H
#define AVS_CFRT_H

#ifdef __cplusplus
extern "C" {
#endif

enum cfrt_leg {
	CFRT_LEG_PUB = 0,
	CFRT_LEG_SUB = 1,
};
#define CFRT_LEG_COUNT 2

#define CFRT_TRACK_AUDIO "audio"
#define CFRT_TRACK_VIDEO "video"

struct cfrt;

/* Result of a tracks/new request */
struct cfrt_track {
	struct le le;
	char *mid;
	char *session_id;   /* remote tracks: publisher session */
	char *track_name;
	char *kind;         /* "audio" / "video" */
	uint32_t ssrc;      /* remote tracks: from the SFU offer */
};

/* Perform an HTTP request. Return non-zero if it could not be started */
typedef int  (cfrt_http_h)(enum cfrt_leg leg,
			   const char *method,
			   const char *path,
			   const char *body,
			   void *arg);

/* Session was created */
typedef void (cfrt_session_h)(enum cfrt_leg leg,
			      const char *session_id,
			      void *arg);

/* SDP from the SFU. initial: answer to the leg's first offer */
typedef void (cfrt_sdp_h)(enum cfrt_leg leg,
			  bool offer,
			  bool initial,
			  const char *sdp,
			  void *arg);

/* Remote tracks were added (list of struct cfrt_track) */
typedef void (cfrt_tracks_h)(enum cfrt_leg leg,
			     const struct list *tracks,
			     void *arg);

/* Data channel was added. session_id is NULL for our own channel */
typedef void (cfrt_dc_h)(enum cfrt_leg leg,
			 const char *session_id,
			 const char *name,
			 int id,
			 void *arg);

/* A request failed, the leg is unusable */
typedef void (cfrt_error_h)(enum cfrt_leg leg, int err, void *arg);

struct cfrt_handlers {
	cfrt_http_h    *httph;
	cfrt_session_h *sessionh;
	cfrt_sdp_h     *sdph;
	cfrt_tracks_h  *tracksh;
	cfrt_dc_h      *dch;
	cfrt_error_h   *errorh;
};

int  cfrt_alloc(struct cfrt **rtp, const struct cfrt_handlers *h, void *arg);

/* Our offer. PUB: creates the session if needed and publishes every
 * sending audio/video m-line not yet published. SUB: creates the session.
 */
int  cfrt_offer(struct cfrt *rt, enum cfrt_leg leg, const char *sdp);

/* Our answer to an SFU offer (renegotiate) */
int  cfrt_answer(struct cfrt *rt, enum cfrt_leg leg, const char *sdp);

/* SUB: pull a publisher's tracks (NULL names are skipped) */
int  cfrt_subscribe(struct cfrt *rt, const char *session_id,
		    const char *audio_track, const char *video_track);
/* SUB: stop pulling all tracks of a publisher */
int  cfrt_unsubscribe(struct cfrt *rt, const char *session_id);

/* PUB: publish our data channel */
int  cfrt_dc_publish(struct cfrt *rt, const char *name);
/* SUB: pull / drop a publisher's data channel */
int  cfrt_dc_subscribe(struct cfrt *rt, const char *session_id,
		       const char *name);
int  cfrt_dc_unsubscribe(struct cfrt *rt, const char *session_id,
			 const char *name);

/* HTTP result of the leg's request in flight */
int  cfrt_response(struct cfrt *rt, enum cfrt_leg leg,
		   int status, const char *body);

/* Forget the leg's session and pending requests (e.g. PC replaced) */
void cfrt_reset(struct cfrt *rt, enum cfrt_leg leg);

const char *cfrt_session_id(const struct cfrt *rt, enum cfrt_leg leg);
const char *cfrt_leg_name(enum cfrt_leg leg);
bool cfrt_busy(const struct cfrt *rt, enum cfrt_leg leg);

/* Tracks of a leg (struct cfrt_track): PUB published, SUB pulled */
const struct list *cfrt_tracks(const struct cfrt *rt, enum cfrt_leg leg);

int  cfrt_debug(struct re_printf *pf, const struct cfrt *rt);


/* SDP helpers (exposed for tests) */
typedef bool (cfrt_sdp_media_h)(const char *kind, const char *mid,
				const char *dir, uint32_t ssrc, void *arg);
/* Calls mediah for every m-line; stops when it returns true */
int  cfrt_sdp_media_apply(const char *sdp, cfrt_sdp_media_h *mediah,
			  void *arg);
uint32_t cfrt_sdp_ssrc(const char *sdp, const char *mid);

#ifdef __cplusplus
}
#endif

#endif
