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


#include <re.h>
#include <avs.h>
#include "avs_wcall.h"

#define EGCALL_START_TIMEOUT           (30000)
#define EGCALL_SHOULD_RING_TIMEOUT     (30000)
#define EGCALL_RING_TIMEOUT            (35000)
#define EGCALL_ANSWER_TIMEOUT          (60000)
#define EGCALL_ACTIVE_ROSTER_TIMEOUT   (60000)
#define EGCALL_ACTIVE_ROSTER_RAND      (30000)
#define EGCALL_PASSIVE_ROSTER_TIMEOUT (120000)

struct media_entry {
	struct ecall *ecall;
	bool started;

	struct le le;
};

struct egcall {
	struct icall icall;
	struct list ecalll;
	struct list media_startl;
	enum egcall_state state;
	char *convid;
	char *userid_self;
	char *clientid_self;
	const struct ecall_conf *conf;
	int call_type;

	struct dict *roster;
	struct {
		struct list partl;
	} conf_pos;

	struct zapi_ice_server turnv[MAX_TURN_SERVERS];
	size_t turnc;
	
	/* TODO replace this with an ONGOING state */
	bool is_call_answered;
	struct tmr call_timer;
	struct tmr roster_timer;

	bool audio_cbr;

	struct {
		enum icall_vstate send_state;
	} video;

	struct {
		int interval;
	} quality;
};

struct roster_item {
	char *userid;
	char *key;

	bool audio_estab;
	int video_recv;
};

static void egcall_start_timeout(void *arg);
static void egcall_ring_timeout(void *arg);
static void egcall_answer_timeout(void *arg);
static void egcall_active_roster_timeout(void *arg);
static void egcall_passive_roster_timeout(void *arg);
static void egcall_start_active_roster_timer(struct egcall *egcall);
static void egcall_end_with_err(struct egcall *egcall, int err);


static void media_entry_destructor(void *arg)
{
	struct media_entry *me = arg;

	list_unlink(&me->le);
}


static bool all_media_stopped(struct egcall *egcall)
{
	bool started = false;
	struct le *le;

	for(le = egcall->media_startl.head; le && !started; le = le->next) {
		struct media_entry *me = le->data;

		started = me->started;
	}

	return !started;
}


static struct media_entry *lookup_media_entry(struct egcall *egcall,
					      struct ecall *ecall)
{
	struct media_entry *me = NULL;
	struct le *le;

	for(le = egcall->media_startl.head; le && me == NULL; le = le->next) {
		me = le->data;

		if (me->ecall != ecall)
			me = NULL;
	}

	return me;
}


static void set_media_started(struct egcall *egcall, struct ecall *ecall,
			      bool started)
{
	struct media_entry *me;

	me = lookup_media_entry(egcall, ecall);
	if (me)
		me->started = started;
}


static void destroy_ecall(struct egcall *egcall, struct ecall *ecall)
{
	struct media_entry *me;

	me = lookup_media_entry(egcall, ecall);
	if (me)
		mem_deref(me);

	mem_deref(ecall);
}


static void roster_destructor(void *arg)
{
	struct roster_item *item = arg;

	mem_deref(item->userid);
	mem_deref(item->key);
}


static int get_userclient(char** userclient,
			  const char* userid, const char *clientid)
{
	char buf[128];

	re_snprintf(buf, sizeof(buf), "%s.%s", userid, clientid);

	return str_dup(userclient, buf);
}


static void update_conf_pos(struct egcall *egcall)
{
	int err;

	info("egcall(%p): update_conf_pos called\n", egcall );

	conf_pos_sort(&egcall->conf_pos.partl);

	err = msystem_update_conf_parts(&egcall->conf_pos.partl);
	if (err) {
		warning("egcall: msystem_update_conf_parts error (%m)\n", err);
	}

	ICALL_CALL_CB(egcall->icall, group_changedh,
		egcall->icall.arg);	
}

static struct roster_item *roster_add(struct egcall *egcall,
		       const char *userid,
		       const char *clientid)
{
	struct roster_item *item;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;
	char *uc;

	if (!egcall || !userid || !clientid) {
		return NULL;
	}

	err = get_userclient(&uc, userid, clientid);
	if (err)
		return NULL;

	item = dict_lookup(egcall->roster, uc);
	mem_deref(uc);

	if (item) {
		return item;
	}

	item = mem_zalloc(sizeof(*item), roster_destructor);
	if (!item) {
		err = ENOMEM;
		goto out;
	}

	str_dup(&item->userid, userid);
	err = get_userclient(&item->key, userid, clientid);
	if (err) {
		warning("egcall: unable to find user to add to roster, userid=%s\n",
			anon_id(userid_anon, userid));
		goto out;
	}
	err = dict_add(egcall->roster, item->key, item);
	if (err && err != EADDRINUSE) {
		warning("egcall: unable to add user to roster dictionary, userid=%s\n",
			anon_id(userid_anon, userid));
		goto out;
	}

	egcall->is_call_answered = false;

	info("egcall(%p): roster_add %s.%s len=%u\n", egcall,
	      anon_id(userid_anon, userid), anon_client(clientid_anon, clientid),
	      dict_count(egcall->roster));
out:
	mem_deref(item);

	return item;
}

