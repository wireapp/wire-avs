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

#define CREQ_POST_FLOWS "/conversations/%s/call/flows/v2"
#define CREQ_FLOWS "/conversations/%s/call/flows"
#define CREQ_LSDP  "/conversations/%s/call/flows/%s/local_sdp"
#define CREQ_LCAND "/conversations/%s/call/flows/%s/local_candidates"
#define CREQ_METRICS "/conversations/%s/call/metrics"
#define CREQ_LOG ""
#define CREQ_CONFIG "/calls/config"


#define HTTP_POST   "POST"
#define HTTP_PUT    "PUT"
#define HTTP_DELETE "DELETE"
#define HTTP_GET    "GET"

#define CTYPE_JSON "application/json"


struct flow;


enum flowmgr_estab {
	FLOWMGR_ESTAB_NONE   = 0,
	FLOWMGR_ESTAB_ICE    = 0x1,
	FLOWMGR_ESTAB_ACTIVE = 0x2,
	FLOWMGR_ESTAB_MEDIA  = FLOWMGR_ESTAB_ICE | FLOWMGR_ESTAB_ACTIVE,
	FLOWMGR_ESTAB_RTP    = 0x4,
};

struct mflow_stats {
	int estab_time;
	int time;
	char ltype[8];
	char rtype[8];
	struct sa sa;
	char codec[8];
	char crypto[16];
};

enum userflow_state {
	USERFLOW_STATE_IDLE = 0,
	USERFLOW_STATE_POST,
	USERFLOW_STATE_RESTART,
	USERFLOW_STATE_ANSWER,
	USERFLOW_STATE_OFFER,
};

enum userflow_signal_state {
	USERFLOW_SIGNAL_STATE_STABLE = 0,
	USERFLOW_SIGNAL_STATE_HAVE_LOCAL_OFFER,
	USERFLOW_SIGNAL_STATE_HAVE_REMOTE_OFFER,
	USERFLOW_SIGNAL_STATE_UNKNOWN,
};

struct userflow {
	struct call *call;  /* pointer to parent */
	struct flow *flow;
	enum userflow_state state;
	enum userflow_signal_state signal_state;
	
	char *userid;
	char *name;

	struct mediaflow *mediaflow;

	struct {
		char *type; /* type of sdp, offer or answer */
		char *sdp;
		size_t len;
		
		bool ready;

		bool async_answer;
		bool async_offer;
	} sdp;

	unsigned num_if;
};


struct call {
	struct dict *flows;  /* struct flow */
	struct list conf_parts;
	struct dict *users;  /* struct userflow */
	char *convid;
	char *sessid;
	enum flowmgr_mcat mcat; /* current media category of call */
	bool catchg_pending;
	unsigned ix_ctr;

	struct flowmgr *fm;  /* owner */

	struct list rrl;
	struct list ghostl;

	uint64_t start_ts;
	uint64_t rtp_start_ts;
	bool rtp_started;
	bool is_mestab;
	bool active;

	struct le post_le;
};


typedef void (mflow_volume_h) (struct flow *flow,
			       float invol, float outvol);

struct flow {
	struct le le; /* member of flows list */
	struct call *call;  /* pointer to owner */
	char *flowid;
	char *remoteid;  /* user ID of far end  */
	int err;
	bool creator;
	bool deleted;
	int ix;
	uint64_t estabts;

	enum flowmgr_estab est_st;

	bool got_sdp;
	struct list pendingl;  /* pending 'struct cand' */

	uint64_t startts;
	bool active;
	bool estab;

	struct mflow_stats stats;

	mflow_volume_h *volh;          // XXX: remove this

	struct {
		flowmgr_video_state_change_h *state_change_h;
		flowmgr_render_frame_h *render_frame_h;
		void *arg;
	} video;

	struct conf_part *cp;

	struct userflow *userflow;	
};


struct flow_elem {
	struct call *call;
	const char *flowid;
	const char *creator;
	bool has_creator;
	bool is_creator;
	struct json_object *jflow;

	struct le le;
};

struct flowmgr {
	struct dict *calls;  /* struct call */

	struct list rrl;

	struct list postl;

	/* Sync engine callbacks
	 */
	flowmgr_req_h *reqh;
	flowmgr_err_h *errh;
	void *sarg;

	/* Media manager callbacks
	 */
	flowmgr_mcat_chg_h *cath;
	flowmgr_volume_h *volh;
	void *marg;

	/* Media established handler */
	flowmgr_media_estab_h *mestabh;
	void *mestab_arg;

