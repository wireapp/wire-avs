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

extern const char econn_proto_version[];

enum econn_msg {
	/* via backend: */
	ECONN_SETUP       = 0x01,
	ECONN_CANCEL      = 0x02,

	/* p2p via datachannel: */
	ECONN_HANGUP      = 0x03,
	ECONN_PROPSYNC    = 0x04,

	/* Group call: */
	ECONN_GROUP_START = 0x05,
	ECONN_GROUP_LEAVE = 0x06,
	ECONN_GROUP_CHECK = 0x07,
	ECONN_GROUP_SETUP = 0x08,
	
	ECONN_UPDATE = 0x10,
	ECONN_REJECT = 0x11,
	ECONN_ALERT  = 0x12,
	
	/* Device pairing messages */
	ECONN_DEVPAIR_PUBLISH = 0x21,
	ECONN_DEVPAIR_ACCEPT  = 0x22,

};

enum econn_state {
	ECONN_IDLE     = 0,
	ECONN_PENDING_OUTGOING,
	ECONN_PENDING_INCOMING,
	ECONN_CONFLICT_RESOLUTION,
	ECONN_ANSWERED,              /* The call was answered */
	ECONN_DATACHAN_ESTABLISHED,  /* DataChannel was established */
	ECONN_HANGUP_SENT,
	ECONN_HANGUP_RECV,
	ECONN_UPDATE_SENT,
	ECONN_UPDATE_RECV,
	ECONN_TERMINATING,
};

enum econn_dir {
	ECONN_DIR_UNKNOWN = 0,
	ECONN_DIR_OUTGOING,
	ECONN_DIR_INCOMING
};

enum econn_transport {
	ECONN_TRANSP_BACKEND = 0,
	ECONN_TRANSP_DIRECT
};

enum econn_alert_level {
	ECONN_ALERT_LEVEL_WARNING = 1,
	ECONN_ALERT_LEVEL_FATAL   = 2,
};

#define ECONN_MESSAGE_TIME_UNKNOWN (0)

/**
 * Econn Properties.
 *
 * Each instance has a local set of properties
 * and a remote set of properties.
 *
 * The SETUP contains an initial set of properties,
 * and use UPDATE via Datachannel to modify properties.
 */
struct econn_props {
	struct odict *dict;
};

/*
 * The message-type is the actually message type that is sent on the Wire.
 * It containes the type (SETUP or CANCEL) and message type specific
 * parameters.
 *
 * The serialization format will be Protobuf.
 */
struct econn_message {

	/* common types: */
	enum econn_msg msg_type;
	char sessid_sender[64];
	char dest_userid[64];
	char dest_clientid[64];
	bool resp;
	bool transient;

	uint32_t time; /* in seconds */
	uint32_t age; /* in seconds */

	/* message-specific types: */
	union {
		struct setup {
			char *sdp_msg;
			struct econn_props *props;
		} setup;

		struct propsync {
			struct econn_props *props;
		} propsync;

		struct devpair_accept {
			char *sdp;
		} devpair_accept;
		
		struct devpair_publish {
			struct zapi_ice_server *turnv;
			size_t turnc;
			char *sdp;
			char *username;
		} devpair_publish;

		struct alert {
			uint32_t level;
			char *descr;
		} alert;

		struct groupstart {
			struct econn_props *props;
		} groupstart;
	} u;
};


struct econn;


/**
 * Indicates an incoming call on this ECONN.
 * Should only be called once per ECONN.
 */
typedef void (econn_conn_h)(struct econn *econn,
			    uint32_t msg_time,
			    const char *userid_sender,
			    const char *clientid_sender,
			    uint32_t age,
			    const char *sdp,
			    struct econn_props *props,
			    void *arg);

/**
 * Indicates that the remote peer answered the call.
 * Should only be called once per ECONN.
 */
typedef void (econn_answer_h)(struct econn *econn,
			      bool reset, const char *sdp,
			      struct econn_props *props, void *arg);

/**
 * Indicates that the remote peer offers a SDP update
 */
typedef void (econn_update_req_h)(struct econn *econn,
				  const char *userid_sender,
				  const char *clientid_sender,
				  const char *sdp,
				  struct econn_props *props,
				  bool should_reset,
				  void *arg);

/**
 * Indicates that the remote peer answered an UPDATE request.
 */