static void roster_remove(struct egcall *egcall, const char* userid,
			  const char *clientid)
{
	int err = 0;
	struct ecall *ecall;
	char *userclient = NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	err = get_userclient(&userclient, userid, clientid);
	if (err) {
		warning("egcall: unable to remove user from roster, "
			"userid=%s\n", anon_id(userid_anon, userid));
		goto out;
	}

	dict_remove(egcall->roster, userclient);

	info("egcall(%p): roster_remove %s.%s len=%u\n",
	     egcall, 
	     anon_id(userid_anon, userid), anon_client(clientid_anon, clientid),
	     dict_count(egcall->roster));

	ecall = ecall_find_userid(&egcall->ecalll, userid);
	if (ecall) {
		ecall_set_conf_part(ecall, NULL);
		update_conf_pos(egcall);
	}
	
out:
	mem_deref(userclient);
}

static struct roster_item *roster_lookup(struct egcall *egcall,
					 struct ecall *ecall)
{	
	const char *uid = ecall_get_peer_userid(ecall);
	const char *cid = ecall_get_peer_clientid(ecall);
	struct roster_item *ri = NULL;
	char *uc;
	int err;

	err = get_userclient(&uc, uid, cid);
	if (err)
		return NULL;

	ri = dict_lookup(egcall->roster, uc);
	mem_deref(uc);
	
	return ri;
}


static int add_ecall(struct ecall **ecallp, struct egcall *egcall,
		     const char *userid_peer, const char *clientid_peer);

static void destructor(void *arg)
{
	struct egcall *egcall = arg;

	tmr_cancel(&egcall->call_timer);
	tmr_cancel(&egcall->roster_timer);
	list_flush(&egcall->media_startl);
	list_flush(&egcall->ecalll);
	mem_deref(egcall->convid);
	mem_deref(egcall->userid_self);
	mem_deref(egcall->clientid_self);

	dict_flush(egcall->roster);
	mem_deref(egcall->roster);
}

const char *egcall_state_name(enum egcall_state state)
{
	switch (state) {
		case EGCALL_STATE_NONE:
			return "EGCALL_STATE_NONE";

		case EGCALL_STATE_IDLE:
			return "EGCALL_STATE_IDLE";

		case EGCALL_STATE_OUTGOING:
			return "EGCALL_STATE_OUTGOING";

		case EGCALL_STATE_INCOMING:
			return "EGCALL_STATE_INCOMING";

		case EGCALL_STATE_ANSWERED:
			return "EGCALL_STATE_ANSWERED";

		case EGCALL_STATE_ACTIVE:
			return "EGCALL_STATE_ACTIVE";

		case EGCALL_STATE_TERMINATING:
			return "EGCALL_STATE_TERMINATING";

		default:
			return "???";
	}
}

static void set_state(struct egcall* egcall, enum egcall_state state)
{
	info("egcall(%p): State changed: `%s' --> `%s'\n",
		egcall,
		egcall_state_name(egcall->state),
		egcall_state_name(state));

	egcall->state = state;

	switch(egcall->state) {
	case EGCALL_STATE_IDLE:
		tmr_cancel(&egcall->call_timer);
		tmr_cancel(&egcall->roster_timer);
		break;

	case EGCALL_STATE_OUTGOING:
		egcall->is_call_answered = true;
		tmr_start(&egcall->call_timer, EGCALL_START_TIMEOUT,
			egcall_start_timeout, egcall);
		egcall_start_active_roster_timer(egcall);
		break;

	case EGCALL_STATE_INCOMING:
		tmr_cancel(&egcall->call_timer);
		tmr_start(&egcall->roster_timer, EGCALL_PASSIVE_ROSTER_TIMEOUT, 
			egcall_passive_roster_timeout, egcall);
		break;

	case EGCALL_STATE_ANSWERED:
		egcall->is_call_answered = true;
		tmr_start(&egcall->call_timer, EGCALL_ANSWER_TIMEOUT,
			egcall_answer_timeout, egcall);
		egcall_start_active_roster_timer(egcall);
		break;

	case EGCALL_STATE_ACTIVE:
		egcall->is_call_answered = true;
		tmr_cancel(&egcall->call_timer);
		egcall_start_active_roster_timer(egcall);
		break;

	case EGCALL_STATE_TERMINATING:
	case EGCALL_STATE_NONE:
		tmr_cancel(&egcall->call_timer);
		tmr_cancel(&egcall->roster_timer);
		break;

	}
}


static int send_msg(struct egcall *egcall, enum econn_msg type,
		    bool resp, bool transient)
{
	struct econn_message *msg = NULL;
	int err = 0;

	if (egcall == NULL) {
		return EINVAL;
	}

	msg = econn_message_alloc();
	if (msg == NULL) {
		return ENOMEM;
	}

	msg->msg_type = type;
	msg->resp = resp;
	msg->transient = transient;

	if (type == ECONN_GROUP_START) {
		/* Add videosend prop */
		err = econn_props_alloc(&msg->u.groupstart.props, NULL);
		if (err)
			goto skipprops;

		err = econn_props_add(msg->u.groupstart.props, "videosend",
			egcall->call_type == WCALL_CALL_TYPE_VIDEO ? "true"
				                                   : "false");
		if (err)
			goto skipprops;
	}

skipprops:
	err = ICALL_CALL_CBE(egcall->icall, sendh, egcall->userid_self, msg, egcall->icall.arg);
	if (err != 0) {
		goto out;
	}

out:
	mem_deref(msg);

	return err;
}