	/* Conference position handler */
	flowmgr_conf_pos_h *conf_posh;
	void *conf_pos_arg;

	/* log handlers */
	struct {
		flowmgr_log_append_h *appendh;
		flowmgr_log_upload_h *uploadh;
		void *arg;
	} log;

	/* username handlers */
	struct {
		flowmgr_username_h *nameh;
		void *arg;
	} username;

	/* Event callback -- for debugging */
	flowmgr_event_h *evh;
	void *evarg;
	
	int trace;

	/* Video handlers */
	struct {
		flowmgr_video_state_change_h *state_change_h;
		flowmgr_render_frame_h *render_frame_h;
		void *arg;
	} video;

	bool use_metrics;

	struct list eventq;

	struct {
		struct call_config cfg;
		struct tmr tmr;
		struct rr_resp *rr;
		call_config_h *configh;
		bool pending;
		bool ready;
		void *arg;
	} config;

	struct le le;
	char self_userid[64];

	struct {
		struct rest_cli *cli;
		struct login_token token;
	} rest;
};


typedef void (rr_resp_h)(int status, struct rr_resp *rr,
			 struct json_object *jobj, void *arg);

struct rr_resp {
	uint32_t magic;
	struct le le;
	struct le call_le;

	struct flowmgr *fm;
	struct call *call;
	uint64_t ts_req;
	uint64_t ts_resp;
	char debug[256];
	rr_resp_h *resph;
	void *arg;
};


/* Internal flowmgr */
int flowmgr_post_flows(struct call *call);
const char *flowmgr_mediacat_name(enum flowmgr_mcat mcat);
int flowmgr_send_request(struct flowmgr *fm, struct call *call,
		         struct rr_resp *rr,
		         const char *path, const char *method,
		         const char *ctype, struct json_object *jobj);
void flowmgr_silencing(bool silenced);
int  flowmgr_update_conf_parts(struct list *decl);



/* call */
int  call_alloc(struct call **callp, struct flowmgr *fm, const char *convid);
int  call_lookup_alloc(struct call **callp, bool *allocated,
		       struct flowmgr *fm, const char *convid);
int call_userflow_lookup_alloc(struct userflow **ufp,
			       bool *allocated,
			       struct call *call,
			       const char *userid, const char *username);
void call_flush_users(struct call *call);
void call_cancel(struct call *call);
int  call_add_flow(struct call *call, struct userflow *uf, struct flow *flow);
int  call_set_active(struct call *call, bool active);
void call_remove_flow(struct call *call, const char *flowid);
struct flow *call_find_flow(struct call *call, const char *flowid);
bool call_has_flow(struct call *call, struct flow *flow);
int call_post_flows(struct call *call);
int call_postponed_flows(struct call *call);
struct list *call_conf_parts(struct call *call);
struct json_object *call_userflow_sdp(struct call *call);

int  call_mcat_change(struct call *call, enum flowmgr_mcat mcat);
int  call_mcat_changed(struct call *call, enum flowmgr_mcat mcat);
bool call_update_media(struct call *call);
int  call_repost(struct call *call);
int  call_interruption(struct call *call, bool interrupted);
void call_check_and_post(struct call *call);
void call_purge_users(struct call *call);



struct flowmgr *call_flowmgr(struct call *call);
const char *call_convid(const struct call *call);
int call_set_sessid(struct call *call, const char *sessid);
bool call_has_good_flow(const struct call *call);
bool call_is_multiparty(const struct call *call);
bool call_cat_chg_pending(const struct call *call);
struct flow *call_best_flow(const struct call *call);
int call_deestablish_media(struct call *call);
bool call_has_media(struct call *call);
void call_rtp_started(struct call *call, bool started);
void call_mestab_check(struct call *call);

enum flowmgr_mcat call_mcat(const struct call *call);

bool call_active_handler(char *key, void *val, void *arg);
int  call_debug(struct re_printf *pf, const struct call *call);
bool call_debug_handler(char *key, void *val, void *arg);
bool call_stats_prepare(struct call *call, struct json_object *jobj);
void call_ghost_flow_handler(int status, struct rr_resp *rr,
			     struct json_object *jobj, void *arg);
void call_restart(struct call *call);
bool call_restart_handler(char *key, void *val, void *arg);
int  call_add_conf_part(struct call *call, struct flow *flow);
void call_remove_conf_part(struct call *call, struct flow *flow);
void call_mestab(struct call *call, bool mestab);


bool call_can_send_video(struct call *call);
void call_video_rcvd(struct call *call, bool rcvd);
void call_set_video_send_active(struct call *call, bool video_active);
bool call_is_sending_video(struct call *call, const char *partid);