typedef void (econn_update_resp_h)(struct econn *econn, const char *sdp,
				   struct econn_props *props, void *arg);

typedef void (econn_alert_h)(struct econn *econn, uint32_t level,
			     const char *descr, void *arg);


/**
 * Indicates that this ECONN was closed, locally or by remote peer
 * Should only be called once per ECONN.
 */
typedef void (econn_close_h)(struct econn *econn, int err, uint32_t msg_time, void *arg);


/* transport */

typedef int  (econn_transp_send_h)(struct econn *conn,
				   struct econn_message *msg, void *arg);


struct econn_transp {
	econn_transp_send_h *sendh;
	void *arg;
};

/**
 * Econn configuration values
 */
struct econn_conf {
	uint32_t timeout_setup;  /* Timer Tp after send/recv SETUP [ms] */
	uint32_t timeout_term;   /* Timer Tt after terminating [ms]     */
};


/* econn object */


int  econn_alloc(struct econn **econnp,
		 const struct econn_conf *conf,
		 const char *userid_self,
		 const char *clientid,
		 struct econn_transp *transp,
		 econn_conn_h *connh,
		 econn_answer_h *answerh,
		 econn_update_req_h *update_reqh,
		 econn_update_resp_h *update_resph,
		 econn_alert_h *alerth,
		 econn_close_h *closeh, void *arg);
int  econn_start(struct econn *conn, const char *sdp,
		 const struct econn_props *props);
int  econn_answer(struct econn *conn, const char *sdp,
		  const struct econn_props *props);

/* UPDATE */
int  econn_update_req(struct econn *conn, const char *sdp,
		      const struct econn_props *props);
int  econn_update_resp(struct econn *conn, const char *sdp,
		       const struct econn_props *props);

void econn_end(struct econn *conn);
void econn_close(struct econn *conn, int err, uint32_t msg_time);
void econn_set_state(struct econn *conn, enum econn_state state);
enum econn_state econn_current_state(const struct econn *conn);
enum econn_dir econn_current_dir(const struct econn *conn);
const char *econn_userid_remote(const struct econn *conn);
const char *econn_clientid_remote(const struct econn *conn);
const char *econn_sessid_local(const struct econn *conn);
const char *econn_sessid_remote(const struct econn *conn);
int  econn_debug(struct re_printf *pf, const struct econn *econn);

void econn_set_datachan_established(struct econn *conn);

void econn_set_error(struct econn *conn, int err);
int  econn_send_alert(struct econn *conn, uint32_t level, const char *descr);

void econn_recv_message(struct econn *conn,
		 const char *userid_sender,
		 const char *clientid_sender,
		 const struct econn_message *msg);


/* helper functions */

const char *econn_msg_name(enum econn_msg msg);
const char *econn_state_name(enum econn_state st);
const char *econn_dir_name(enum econn_dir dir);
const char *econn_transp_name(enum econn_transport tp);
bool econn_iswinner(const char *userid_self, const char *clientid,
		    const char *userid_remote, const char *clientid_remote);
bool econn_is_creator(const char *userid_self, const char *userid_remote,
		      const struct econn_message *msg);
enum econn_transport econn_transp_resolve(enum econn_msg type);


/* econn message */

struct econn_message *econn_message_alloc(void);
int  econn_message_init(struct econn_message *msg, enum econn_msg msg_type,
			const char *sessid_sender);
void econn_message_reset(struct econn_message *msg);
bool econn_message_isrequest(const struct econn_message *msg);
int  econn_message_print(struct re_printf *pf,
			 const struct econn_message *msg);
int  econn_message_brief(struct re_printf *pf,
			 const struct econn_message *msg);


/* econn properties */

int  econn_props_alloc(struct econn_props **epp, struct odict *dict);
int  econn_props_add(struct econn_props *props,
		     const char *key, const char *val);
int  econn_props_update(struct econn_props *props, const char *key,
			const char *val);
const char *econn_props_get(const struct econn_props *props, const char *key);
int  econn_props_print(struct re_printf *pf, const struct econn_props *props);


bool econn_can_send_propsync(const struct econn *econn);
int econn_send_propsync(struct econn *conn, bool resp,
			struct econn_props *props);


struct vector {
	uint8_t *bytes;
	size_t len;
};

int vector_alloc(struct vector **vecp, const uint8_t *bytes, size_t len);