static void egcall_start_timeout(void *arg)
{
	struct egcall *egcall = arg;

	info("egcall(%p): start_timeout state=%s\n",
	     egcall, egcall_state_name(egcall->state));
	if (egcall->state != EGCALL_STATE_ACTIVE) {
		dict_flush(egcall->roster);
		egcall_end_with_err(egcall, ETIMEDOUT);
	}
}

static void egcall_ring_timeout(void *arg)
{
	struct egcall *egcall = arg;

	info("egcall(%p): ring_timeout state=%s\n",
	     egcall, egcall_state_name(egcall->state));
	
	if (egcall->state == EGCALL_STATE_INCOMING) {
		ICALL_CALL_CB(egcall->icall, leaveh, EGCALL_REASON_STILL_ONGOING,
				ECONN_MESSAGE_TIME_UNKNOWN, egcall->icall.arg);
	}
}

static void egcall_answer_timeout(void *arg)
{
	struct egcall *egcall = arg;

	info("egcall(%p): answer_timeout state=%s\n", egcall, egcall_state_name(egcall->state));
	if (egcall->state != EGCALL_STATE_ACTIVE) {
		dict_flush(egcall->roster);
		egcall_end_with_err(egcall, ETIMEDOUT);
	}
}

static void egcall_active_roster_timeout(void *arg)
{
	struct egcall *egcall = arg;
	int err = 0;

	info("egcall(%p): active_roster_timeout state=%s\n", egcall, egcall_state_name(egcall->state));
	if (egcall->state == EGCALL_STATE_ACTIVE) {
		err = send_msg(egcall, ECONN_GROUP_CHECK, true, true);
		if (err != 0) {
			warning("egcall(%p): active_roster_timeout failed to send msg err=%d\n",
				egcall, err);
		}
		egcall_start_active_roster_timer(egcall);
	}
}

static void egcall_passive_roster_timeout(void *arg)
{
	struct egcall *egcall = arg;

	info("egcall(%p): passive_roster_timeout state=%s\n", egcall, egcall_state_name(egcall->state));
	dict_flush(egcall->roster);
	set_state(egcall, EGCALL_STATE_IDLE);
	ICALL_CALL_CB(egcall->icall, closeh, 
		0, NULL, &egcall->icall, ECONN_MESSAGE_TIME_UNKNOWN,
		NULL, NULL, egcall->icall.arg);
}

static void egcall_start_active_roster_timer(struct egcall *egcall)
{
	uint32_t timeout;
	timeout = EGCALL_ACTIVE_ROSTER_TIMEOUT + (rand() % EGCALL_ACTIVE_ROSTER_RAND);
	info("egcall(%p): start_active_roster_timer %u\n",
		egcall, timeout);
	tmr_start(&egcall->roster_timer, timeout, egcall_active_roster_timeout, egcall);
}

int egcall_alloc(struct egcall **egcallp,
		 const struct ecall_conf *conf,		 
		 const char *convid,
		 const char *userid_self,
		 const char *clientid)
{
	struct egcall *egcall = NULL;
	int err = 0;

	if (convid == NULL || egcallp == NULL) {
		return EINVAL;
	}

	egcall = mem_zalloc(sizeof(*egcall), destructor);
	if (egcall == NULL) {
		return ENOMEM;
	}

	err = dict_alloc(&egcall->roster);
	if (err)
		goto out;

	list_init(&egcall->conf_pos.partl);
	
	err = str_dup(&egcall->convid, convid);
	err |= str_dup(&egcall->userid_self, userid_self);
	err |= str_dup(&egcall->clientid_self, clientid);
	if (err)
		goto out;

	egcall->conf = conf;
	egcall->state = EGCALL_STATE_IDLE;

	list_init(&egcall->conf_pos.partl);
	
	tmr_init(&egcall->call_timer);
	tmr_init(&egcall->roster_timer);

	icall_set_functions(&egcall->icall,
		egcall_add_turnserver,
		egcall_start,
		egcall_answer,
		egcall_end,
		egcall_media_start,
		egcall_media_stop,
		egcall_set_video_send_state,
		egcall_msg_recv,
		egcall_get_members,
		egcall_set_quality_interval,
		egcall_debug);
out:
	if (err == 0) {
		*egcallp = egcall;
	}
	else {
		mem_deref(egcall);
	}

	return err;
}

struct icall *egcall_get_icall(struct egcall *egcall)
{
	return &egcall->icall;
}

int egcall_add_turnserver(struct icall *icall,
			  struct zapi_ice_server *srv)
{
	struct egcall *egcall = (struct egcall*)icall;
	int err = 0;

	if (!egcall || !srv)
		return EINVAL;

	if (egcall->turnc >= ARRAY_SIZE(egcall->turnv))
		return EOVERFLOW;
	
	info("egcall(%p): add_turnserver: %s\n", egcall, srv->url);

	egcall->turnv[egcall->turnc] = *srv;
	++egcall->turnc;

	return err;
}


int egcall_start(struct icall *icall, enum icall_call_type call_type,
		 bool audio_cbr, void *extcodec_arg)
{
	struct egcall *egcall = (struct egcall*)icall;
	int err = 0;

	(void)extcodec_arg;

	if (egcall == NULL) {
		return EINVAL;
	}

	egcall->call_type = call_type;
	egcall->audio_cbr = audio_cbr;

	info("egcall(%p): egcall_start state=%s\n", egcall, egcall_state_name(egcall->state));
	if (egcall->state != EGCALL_STATE_IDLE &&
		egcall->state != EGCALL_STATE_INCOMING) {

		return EALREADY;
	}

	err = send_msg(egcall, ECONN_GROUP_START, false, false);
	if (err != 0) {
		goto out;
	}

	set_state(egcall, EGCALL_STATE_OUTGOING);
out:
	return err;
}