struct flow *call_find_remote_user(const struct call *call,
				   const char *remote_user);


/* flow */
int flow_alloc(struct flow **flowp, struct call *call,
	       const char *flowid, const char *remoteid,
	       bool creator, bool active);
void flow_mediaflow_estab(struct flow *flow,
			  const char *crypto, const char *codec,
			  const char *ltype, const char *rtype,
			  const struct sa *sa);
void flow_update_media(struct flow *flow);
int  flow_activate(struct flow *flow, bool active);
bool flow_is_active(const struct flow *flow);
bool flow_has_ice(struct flow *flow);
const char *flow_flowid(struct flow *flow);
const char *flow_remoteid(struct flow *flow);
void flow_error(struct flow *flow, int err);
void flow_delete(struct call *call, const char *flowid,
		 struct rr_resp *rr, int err);

struct call *flow_call(struct flow *flow);
int  flow_set_volh(struct flow *flow, mflow_volume_h *);
int  flow_interruption(struct flow *flow, bool interrupted);

bool flow_good_handler(char *key, void *val, void *arg);
bool flow_deestablish_media(char *key, void *val, void *arg);
bool flow_count_active_handler(char *key, void *val, void *arg);
bool flow_active_handler(char *key, void *val, void *arg);
bool flow_stats_handler(char *key, void *val, void *arg);
bool flow_best_handler(char *key, void *val, void *arg);
bool flow_restart_handler(char *key, void *val, void *arg);
void flow_vol_handler(struct flow *flow, bool using_voe);
bool flow_lookup_part_handler(char *key, void *val, void *arg);
void flow_ice_resp(int status, struct rr_resp *rr,
		   struct json_object *jobj, void *arg);
void flow_rtp_start(struct flow *flow, bool started, bool video_started);

void flow_set_video_handlers(struct flow *flow,
				flowmgr_video_state_change_h *state_change_h,
				flowmgr_render_frame_h *render_frame_h,
				void *arg);
bool flow_can_send_video(struct flow *flow);
bool flow_is_sending_video(struct flow *flow);
void flow_set_video_send_active(struct flow *flow, bool video_active);

int  flow_debug(struct re_printf *pf, const struct flow *flow);
bool flow_debug_handler(char *key, void *val, void *arg);


/* Protocol event handlers */
int  flow_act_handler(struct flow *flow, struct json_object *jobj);
int  flow_del_handler(struct flow *flow);
int  flow_sdp_handler(struct flow *flow, struct json_object *jobj,
		      bool replayed);
int  flow_cand_handler(struct flow *flow, struct json_object *jobj);

void flow_local_sdp_req(struct flow *flow, const char *type, const char *sdp);

/* rr */
int  rr_alloc(struct rr_resp **rrp, struct flowmgr *fm, struct call *call,
	      rr_resp_h *resph, void *arg);
void rr_cancel(struct rr_resp *rr);
void rr_response(struct rr_resp *rr);
bool rr_isvalid(const struct rr_resp *rr);


void msystem_start_volume(void);
void msystem_cancel_volume(void);
struct tls *msystem_dtls(void);
struct list *msystem_aucodecl(void);
struct list *msystem_vidcodecl(void);
struct list *msystem_flows(void);
bool msystem_get_loopback(void);
bool msystem_get_privacy(void);
const char *msystem_get_interface(void);

int  marshal_init(void);
void marshal_close(void);


bool flowmgr_is_using_voe(void);

/* Userflow */

int  userflow_alloc(struct userflow **ufp,
		    struct call *call, const char *userid, const char *name);
void userflow_set_flow(struct userflow *uf, struct flow *flow);
int  userflow_generate_offer(struct userflow *uf);
bool userflow_check_sdp_handler(char *key, void *val, void *arg);
int  userflow_alloc_mediaflow(struct userflow *uf);
void userflow_release_mediaflow(struct userflow *uf);

struct mediaflow *userflow_mediaflow(struct userflow *uf);
void userflow_set_state(struct userflow *uf, enum userflow_state state);
bool userflow_debug_handler(char *key, void *val, void *arg);
const char *userflow_state_name(enum userflow_state st);
const char *userflow_signal_state_name(enum userflow_signal_state st);
bool userflow_sdp_isready(struct userflow *uf);

enum userflow_signal_state userflow_signal_state(struct userflow *uf);

/* SDP offer */
int userflow_accept(struct userflow *uf, const char *sdp);

/* SDP answer */
int userflow_update(struct userflow *uf, const char *sdp);

int userflow_update_config(struct userflow *uf);

