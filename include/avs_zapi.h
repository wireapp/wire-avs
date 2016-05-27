/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
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
/* libavs
 *
 * Zeta-API helpers
 */


struct rest_cli;
struct json_object;


/************* Generic Declarations *****************************************/

typedef void (zapi_error_h)(int err, int code, const char *message,
			    const char *label, void *arg);


/************* Authentication ***********************************************/


/************* Users and Connections ****************************************/

/* Types, decoding, and encoding
 */

/* User objects.
 *
 * XXX This doesn't support picture assets yet.
 */
struct zapi_user {
	const char *email;
	const char *phone;
	int accent_id;		 /* -1 for not present  */
	const char *name;
	const char *id;
};

int zapi_user_encode(struct json_object *jobj, const struct zapi_user *user);
int zapi_user_decode(struct json_object *jobj, struct zapi_user *user);


struct zapi_connection {
	const char *status;
	const char *conversation;
	const char *to;
	const char *from;
	const char *last_update;
	const char *message;
};

int zapi_connection_encode(struct json_object *jobj,
			   const struct zapi_connection *conn);
int zapi_connection_decode(struct json_object *jobj,
			   struct zapi_connection *conn);


/* REST Requests
 */

typedef void (zapi_user_h)(int err, const struct zapi_user *user, void *arg);
int zapi_user_get(struct rest_cli *cli, int pri, const char *id,
		  zapi_user_h *userh, void *arg);


typedef bool (zapi_connection_h)(int err, const struct zapi_connection *conn,
				 void *arg);
int zapi_connection_apply(struct rest_cli *cli, int pri,
			  zapi_connection_h *connh, void *arg);

int zapi_connection_update(struct rest_cli *cli, int pri, const char *id,
			   const struct zapi_connection *conn,
			   zapi_error_h *errh, void *arg);

/************* Push Tokens **************************************************/


/************* Conversations and Messaging **********************************/


struct zapi_message {
	const char *content;
	const char *nonce;
};

struct zapi_knock {
	const char *nonce;
};

struct zapi_hot_knock {
	const char *nonce;
	const char *ref;
};

struct zapi_event {
	const char *conversation;
	const char *type;
	const char *from;
	const char *id;
	const char *time;
	union {
		struct zapi_message	message;
		struct zapi_knock	knock;
		struct zapi_hot_knock	hot_knock;
	} data;
};

int zapi_event_decode(struct json_object *jobj, struct zapi_event *ev);

int zapi_conversation_put_self(struct rest_cli *cli, const char *conv,
			       const char *last_read, const bool *muted,
			       const char *archived,
			       zapi_error_h *errh, void *arg);

int zapi_conversation_post_message(struct rest_cli *cli, const char *conv,
				   const char *content, const char *nonce,
			   	   zapi_error_h *errh, void *arg);


/************* Calling ******************************************************/

struct zapi_ice_server {
	char url[256];         /* mandatory */
	char username[128];    /* optional */
	char credential[128];  /* optional */
};

#define MAX_ICESERVERS 4
struct zapi_flow {
	bool active;
	char id[UUID_SZ];
	struct zapi_ice_server srvv[MAX_ICESERVERS];  /* optional, for now */
	size_t srvc;
	char sdp_step[32];       /* pending, answering, offered */
	char remote_user[UUID_SZ];
	char creator[UUID_SZ];
};

int zapi_flow_encode(struct json_object *jobj,
		     const struct zapi_flow *flow);
int zapi_flow_decode(struct json_object *jobj, struct zapi_flow *flow);


int zapi_flows_encode(struct json_object *jobj,
		      const struct zapi_flow *flowv, size_t flowc);
int zapi_flows_decode(struct json_object *jobj,
		      struct zapi_flow *flowv, size_t *flowc);


int zapi_flowadd_encode(struct json_object *jobj,
			const char *convid,
			const struct zapi_flow *flowv, size_t flowc);

int zapi_iceservers_encode(struct json_object *jobj,
			   const struct zapi_ice_server *srvv,
			   size_t srvc);
int zapi_iceservers_decode(struct json_object *jobj,
			   struct zapi_ice_server *srvv, size_t *srvc);


/*
 * Usage: PUT /conversations/$CONVID/call/flows/$FLOWID/local_sdp
 */
struct zapi_local_sdp {
	const char *type;  /* offer, answer, ... */
	const char *sdp;
};

int zapi_local_sdp_encode(struct json_object *jobj,
			  const struct zapi_local_sdp *lsdp);
int zapi_local_sdp_decode(struct zapi_local_sdp *lsdp,
			  struct json_object *jobj);


/*
 * Usage: call.remote-sdp event
 */
struct zapi_remote_sdp {
	const char *state;
	const char *sdp;
	const char *flow;
	const char *conv;
	/* .type = call.remote-sdp */
};

int zapi_remote_sdp_encode(struct json_object *jobj,
			   const struct zapi_remote_sdp *rsdp);
int zapi_remote_sdp_decode(struct zapi_remote_sdp *rsdp,
			   struct json_object *jobj);


/*
 * Usage: PUT /conversations/$CONVID/call/flows/$FLOWID/local_candidates
 */
struct zapi_candidate {
	const char *mid;  /* sdp_mid         */
	int mline_index;  /* sdp_mline_index */
	const char *sdp;  /* sdp             */
};

int zapi_candidate_encode(struct json_object *jobj,
			  const struct zapi_candidate *cand);
int zapi_candidate_decode(struct zapi_candidate *cand,
			  struct json_object *jobj);


/*
 * Usage: PUT /conversations/%s/call/state
 */
struct zapi_call_state {
	const char *state;
	double quality;
};

int zapi_call_state_encode(struct json_object *jobj,
			   const struct zapi_call_state *cs);
int zapi_call_state_decode(struct zapi_call_state *cs,
			   struct json_object *jobj);


/*
 * call.state event
 */

struct zapi_call_state_event {
	char convid[UUID_SZ];
	char cause[16];

#define MAX_PARTICIPANTS 16
	struct zapi_participant {
		char userid[UUID_SZ];
		char state[16];
	} participantv[MAX_PARTICIPANTS];
	size_t participantc;
	uint32_t sequence;
	char type[16];
	char session[UUID_SZ];
	struct {
		char reason[64];
		char state[16];
	} self;
};

int zapi_call_state_event_encode(struct json_object *jobj,
				 const struct zapi_call_state_event *cs);
int zapi_call_state_event_decode(struct zapi_call_state_event *cs,
				 struct json_object *jobj);


/************* Assets *******************************************************/


/************* Push Notifications *******************************************/


/************* Onboarding and Address Book **********************************/


/************* People Search ************************************************/


/************* Notification stream ******************************************/


/* Prekeys */
#define ZAPI_PREKEY_MAX 128
struct zapi_prekey {
	uint8_t key[ZAPI_PREKEY_MAX];
	size_t key_len;
	uint16_t id;
};

int zapi_prekey_encode(struct json_object *jobj, const struct zapi_prekey *pk);
int zapi_prekey_decode(struct zapi_prekey *pk, struct json_object *jobj);