int egcall_answer(struct icall *icall, enum icall_call_type call_type,
		  bool audio_cbr, void *extcodec_arg)
{
	struct egcall *egcall = (struct egcall*)icall;
	int err = 0;

	(void)extcodec_arg;

	if (egcall == NULL) {
		return EINVAL;
	}

	egcall->call_type = call_type;
	egcall->audio_cbr = audio_cbr;
	
	info("egcall(%p): egcall_answer state=%s\n", egcall, egcall_state_name(egcall->state));
	if (egcall->state != EGCALL_STATE_INCOMING) {
		return EPROTO;
	}

	err = send_msg(egcall, ECONN_GROUP_START, true, false);
	if (err != 0) {
		goto out;
	}

	set_state(egcall, EGCALL_STATE_ANSWERED);

out:
	return err;
}


static void send_leave(struct egcall *egcall, uint32_t msg_time, int err)
{
	int send_err = 0;
	uint32_t rcount = 0;

	send_err = send_msg(egcall, ECONN_GROUP_LEAVE, false, false);
	if (send_err != 0) {
		warning("egcall(%p): end failed err: %d\n", egcall, send_err);
	}

	rcount = dict_count(egcall->roster);
	if (rcount == 0) {
		set_state(egcall, EGCALL_STATE_IDLE);
		info("egcall(%p): send_leave no ecalls in list, closing\n",
		     egcall);
		ICALL_CALL_CB(egcall->icall, closeh, 
			0, NULL, &egcall->icall, ECONN_MESSAGE_TIME_UNKNOWN,
			NULL, NULL, egcall->icall.arg);
	}
	else {
		info("egcall(%p): send_leave %u ecalls in list, leaving\n",
		     egcall, rcount);
		set_state(egcall, EGCALL_STATE_INCOMING);
		ICALL_CALL_CB(egcall->icall, leaveh, 
			EGCALL_REASON_STILL_ONGOING, msg_time, egcall->icall.arg);
	}
}

static void egcall_end_with_err(struct egcall *egcall, int err)
{
	uint32_t ecount = 0;

	if (egcall == NULL) {
		return;
	}

	ecount = list_count(&egcall->ecalll);
	info("egcall(%p): egcall_end state=%s ecalls=%u\n", egcall,
		egcall_state_name(egcall->state), ecount);
	if (egcall->state == EGCALL_STATE_IDLE ||
		egcall->state == EGCALL_STATE_INCOMING ||
		egcall->state == EGCALL_STATE_TERMINATING) {
		return;
	}

	if (ecount > 0) {
		struct le *le;

		set_state(egcall, EGCALL_STATE_TERMINATING);
		LIST_FOREACH(&egcall->ecalll, le) {
			struct ecall *ecall = le->data;

			ecall_end(ecall);
		}
	}
	else {
		send_leave(egcall, ECONN_MESSAGE_TIME_UNKNOWN, err);
	}
}


void egcall_end(struct icall *icall)
{
	struct egcall *egcall = (struct egcall*)icall;
	egcall_end_with_err(egcall, 0);
}

static void ecall_setup_handler(struct icall *icall,
				uint32_t msg_time,
			        const char *userid_sender,
			        bool video_call,
				bool should_ring,
			        void *arg)
{
	struct egcall *egcall = arg;
	struct ecall *ecall0 = NULL;
	char userid_anon[ANON_ID_LEN];

	(void)msg_time;
	(void)icall; /* not really used, revise code below and use directly */
	(void)should_ring;

	ecall0 = ecall_find_userid(&egcall->ecalll, userid_sender);
	
	info("ecall_setup_h user=%s ecall=%p ecall0=%p\n",
	     anon_id(userid_anon, userid_sender), icall, ecall0);
	if (ecall0) {
		ecall_answer(ecall0,
			egcall->call_type != WCALL_CALL_TYPE_FORCED_AUDIO,
			egcall->audio_cbr, NULL);
	}
}


static void ecall_setup_resp_handler(void *arg)
{
	struct egcall *egcall = arg;

	tmr_cancel(&egcall->call_timer);
}


static void ecall_media_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update, void *arg)
{
	struct egcall *egcall = arg;

	ICALL_CALL_CB(egcall->icall, media_estabh,
		icall, userid, clientid, update, egcall->icall.arg);
}

static void ecall_media_stopped_handler(struct icall *icall, void *arg)
{
	struct egcall *egcall = arg;

	set_media_started(egcall, (struct ecall*)icall, false);

	if (all_media_stopped(egcall)) {
		ICALL_CALL_CB(egcall->icall, media_stoppedh,
			icall, egcall->icall.arg);
	}
}



