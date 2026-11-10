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
 * confsig -- conference call signalling shared by conference icalls
 *
 * Handles CONFSTART / CONFCHECK / CONFEND / REJECT: which call in a
 * conversation is the "real" one (earliest timestamp.seqno wins),
 * ringing, stopping our other clients from ringing and the periodic
 * CONFCHECK. The owner (e.g. ccall) keeps its own state machine and is
 * queried for its phase; backend specific join information (e.g. SFT
 * urls) is encoded and adopted by the owner through handlers.
 */

#define CONFSIG_CONFSTART_TIMEOUT_S    (     120)
#define CONFSIG_SHOULD_RING_TIMEOUT    (   30000)
#define CONFSIG_RINGER_TIMEOUT         (   30000)
#define CONFSIG_SEND_CHECK_TIMEOUT     (   60000)
#define CONFSIG_ONGOING_CALL_TIMEOUT   (   90000)

struct confsig;

enum confsig_phase {
	CONFSIG_PHASE_NONE = 0,  /* not handling signalling (e.g. terminating) */
	CONFSIG_PHASE_IDLE,
	CONFSIG_PHASE_INCOMING,
	CONFSIG_PHASE_JOINING,
	CONFSIG_PHASE_ACTIVE,
};

enum confsig_stop_ringing {
	CONFSIG_STOP_RINGING_NONE     = 0,
	CONFSIG_STOP_RINGING_ANSWERED = 1,
	CONFSIG_STOP_RINGING_REJECTED = 2
};

/* Owner's current phase */
typedef enum confsig_phase (confsig_phase_h)(void *arg);

/* True if we are responsible for asserting the call (CONFCHECK) */
typedef bool (confsig_leader_h)(void *arg);

/* Send a fully built message */
typedef int  (confsig_send_h)(struct econn_message *msg,
			      struct list *targets,
			      bool my_clients_only,
			      void *arg);

/* Add backend join information to an outgoing CONFSTART/CONFCHECK */
typedef int  (confsig_encode_h)(struct econn_message *msg, void *arg);

/* Remote call is earlier than ours: take over its identity
 * (join information and secret, see confsig_set_secret).
 */
typedef int  (confsig_adopt_h)(const struct econn_message *msg,
			       const uint8_t *secret,
			       size_t secretlen,
			       void *arg);

/* Join information from a call that is not later than ours,
 * received while idle or incoming.
 */
typedef void (confsig_update_h)(const struct econn_message *msg, void *arg);

/* Idle -> incoming call */
typedef void (confsig_incoming_h)(uint32_t msg_time,
				  const char *userid,
				  const char *clientid,
				  bool should_ring,
				  void *arg);

/* Remote call is earlier while joining/active: leave ours, join theirs */
typedef int  (confsig_switch_h)(const struct econn_message *msg, void *arg);

/* Ringing stopped while incoming (reason is ICALL_REASON_*) */
typedef void (confsig_leave_h)(int reason, uint32_t msg_time, void *arg);

/* Incoming call is over. The owner may be destroyed in this handler. */
typedef void (confsig_close_h)(uint32_t msg_time, void *arg);

/* Called after a periodic CONFCHECK was sent */
typedef void (confsig_check_h)(void *arg);

struct confsig_handlers {
	confsig_phase_h    *phaseh;
	confsig_leader_h   *leaderh;
	confsig_send_h     *sendh;
	confsig_encode_h   *encodeh;
	confsig_adopt_h    *adopth;
	confsig_update_h   *updateh;
	confsig_incoming_h *incomingh;
	confsig_switch_h   *switchh;
	confsig_leave_h    *leaveh;
	confsig_close_h    *closeh;
	confsig_check_h    *checkh;     /* optional */
};

int  confsig_alloc(struct confsig **sigp,
		   struct userlist *userl,
		   bool is_mls,
		   const struct confsig_handlers *h,
		   void *arg);

int  confsig_set_secret(struct confsig *sig,
			const uint8_t *secret,
			size_t secretlen,
			const char *sessid);
const uint8_t *confsig_secret(const struct confsig *sig, size_t *lenp);

void confsig_set_meeting(struct confsig *sig, bool meeting);

uint64_t confsig_timestamp(const struct confsig *sig);
uint32_t confsig_seqno(const struct confsig *sig);
void confsig_set_ts(struct confsig *sig, uint64_t timestamp, uint32_t seqno);

/* Set timestamp/seqno of our new call and announce it with CONFSTART */
int  confsig_start(struct confsig *sig, uint64_t timestamp, uint32_t seqno);
/* Announce that the call ended (CONFEND) */
int  confsig_end(struct confsig *sig);

/* Handle CONFSTART, CONFCHECK, CONFEND, REJECT. Other types: EPROTO.
 * The owner may be destroyed during this call.
 */
int  confsig_recv(struct confsig *sig,
		  uint32_t msg_time,
		  const char *userid_sender,
		  const char *clientid_sender,
		  struct econn_message *msg);

/* Periodic CONFCHECK while active and leader */
void confsig_check_start(struct confsig *sig, uint64_t delay);
/* Run one periodic CONFCHECK tick now (and re-arm the timer) */
void confsig_check_now(struct confsig *sig);
void confsig_check_stop(struct confsig *sig);

/* Incoming call is closed when no CONFSTART/CONFCHECK arrives in time */
void confsig_ongoing_start(struct confsig *sig);
void confsig_ongoing_stop(struct confsig *sig);

void confsig_ring_cancel(struct confsig *sig);

/* Tell our other clients to stop ringing */
void confsig_set_stop_ringing(struct confsig *sig,
			      enum confsig_stop_ringing reason);
bool confsig_stop_ringing_pending(const struct confsig *sig);
void confsig_stop_others_ringing(struct confsig *sig);

const char *confsig_phase_name(enum confsig_phase phase);