static void ecall_audio_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update, void *arg)
{
	struct egcall *egcall = arg;
	struct ecall *ecall = (struct ecall*)icall;

	if (egcall->state == EGCALL_STATE_ANSWERED) {
		set_state(egcall, EGCALL_STATE_ACTIVE);
	}
	else if (egcall->state != EGCALL_STATE_ACTIVE) {
		warning("egcall(%p): received audio_est in state %s\n",
			egcall, egcall_state_name(egcall->state));
	}

	if (ecall) {
		struct mediaflow *mf;
		struct roster_item *ri;
		struct conf_part *cp;

		ri = roster_add(egcall,
				ecall_get_peer_userid(ecall),
				ecall_get_peer_clientid(ecall));
		if (ri)
			ri->audio_estab = true;

		cp = ecall_get_conf_part(ecall);
		mf = ecall_mediaflow(ecall);


		/* Add conf_part only if there is no conf_part already,
		 * otherwise we might end up with multiple conf_parts
		 */
		if (cp == NULL && mf) {

			int err;

			err = conf_part_add(&cp,
					    &egcall->conf_pos.partl,
					    userid,
					    mf);
			if (!err) {
				ecall_set_conf_part(ecall, cp);
				update_conf_pos(egcall);
			}
		}
	}

	
	ICALL_CALL_CB(egcall->icall, audio_estabh,
		icall, userid, clientid, update, egcall->icall.arg);
}


static void ecall_datachan_estab_handler(struct icall *icall, const char *userid,
					 const char *clientid, bool update, void *arg)
{
	struct egcall *egcall = arg;

	ICALL_CALL_CB(egcall->icall, datachan_estabh,
		icall, userid, clientid, update, egcall->icall.arg);
}


static void ecall_vstate_handler(struct icall *icall, const char *userid,
				 const char *clientid, enum icall_vstate state, void *arg)
{
	struct egcall *egcall = arg;

	if (icall) {
		struct roster_item *ri;

		ri = roster_lookup(egcall, (struct ecall*)icall);
		if (ri)
			ri->video_recv = state;
	}

	ICALL_CALL_CB(egcall->icall, vstate_changedh,
		icall, userid, clientid, state, egcall->icall.arg);
}


static void ecall_audiocbr_handler(struct icall *icall, const char *userid,
				   const char *clientid, int enabled, void *arg)
{
#if 1
	/* For now CBR should not be signalled for group calls */
	(void)icall;
	(void)userid;
	(void)clientid;
	(void)enabled;
	(void)arg;
#else
	struct egcall *egcall = arg;

	ICALL_CALL_CB(egcall->icall, acbr_changedh,
		icall, userid, enabled, egcall->icall.arg);
#endif
}

static void ecall_close_handler(int err, const char *metrics_json,
				struct icall *icall, uint32_t msg_time,
				const char *userid, const char *clientid,
				void *arg)
{
	struct ecall *ecall = (struct ecall*)icall;
	struct egcall *egcall = arg;

	info("egcall(%p): ecall_close_handler err=%d ecall=%p\n",
	     egcall, err, ecall);
	
	if(metrics_json){
		ICALL_CALL_CB(egcall->icall, metricsh,
			metrics_json, egcall->icall.arg);
	}

	ecall_set_conf_part(ecall, NULL);
	update_conf_pos(egcall);

	if ((err != 0 || egcall->state != EGCALL_STATE_TERMINATING)
	    && userid
	    && clientid) {
		roster_remove(egcall, userid, clientid);
	}

	destroy_ecall(egcall, ecall);

	if (list_count(&egcall->ecalll) == 0) {
		send_leave(egcall, msg_time, 0);
	}
}


static int ecall_transp_send_handler(const char *userid,
				     struct econn_message *msg, void *arg)
{
	struct egcall *egcall = arg;
	int err = 0;
	char *str = NULL;

	if (!egcall->icall.sendh)
		return ENOTSUP;

	/* Remap messages to GROUP-equivalents */
	switch (msg->msg_type) {
	case ECONN_SETUP:
		msg->msg_type = ECONN_GROUP_SETUP;
		break;

	default:
		/* Keep as is */
		break;
	}

	err = ICALL_CALL_CBE(egcall->icall, sendh,
		userid, msg, egcall->icall.arg);

	mem_deref(str);

	return err;
}


static void ecall_quality_handler(struct icall *icall,
				  const char *userid,
				  int rtt, int uploss, int downloss,
				  void *arg)
{
	struct egcall *egcall = arg;

	ICALL_CALL_CB(egcall->icall, qualityh,
		icall, userid, rtt, uploss, downloss, egcall->icall.arg);
}


static int add_ecall(struct ecall **ecallp, struct egcall *egcall,
		     const char *userid_peer, const char *clientid_peer)
{
	struct ecall *ecall;
	struct media_entry *me;
	struct msystem *msys;
	size_t i;
	int err = 0;

	ecall = ecall_find_userid(&egcall->ecalll,
				  userid_peer);
	if (ecall) {
		destroy_ecall(egcall, ecall);
	}

	msys = flowmgr_msystem();
	/* If call has been answered, send SETUP */
	err = ecall_alloc(&ecall, &egcall->ecalll,
			  ICALL_CONV_TYPE_GROUP,
			  egcall->conf,
			  msys,
			  egcall->convid,
			  egcall->userid_self,
			  egcall->clientid_self);

	icall_set_callbacks(ecall_get_icall(ecall),
			    ecall_transp_send_handler,
			    ecall_setup_handler, 
			    ecall_setup_resp_handler,
			    ecall_media_estab_handler,
			    ecall_audio_estab_handler,
			    ecall_datachan_estab_handler,
			    ecall_media_stopped_handler,
			    NULL, // group_changed_handler
			    NULL, // leave_handler
			    ecall_close_handler,
			    NULL, // metrics_handler
			    ecall_vstate_handler,
			    ecall_audiocbr_handler,
			    ecall_quality_handler,
			    egcall);

	if (err) {
		warning("egcall(%p): ecall_alloc failed: %m\n",
			egcall, err);
		goto out;
	}

	me = mem_zalloc(sizeof(*me), media_entry_destructor);
	if (!me) {
		err = ENOMEM;
		goto out;
	}
	me->ecall = ecall;
	me->started = false;
	list_append(&egcall->media_startl, &me->le, me);

	ecall_set_quality_interval(ecall, egcall->quality.interval);
	
	ecall_set_peer_userid(ecall, userid_peer);
	ecall_set_peer_clientid(ecall, clientid_peer);
	ecall_set_video_send_state(ecall, egcall->video.send_state);
	ecall_set_group_mode(ecall, true);

	for (i = 0; i < egcall->turnc; ++i) {
		err = ecall_add_turnserver(ecall, &egcall->turnv[i]);
		if (err)
			goto out;
	}
	
 out:
	if (err)
		destroy_ecall(egcall, ecall);
	else
		*ecallp = ecall;

	return err;
}


static void recv_start(struct egcall *egcall,
		       const char *userid_sender,
		       const char *clientid_sender,
		       const struct econn_message *msg)
{
	struct ecall *ecall = NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_ID_LEN];
	bool video = false;
	int err = 0;
	
	info("egcall(%p): recv_start u: %s c: %s r: %s\n", egcall,
	     anon_id(userid_anon, userid_sender),
	     anon_id(clientid_anon, clientid_sender), msg->resp ? "yes" : "no");

	roster_add(egcall, userid_sender, clientid_sender);

	if (egcall->state != EGCALL_STATE_IDLE || !econn_message_isrequest(msg)) {
		egcall->is_call_answered = true;
	}
	switch (egcall->state) {
	case EGCALL_STATE_IDLE:
		set_state(egcall, EGCALL_STATE_INCOMING);
		if (egcall->icall.starth) {
			bool should_ring = econn_message_isrequest(msg) &&
				((msg->age * 1000) < EGCALL_SHOULD_RING_TIMEOUT) &&
				!strcaseeq(userid_sender, egcall->userid_self);

			if (msg->u.groupstart.props) {
				const char *vsend = econn_props_get(msg->u.groupstart.props,
					"videosend");

				if (vsend && strcmp(vsend, "true") == 0) {
					video = true;
				}
			}
			// Needs to happen after set_state
			ICALL_CALL_CB(egcall->icall, starth,
				      &egcall->icall, msg->time, userid_sender,							       video, should_ring, egcall->icall.arg);
			if (should_ring) {
				tmr_start(&egcall->call_timer, EGCALL_RING_TIMEOUT,
					egcall_ring_timeout, egcall);
			}
		}
		break;

	case EGCALL_STATE_INCOMING:
		if (!econn_message_isrequest(msg) &&
			((msg->age * 1000) < EGCALL_SHOULD_RING_TIMEOUT) &&
			strcaseeq(userid_sender, egcall->userid_self)) {

			ICALL_CALL_CB(egcall->icall, leaveh,
				EGCALL_REASON_ANSWERED_ELSEWHERE,
				msg ? msg->time : ECONN_MESSAGE_TIME_UNKNOWN,
				egcall->icall.arg);
		}
		break;

	case EGCALL_STATE_OUTGOING:
	case EGCALL_STATE_ANSWERED:
	case EGCALL_STATE_ACTIVE:
		if (strcaseeq(userid_sender, egcall->userid_self)) {
			info("egcall(%p): ignore message %s from same user (%s)\n", egcall,
			     econn_msg_name(msg->msg_type), anon_id(userid_anon, userid_sender));
			break;
		}

		ecall = ecall_find_userid(&egcall->ecalll, userid_sender);
		if (ecall &&
			(ECONN_UPDATE_SENT == ecall_state(ecall) ||
			ECONN_UPDATE_RECV == ecall_state(ecall))) {

			const char *rcid = ecall_get_peer_clientid(ecall);
			if (strcmp(rcid, clientid_sender) == 0) {
				warning("egcall(%p): recv_start: ecall already exists for %s.%s"
					" in state %s, resetting\n",
					egcall,
					anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender),
					econn_state_name(ecall_state(ecall)));

				ecall_end(ecall);
				ecall_remove(ecall);
				ecall = NULL;
			}
		}

		if (!ecall) {
			err = add_ecall(&ecall, egcall, userid_sender, clientid_sender);
			if (err) {
				warning("egcall(%p): add_ecall failed %m\n", egcall, err);
				goto out;
			}
		}

		err = ecall_start(ecall, egcall->call_type, egcall->audio_cbr, NULL);
		if (EALREADY == err) {
			err = 0;
		}

		if (err) {
			warning("egcall(%p): ecall_start failed: %m\n",
				egcall, err);
			goto out;
		}

		if (egcall->state == EGCALL_STATE_OUTGOING) {
			set_state(egcall, EGCALL_STATE_ANSWERED);
			ICALL_CALL_CB(egcall->icall, answerh,
				egcall->icall.arg);
		}
		break;

	case EGCALL_STATE_TERMINATING:
	case EGCALL_STATE_NONE:
		break;
	}
 out:
	if (err)
		destroy_ecall(egcall, ecall);
}
		
static void recv_leave(struct egcall* egcall,
			const char *userid_sender,
			const char *clientid_sender,
			const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct ecall *ecall;
	const char *rcid;

	info("egcall(%p): recv_leave u: %s c: %s r: %s\n", egcall,
	     anon_id(userid_anon, userid_sender), 
	     anon_client(clientid_anon, clientid_sender),
	     msg->resp ? "yes" : "no");

	roster_remove(egcall, userid_sender, clientid_sender);

	ecall = ecall_find_userid(&egcall->ecalll, userid_sender);
	if (ecall) {
		rcid = ecall_get_peer_clientid(ecall);
		if (strcmp(rcid, clientid_sender) == 0) {
			ecall_end(ecall);
			ecall_remove(ecall);
		}
	}

	if (egcall->state == EGCALL_STATE_INCOMING
	    && dict_count(egcall->roster) == 0) {
		info("egcall(%p): recv_leave no users in roster in %s\n",
		     egcall, egcall_state_name(egcall->state));
		set_state(egcall, EGCALL_STATE_IDLE);
		ICALL_CALL_CB(egcall->icall, closeh,
			egcall->is_call_answered ? 0 : ECANCELED,
			NULL, &egcall->icall,
			msg ? msg->time : ECONN_MESSAGE_TIME_UNKNOWN,
			userid_sender, clientid_sender, egcall->icall.arg);
	}	
}
		
static void recv_check(struct egcall* egcall,
			const char *userid_sender,
			const char *clientid_sender,
			const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	info("egcall(%p): recv_check u: %s c: %s r: %s\n", egcall,
	     anon_id(userid_anon, userid_sender), 
	     anon_client(clientid_anon, clientid_sender), msg->resp ? "yes" : "no");

	roster_add(egcall, userid_sender, clientid_sender);

	egcall->is_call_answered = true;

	if (egcall->state == EGCALL_STATE_IDLE) {
		set_state(egcall, EGCALL_STATE_INCOMING);
		ICALL_CALL_CB(egcall->icall, starth,
			&egcall->icall, msg->time, userid_sender,
			false, false, egcall->icall.arg);
	}
	else if (egcall->state == EGCALL_STATE_ACTIVE) {
		egcall_start_active_roster_timer(egcall);
	}
	else {
		tmr_start(&egcall->roster_timer, EGCALL_PASSIVE_ROSTER_TIMEOUT, 
			egcall_passive_roster_timeout, egcall);
	}
}

static void recv_reject(struct egcall* egcall,
			const char *userid_sender,
			const char *clientid_sender,
			const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	info("egcall(%p): recv_reject u: %s c: %s r: %s\n", egcall,
	     anon_id(userid_anon, userid_sender), 
	     anon_client(clientid_anon, clientid_sender), msg->resp ? "yes" : "no");

	/* Handle REJECT from ourselves but remain in INCOMING */
	if (0 == str_casecmp(egcall->userid_self, userid_sender) && 
		egcall->state == EGCALL_STATE_INCOMING) {

		ICALL_CALL_CB(egcall->icall, leaveh,
			EGCALL_REASON_REJECTED, ECONN_MESSAGE_TIME_UNKNOWN, egcall->icall.arg);
	}
}

int egcall_msg_recv(struct icall *icall,
		     uint32_t curr_time,
		     uint32_t msg_time,
		     const char *userid_sender,
		     const char *clientid_sender,
		     struct econn_message *msg)
{
	struct egcall *egcall = (struct egcall*)icall;
	struct ecall *ecall = NULL;
	char userid_anon[ANON_ID_LEN];

	if (!egcall || !msg)
		return EINVAL;

	// assert(ECONN_MAGIC == conn->magic);

	switch (msg->msg_type) {

	case ECONN_GROUP_START:
		recv_start(egcall, userid_sender, clientid_sender, msg);
		return 0;

	case ECONN_GROUP_LEAVE:
		recv_leave(egcall, userid_sender, clientid_sender, msg);
		return 0;

	case ECONN_GROUP_CHECK:
		recv_check(egcall, userid_sender, clientid_sender, msg);
		return 0;

	case ECONN_REJECT:
		recv_reject(egcall, userid_sender, clientid_sender, msg);
		return 0;

	default:
		if (!str_isset(msg->dest_userid) || !str_isset(msg->dest_clientid)) {
			info("egcall(%p): ignoring message as user & dest arent set\n", egcall);
			return EINVAL;
		}

		if (strcaseeq(msg->dest_userid, egcall->userid_self) &&
			strcaseeq(msg->dest_clientid, egcall->clientid_self)) {

			if (msg->msg_type == ECONN_GROUP_SETUP &&
				strcaseeq(userid_sender, egcall->userid_self)) {

				info("egcall(%p): ignore message %s from same user (%s)\n",
				     egcall, econn_msg_name(msg->msg_type),
				     anon_id(userid_anon, userid_sender));
				return 0;
			}

			if (msg->msg_type == ECONN_GROUP_SETUP)
				msg->msg_type = ECONN_SETUP;

			ecall = ecall_find_userid(&egcall->ecalll,
						  userid_sender);
			if (!ecall && msg->msg_type == ECONN_SETUP
			    && econn_message_isrequest(msg)
			    && (egcall->state == EGCALL_STATE_OUTGOING ||
				egcall->state == EGCALL_STATE_ANSWERED ||
				egcall->state == EGCALL_STATE_ACTIVE)) {

				add_ecall(&ecall, egcall, userid_sender,
					  clientid_sender);
				roster_add(egcall, userid_sender,
					   clientid_sender);

				if (egcall->state == EGCALL_STATE_OUTGOING) {
					set_state(egcall, EGCALL_STATE_ANSWERED);
				}
			}
			
			if (ecall) {
				ecall_msg_recv(ecall, curr_time, msg_time,
					       userid_sender,
					       clientid_sender,
					       msg);
			}
		}
		else {
			info("egcall(%p): ignoring message as user & dest dont match\n", egcall);
		}

		return 0;
	}
}


int egcall_media_start(struct icall *icall)
{
	struct egcall *egcall = (struct egcall*)icall;
	struct le *le;
	int err = 0;

	LIST_FOREACH(&egcall->ecalll, le) {
		struct ecall *ecall = le->data;
		
		err = ecall_media_start(ecall);
		if (!err)
			set_media_started(egcall, ecall, true);
	}

	return err;
}

void egcall_media_stop(struct icall *icall)
{
	struct egcall *egcall = (struct egcall*)icall;
	struct le *le;
	
	LIST_FOREACH(&egcall->ecalll, le) {
		struct ecall *ecall = le->data;

		ecall_media_stop(ecall);
	}
}


static bool roster_debug_handler(char *key, void *val, void *arg)
{
	struct re_printf *pf = arg;
	struct roster_item *ri = val;
	char userid_anon[ANON_ID_LEN];

	re_hprintf(pf, "\t\t%s\n", anon_id(userid_anon, ri->userid));

	return false;
}


int egcall_debug(struct re_printf *pf, const struct icall *arg)
{
	struct egcall *egcall = (struct egcall*)arg;
	struct le *le;
	int err = 0;

	if (!egcall)
		return EINVAL;

	err |= re_hprintf(pf, "\tGROUP CALL: in state: %s with "
			  "%d participants\n",
			  egcall_state_name(egcall->state),
			  list_count(&egcall->ecalll));

	/* Roster info */
	err |= re_hprintf(pf, "\t\tRoster: %d members\n",
			  dict_count(egcall->roster));
	dict_apply(egcall->roster, roster_debug_handler, pf);

	/* ecall info */
	LIST_FOREACH(&egcall->ecalll, le) {
		struct ecall *ecall = le->data;

		err |= re_hprintf(pf, "%H\n", ecall_debug, ecall);
		err |= re_hprintf(pf, "------------\n");
	}

	return err;
}


static bool roster_members_handler(char *key, void *val, void *arg)
{
	struct wcall_members *mm = arg;
	struct roster_item *ri = val;
	struct wcall_member *memb = &(mm->membv[mm->membc]);

	str_dup(&memb->userid, ri->userid);
	memb->audio_estab = (int)ri->audio_estab;
	memb->video_recv = ri->video_recv;

	(mm->membc)++;

	return false;
}


static void members_destructor(void *arg)
{
	struct wcall_members *mm = arg;
	size_t i;

	for (i = 0; i < mm->membc; ++i) {
		mem_deref(mm->membv[i].userid);
	}

	mem_deref(mm->membv);
}

	
int egcall_get_members(struct icall *icall, struct wcall_members **mmp)
{
	struct egcall *egcall = (struct egcall*)icall;
	struct wcall_members *mm;
	size_t n = 0;
	int err = 0;

	if (!egcall)
		return EINVAL;

	mm = mem_zalloc(sizeof(*mm), members_destructor);
	if (!mm) {
		err = ENOMEM;
		goto out;
	}

	n = dict_count(egcall->roster);
	if (n == 0) {
		mm->membc = 0;
		mm->membv = NULL;
	}
	else {
		mm->membv = mem_zalloc(sizeof(*(mm->membv)) * n, NULL);
		if (!mm->membv) {
			err = ENOMEM;
			goto out;
		}
		dict_apply(egcall->roster, roster_members_handler, mm);
	}


 out:
	if (err)
		mem_deref(mm);
	else {
		if (mmp)
			*mmp = mm;
	}

	return err;
}


int egcall_set_video_send_state(struct icall *icall, enum icall_vstate state)
{
	struct egcall *egcall = (struct egcall*)icall;
	int err = 0;

	uint32_t ecount = 0;
	struct le *le;

	if (!egcall)
		return EINVAL;

	ecount = list_count(&egcall->ecalll);
	info("egcall: set_video_send_state %s ecalls=%u\n",
	     icall_vstate_name(state), ecount);

	egcall->video.send_state = state;

	LIST_FOREACH(&egcall->ecalll, le) {
		struct ecall *ecall = le->data;

		ecall_set_video_send_state(ecall, state);
	}

	return err;
}

int egcall_set_quality_interval(struct icall *icall, uint64_t interval)
{
	struct egcall *egcall = (struct egcall*)icall;
	int err = 0;

	uint32_t ecount = 0;
	struct le *le;

	if (!egcall)
		return EINVAL;

	ecount = list_count(&egcall->ecalll);
	info("egcall: set_quality_interval %ull ecalls=%u\n",
	     interval, ecount);

	egcall->quality.interval = interval;

	LIST_FOREACH(&egcall->ecalll, le) {
		struct ecall *ecall = le->data;

		ecall_set_quality_interval(ecall, interval);
	}

	return err;
}

