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

#include <pthread.h>

#include <re/re.h>
#include <avs.h>

#include <avs_wcall.h>
#include <avs_vie.h>

#include "wcall.h"

#ifdef __APPLE__
#       include <TargetConditionals.h>
#endif


#define AUDIO_CBR_STATE_UNSET (-1)

#define APITAG "WAPI "


static struct {
	bool initialized;
	struct list instances;
	struct list logl;
	struct lock *lock;	
} calling = {
	.initialized = false,
	.instances = LIST_INIT,
	.logl = LIST_INIT,
	.lock = NULL,
};


struct log_entry {
	struct log logger;

	wcall_log_h *logh;
	void *arg;

	struct le le;
};


struct calling_instance {
	struct wcall_marshal *marshal;
	struct mediamgr *mm;
	char *userid;
	char *clientid;
	struct ecall_conf conf;
	struct call_config *call_config;
	struct lock *lock;
	struct msystem *msys;
	struct config *cfg;

	struct list ecalls;
	struct list wcalls;
	struct list ctxl;

	pthread_t tid;
	bool thread_run;

	wcall_ready_h *readyh;
	wcall_send_h *sendh;
	wcall_incoming_h *incomingh;
	wcall_missed_h *missedh;
	wcall_answered_h *answerh;
	wcall_estab_h *estabh;
	wcall_close_h *closeh;
	wcall_metrics_h *metricsh;
	wcall_config_req_h *cfg_reqh;
	wcall_state_change_h *stateh;
	wcall_video_state_change_h *vstateh;
	wcall_audio_cbr_change_h *acbrh;
	wcall_media_stopped_h *mstoph;
	wcall_data_chan_estab_h *dcestabh;
	wcall_shutdown_h *shuth;
	void *shuth_arg;
	struct {
		wcall_group_changed_h *chgh;
		void *arg;
	} group;
	
	void *arg;

	struct tmr tmr_roam;

	struct le le;

	struct netprobe *netprobe;
	wcall_netprobe_h *netprobeh;
	void *netprobeh_arg;

	struct {
		wcall_network_quality_h *netqh;
		int interval;
		void *arg;
	} quality;
};

struct wcall {
	struct calling_instance *inst;
	char *convid;

	struct icall *icall;

	struct {
		bool video_call;
		int  send_state;
		int  recv_state;
	} video;

	struct {
		int cbr_state;
	} audio;

	int state; /* wcall state */
	bool disable_audio;
	
	struct le le;
};


struct wcall_ctx {
	struct calling_instance *inst;
	struct wcall *wcall;
	void *context;

	struct le le; /* member of ctxl */
};


static bool wcall_has_calls(struct calling_instance *inst);


static bool instance_valid(struct calling_instance *inst)
{
	bool found = false;
	struct le *le;

	lock_write_get(calling.lock);
	for (le = calling.instances.head; le && !found; le = le->next)
		found = le->data == inst;
	lock_rel(calling.lock);

	return found;
}


static bool wcall_valid(const struct wcall *wcall)
{
	struct calling_instance *inst;
	bool found = false;
	struct le *le;
	
	if (!wcall)
		return false;

	inst = wcall->inst;

	if (!instance_valid(inst))
		return false;
	
	lock_write_get(inst->lock);
	for (le = inst->wcalls.head; le && !found; le = le->next)
		found = wcall == le->data;
	lock_rel(inst->lock);

	return found;
}


const char *wcall_state_name(int st)
{	
	switch(st) {
	case WCALL_STATE_NONE:
		return "none";
		
	case WCALL_STATE_OUTGOING:
		return "outgoing";
		
	case WCALL_STATE_INCOMING:
		return "incoming";
		
	case WCALL_STATE_ANSWERED:
		return "answered";

	case WCALL_STATE_MEDIA_ESTAB:
		return "media-established";
		
	case WCALL_STATE_TERM_LOCAL:
		return "locally terminated";

	case WCALL_STATE_TERM_REMOTE:
		return "remotely terminated";
		
	case WCALL_STATE_UNKNOWN:
		return "unknown";

	default:
		return "?";
	}
}

static const char *wcall_call_type_name(int type)
{	
	switch(type) {
	case WCALL_CALL_TYPE_NORMAL:
		return "normal";
		
	case WCALL_CALL_TYPE_VIDEO:
		return "video";
		
	case WCALL_CALL_TYPE_FORCED_AUDIO:
		return "forced-audio";

	default:
		return "?";
	}
}

static const char *wcall_conv_type_name(int type)
{	
	switch(type) {
	case WCALL_CONV_TYPE_ONEONONE:
		return "oneonone";
		
	case WCALL_CONV_TYPE_GROUP:
		return "group";
		
	case WCALL_CONV_TYPE_CONFERENCE:
		return "conference";

	default:
		return "?";
	}
}

static const char *wcall_vstate_name(int vstate)
{
	switch (vstate) {

	case WCALL_VIDEO_STATE_STOPPED:     return "STOPPED";
	case WCALL_VIDEO_STATE_STARTED:     return "STARTED";
	case WCALL_VIDEO_STATE_BAD_CONN:    return "BADCONN";
	case WCALL_VIDEO_STATE_PAUSED:      return "PAUSED";
	default: return "???";
	}
}

static void set_state(struct wcall *wcall, int st)
{
	bool trigger = wcall->state != st;
	struct calling_instance *inst = wcall->inst;
	
	info("wcall(%p): set_state: %s->%s\n",
	     wcall, wcall_state_name(wcall->state), wcall_state_name(st));
	
	wcall->state = st;

	if (trigger && inst && inst->stateh) {
		inst->stateh(wcall->convid, wcall->state, inst->arg);
	}
}


static void *cfg_wait_thread(void *arg)
{
	struct calling_instance *inst = arg;

	(void)arg;

	debug("wcall(%p): starting config wait\n", inst);	
	while (inst->call_config == NULL && inst->thread_run) {
		sys_msleep(500);
	}
	if (inst->call_config)
		debug("wcall(%p): config ready!\n", inst);

	inst->thread_run = false;

	return NULL;
}


static int async_cfg_wait(struct calling_instance *inst)
{
	int err;

	info("wcall: creating async config polling thread\n");
	
	inst->thread_run = true;
	err = pthread_create(&inst->tid, NULL, cfg_wait_thread, inst);
	if (err) {
		inst->thread_run = false;
		return err;
	}

	return err;
}


struct wcall *wcall_lookup(void *id, const char *convid)
{
	struct calling_instance *inst = id;
	struct wcall *wcall;
	bool found = false;
	struct le *le;

	if (!id || !convid)
		return NULL;
	
	lock_write_get(inst->lock);
	for (le = inst->wcalls.head;
	     le != NULL && !found;
	     le = le->next) { 
		wcall = le->data;

		if (streq(convid, wcall->convid)) {
			found = true;
		}
	}
	lock_rel(inst->lock);
	
	return found ? wcall : NULL;
}


void wcall_i_invoke_incoming_handler(const char *convid,
				     uint32_t msg_time,
				     const char *userid,
				     int video_call,
				     int should_ring,
				     void *arg)
{
	struct calling_instance *inst = arg;
	uint64_t now = tmr_jiffies();

	struct wcall *wcall = wcall_lookup(inst, convid);

	info(APITAG "wcall(%p): wcall=%p calling incomingh: %p\n",
	     inst, wcall, inst->incomingh);

	if (!wcall) {
		warning("wcall(%p): invoke_incoming_handler: wcall=NULL, ignoring\n", inst);
		return;
	}

	if (wcall->state != WCALL_STATE_INCOMING) {
		warning("wcall(%p): invoke_incoming_handler: wcall=%p "
			"wrong state: %s\n",
			inst, wcall, wcall_state_name(wcall->state));
		return;
	}

	if (inst->incomingh) {
		inst->incomingh(convid, msg_time, userid,
				video_call, should_ring, inst->arg);
	}

	info(APITAG "wcall(%p): inst->incomingh took %ld ms \n",
	     inst, tmr_jiffies() - now);
}



static void icall_vstate_handler(struct icall *icall, const char *userid, const char *clientid,
	enum icall_vstate state, void *arg);
static void icall_audiocbr_handler(struct icall *icall, const char *userid,
	const char *clientid, int enabled, void *arg); 

static void icall_start_handler(struct icall *icall,
				uint32_t msg_time,
				const char *userid_sender,
				bool video,
				bool should_ring,
				void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): egcall_start_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	set_state(wcall, WCALL_STATE_INCOMING);

	info(APITAG "wcall(%p): incomingh(%p) video:%s ring:%s\n",
	     wcall, inst->incomingh,
	     video ? "yes" : "no",
	     should_ring ? "yes" : "no");

	wcall->video.video_call = video;

	if (inst->mm) {
		enum mediamgr_state state;

		state = video ?	MEDIAMGR_STATE_INCOMING_VIDEO_CALL
			      : MEDIAMGR_STATE_INCOMING_AUDIO_CALL;

		mediamgr_set_call_state(inst->mm, state);
	}
	
	if (inst->incomingh) {
		if (inst->mm) {
			mediamgr_invoke_incomingh(inst->mm,
						  wcall_invoke_incoming_handler,
						  wcall->convid, msg_time,
						  userid_sender,
						  video ? 1 : 0,
						  should_ring ? 1 : 0,
						  inst);
		}
		else {
			wcall_i_invoke_incoming_handler(wcall->convid, msg_time,
						userid_sender,
						video ? 1 : 0,
						should_ring ? 1 : 0, inst);
		}
	}
}


static void icall_answer_handler(void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char convid_anon[ANON_ID_LEN];

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): ecall_answer_handler: invalid wcall "
                        "inst=%p\n", wcall, wcall->inst);
		return;
	}

	info(APITAG "wcall(%p): answerh(%p) convid=%s\n", wcall, inst->answerh,
	     anon_id(convid_anon, wcall->convid));
	if (inst->answerh) {
		uint64_t now = tmr_jiffies();
		inst->answerh(wcall->convid, inst->arg);

		info(APITAG "wcall(%p): answerh took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}
	set_state(wcall, WCALL_STATE_ANSWERED);
}


static void icall_media_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update, void *arg)
{
	int err;
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char peer_userid_anon[ANON_ID_LEN];
	char convid_anon[ANON_ID_LEN];

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): ecall_media_estab_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	info("wcall(%p): media established(video=%d): "
	     "convid=%s peer_userid=%s update=%d\n",
	     wcall, wcall->video.video_call,
	     anon_id(convid_anon, wcall->convid),
	     anon_id(peer_userid_anon, userid), update);
	
	set_state(wcall, WCALL_STATE_MEDIA_ESTAB);
    
	if (inst->mm) {
		enum mediamgr_state state;

		state = wcall->video.video_call ? MEDIAMGR_STATE_INVIDEOCALL
			                        : MEDIAMGR_STATE_INCALL;
		mediamgr_set_call_state(inst->mm, state);
	}
	else {
		err = ICALL_CALLE(icall, media_start);
		if (err) {
			warning("wcall(%p): icall_media_start failed (%m)\n",
				wcall, err);
		}
	}
}

static void icall_media_stopped_handler(struct icall *icall, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	(void)icall;
	
	if (!wcall_valid(wcall)) {
		warning("wcall(%p): ecall_media_stopped_handler: "
			"invalid wcall inst=%p\n", wcall, inst);
		return;
	}

	if (wcall->state != WCALL_STATE_TERM_LOCAL) {
		info("wcall(%p): ecall_media_stopped_handler: "
		     " ignoring media stopped in state %s\n",
		     wcall, wcall_state_name(wcall->state));
		return;
	}

	info(APITAG "wcall(%p): mstoph(%p)\n", wcall, inst->mstoph);

	if (inst->mstoph) {
		uint64_t now = tmr_jiffies();
		inst->mstoph(wcall->convid, inst->arg);
		info(APITAG "wcall(%p): mstoph took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}
}


static void icall_audio_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char peer_userid_anon[ANON_ID_LEN];
	char convid_anon[ANON_ID_LEN];

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): ecall_audio_estab_handler: "
			"invalid wcall inst=%p\n", wcall, inst);
		return;
	}

	info("wcall(%p): audio established(video=%d): "
	     "convid=%s peer_userid=%s inst=%p mm=%p\n",
	     wcall, wcall->video.video_call,
	     anon_id(convid_anon, wcall->convid),
	     anon_id(peer_userid_anon, userid), inst, inst->mm);

	// TODO: check this, it should not be necessary
	msystem_stop_silencing();
	
	info(APITAG "wcall(%p): estabh(%p) peer_userid=%s\n",
	     wcall, inst->estabh, anon_id(peer_userid_anon, userid));

	if (!update && inst->estabh) {
		uint64_t now = tmr_jiffies();
		inst->estabh(wcall->convid, userid, inst->arg);

		info(APITAG "wcall(%p): estabh took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}
}


static void icall_datachan_estab_handler(struct icall *icall, const char *userid,
	const char *clientid,bool update, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char peer_userid_anon[ANON_ID_LEN];
	char convid_anon[ANON_ID_LEN];

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): ecall_dce_estab_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	inst = wcall->inst;
	
	info("wcall(%p): data channel established for conversation %s "
	     "update=%d\n",
	     wcall, anon_id(convid_anon, wcall->convid), update);

	info(APITAG "wcall(%p): dcestabh(%p) "
	     "conv=%s peer_userid=%s update=%d\n",
	     wcall, inst ? inst->dcestabh : NULL,
	     anon_id(convid_anon, wcall->convid),
	     anon_id(peer_userid_anon, userid),
	     update);

	if (inst && inst->dcestabh) {
		uint64_t now = tmr_jiffies();
		wcall->inst->dcestabh(wcall->convid,
				      userid,
				      inst->arg);

		info(APITAG "wcall(%p): dcestabh took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}
}


static void icall_vstate_handler(struct icall *icall, const char *userid,
	const char *clientid, enum icall_vstate state, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char userid_anon[ANON_ID_LEN];

	(void)clientid;

	if (!wcall) {
		warning("wcall(%p): vstateh wcall is NULL, "
			"ignoring props\n", wcall);
		return;
	}
	
	if (!wcall_valid(wcall)) {
		warning("wcall(%p): icall_vstate_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	if (!icall) {
		warning("wcall(%p): vstateh icall is NULL, "
			"ignoring props\n", wcall);
		return;
	}

	int wstate = WCALL_VIDEO_STATE_STOPPED;
	switch (state) {
	case ICALL_VIDEO_STATE_STARTED:
		wstate = WCALL_VIDEO_STATE_STARTED;
		break;
	case ICALL_VIDEO_STATE_SCREENSHARE:
		wstate = WCALL_VIDEO_STATE_SCREENSHARE;
		break;
	case ICALL_VIDEO_STATE_PAUSED:
		wstate = WCALL_VIDEO_STATE_PAUSED;
		break;
	case ICALL_VIDEO_STATE_BAD_CONN:
		wstate = WCALL_VIDEO_STATE_BAD_CONN;
		break;
	case ICALL_VIDEO_STATE_STOPPED:
	default:
		wstate = WCALL_VIDEO_STATE_STOPPED;
		break;
	}
	info(APITAG "wcall(%p): vstateh(%p) icall=%p user=%s state=%d\n",
	     wcall, inst->vstateh, icall, anon_id(userid_anon, userid), wstate);

	if (inst->vstateh) {
		uint64_t now = tmr_jiffies();
		inst->vstateh(userid, wstate, inst->arg);
		info(APITAG "wcall(%p): vstateh took %ld ms\n",
		     wcall, tmr_jiffies() - now);
	}
}


static void icall_audiocbr_handler(struct icall *icall, const char *userid, 
	const char *clientid, int enabled, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char userid_anon[ANON_ID_LEN];

	(void)clientid;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): icall_audiocbr_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	inst = wcall->inst;
	
	if (!wcall) {
		warning("wcall(%p): acbrh wcall is NULL, "
			"ignoring props\n", wcall);
		return;
	}

	if (!icall) {
		warning("wcall(%p): acbrh icall is NULL, "
			"ignoring props\n", wcall);
		return;
	}

	info(APITAG "wcall(%p): acbrh(%p) usr=%s cbr=%d\n", wcall, inst->acbrh,
	     anon_id(userid_anon, userid), enabled);
	if (inst->acbrh) {
		uint64_t now = tmr_jiffies();
		inst->acbrh(userid, enabled, inst->arg);
		info(APITAG "wcall(%p): acbrh took %ld ms\n",
		     wcall, tmr_jiffies() - now);
	}
}


static int err2reason(int err)
{
	switch (err) {

	case 0:
		return WCALL_REASON_NORMAL;

	case ETIMEDOUT:
		return WCALL_REASON_TIMEOUT;

	case ETIMEDOUT_ECONN:
		return WCALL_REASON_TIMEOUT_ECONN;

	case ECONNRESET:
		return WCALL_REASON_LOST_MEDIA;

	case ECANCELED:
		return WCALL_REASON_CANCELED;

	case EALREADY:
		return WCALL_REASON_ANSWERED_ELSEWHERE;

	case EIO:
		return WCALL_REASON_IO_ERROR;
            
	case EDATACHANNEL:
		return WCALL_REASON_DATACHANNEL;

	case EREMOTE:
		return WCALL_REASON_REJECTED;

	default:
		/* TODO: please convert new errors */
		warning("wcall: default reason (%d) (%m)\n", err, err);
		return WCALL_REASON_ERROR;
	}
}


static void icall_close_handler(int err, const char *metrics_json,
				struct icall *icall, uint32_t msg_time,
				const char *userid, const char *clientid,
				void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	int reason;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): icall_close_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}
	
	reason = err2reason(err);

	info(APITAG "wcall(%p): closeh(%p) group=no state=%s reason=%s\n",
	     wcall, inst->closeh, wcall_state_name(wcall->state),
	     wcall_reason_name(reason));

	
	/* If the call was already rejected, we don't want to
	 * do anything here
	 */
	if (wcall->state == WCALL_STATE_NONE)
		goto out;

	if (wcall->state != WCALL_STATE_TERM_LOCAL) {
		set_state(wcall, WCALL_STATE_TERM_REMOTE);
	}

	if (!userid) {
		userid = inst->userid;
	}
	set_state(wcall, WCALL_STATE_NONE);
	if (inst->closeh) {
		uint64_t now = tmr_jiffies();
		inst->closeh(reason, wcall->convid,
			     msg_time, userid, inst->arg);
		info(APITAG "wcall(%p): closeh took %ld ms\n",
		     wcall, tmr_jiffies() - now);
	}

	info(APITAG "wcall(%p): metricsh(%p) json=%p\n", wcall, inst->metricsh, metrics_json);

	if (inst->metricsh && metrics_json) {
		uint64_t now = tmr_jiffies();
		inst->metricsh(wcall->convid, metrics_json, inst->arg);
		info(APITAG "wcall(%p): metricsh took %ld ms\n",
		     wcall, tmr_jiffies() - now);
	}
out:
	mem_deref(wcall);
}


static void egcall_leave_handler(int reason, uint32_t msg_time, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): egcall_leave_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	info(APITAG "wcall(%p): closeh(%p) group=yes state=%s reason=%s\n",
	     wcall, inst->closeh, wcall_state_name(wcall->state),
	     wcall_reason_name(reason));

	set_state(wcall, WCALL_STATE_INCOMING);
	if (inst->closeh) {
		int wreason = WCALL_REASON_NORMAL;

		switch (reason) {
		case EGCALL_REASON_STILL_ONGOING:
			wreason = WCALL_REASON_STILL_ONGOING;
			break;
		case EGCALL_REASON_ANSWERED_ELSEWHERE:
			wreason = WCALL_REASON_ANSWERED_ELSEWHERE;
			break;
		case EGCALL_REASON_REJECTED:
			wreason = WCALL_REASON_REJECTED;
			break;
		default:
			wreason = WCALL_REASON_NORMAL;
			break;
		}
		uint64_t now = tmr_jiffies();
		inst->closeh(wreason, wcall->convid, msg_time, inst->userid, inst->arg);
		info(APITAG "wcall(%p): closeh took %ld ms\n",
		     wcall, tmr_jiffies() - now);
	}
}


static void egcall_group_changed_handler(void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): egcall_group_changed_handler: "
			"invalid wcall inst=%p\n", wcall, inst);
		return;
	}

	if (inst->group.chgh) {
		uint64_t now = tmr_jiffies();
		info(APITAG "wcall(%p): group_changedh\n", wcall);
		inst->group.chgh(wcall->convid, inst->group.arg);
		info(APITAG "wcall(%p): group_changedh took: %llums\n",
		     wcall, tmr_jiffies() - now);
	}
}

static void egcall_metrics_handler(const char *metrics_json, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): egcall_metrics_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	if (inst->metricsh && metrics_json) {
		inst->metricsh(wcall->convid, metrics_json, inst->arg);
	}
}

static void ctx_destructor(void *arg)
{
	struct wcall_ctx *ctx = arg;
	struct calling_instance *inst = ctx->inst;

	lock_write_get(inst->lock);
	list_unlink(&ctx->le);
	lock_rel(inst->lock);
}

static int ctx_alloc(struct wcall_ctx **ctxp, struct calling_instance *inst, void *context)
{
	struct wcall_ctx *ctx;

	if (!ctxp)
		return EINVAL;
	
	ctx = mem_zalloc(sizeof(*ctx), ctx_destructor);
	if (!ctx)
		return ENOMEM;

	ctx->context = context;
	ctx->inst = inst;
	lock_write_get(inst->lock);
	list_append(&inst->ctxl, &ctx->le, ctx);
	lock_rel(inst->lock);

	*ctxp = ctx;
	
	return 0;
}

static int icall_send_handler(const char *userid,
			      struct econn_message *msg, void *arg)
{	
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	struct wcall_ctx *ctx;
	void *context = wcall;
	char *str = NULL;
	int err = 0;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char dest_userid_anon[ANON_ID_LEN];
	char dest_clientid_anon[ANON_CLIENT_LEN];

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): icall_send_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return ENODEV;
	}
	
	if (inst->sendh == NULL)
		return ENOSYS;
       
	err = ctx_alloc(&ctx, inst, context);
	if (err)
		return err;	

	err = econn_message_encode(&str, msg);
	if (err)
		return err;

	
	info("wcall(%p): c3_message_send: convid=%s from=%s.%s to=%s.%s "
	     "msg=%H ctx=%p\n",
	     wcall, anon_id(convid_anon, wcall->convid),
	     anon_id(userid_anon, userid), anon_client(clientid_anon, inst->clientid),
	     strlen(msg->dest_userid) > 0 ? anon_id(dest_userid_anon, msg->dest_userid) : "ALL",
	     strlen(msg->dest_clientid) > 0 ?
	         anon_client(dest_clientid_anon, msg->dest_clientid) : "ALL",
	     econn_message_brief, msg, ctx);

	char *dest_user = NULL;
	char *dest_client = NULL;    
	if (strlen(msg->dest_userid) > 0) {
		dest_user = msg->dest_userid;
	}

	if (strlen(msg->dest_clientid) > 0) {
		dest_client = msg->dest_clientid;
	}
	err = inst->sendh(ctx, wcall->convid, userid, inst->clientid,
			    dest_user, dest_client, (uint8_t *)str, strlen(str),
			    msg->transient ? 1 : 0, inst->arg);

	info(">>> %s\n", str);
	
	mem_deref(str);

	return err;
}


static void destructor(void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall->inst;
	bool has_calls;
	//size_t i;

	info("wcall(%p): dtor -- started\n", wcall);
	
	lock_write_get(inst->lock);
	list_unlink(&wcall->le);
	has_calls = inst->wcalls.head != NULL;
	lock_rel(inst->lock);

	if (!has_calls) {
		if (inst->mm) {
			mediamgr_set_call_state(inst->mm,
						MEDIAMGR_STATE_NORMAL);
		}
	}

	mem_deref(wcall->icall);
	mem_deref(wcall->convid);

	info("wcall(%p): dtor -- done\n", wcall);
}


static void icall_quality_handler(struct icall *icall,
				  const char *userid,
				  int rtt, int uploss, int downloss,
				  void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	int quality = WCALL_QUALITY_NORMAL;
	uint64_t now;

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): ecall_quality_handler wcall not valid\n",
			wcall);
		return;
	}

	if (!inst->quality.netqh)
		return;

	if (rtt > 800 || uploss > 20 || downloss > 20)
		quality = WCALL_QUALITY_POOR;
	else if (rtt > 400 || uploss > 5 || downloss > 5)
		quality = WCALL_QUALITY_MEDIUM;

	info(APITAG "wcall(%p): calling netqh:%p rtt=%d up=%d dn=%d q=%d\n",
	     wcall, inst->quality.netqh, rtt, uploss, downloss, quality);
	now = tmr_jiffies();
	inst->quality.netqh(wcall->convid,
			    userid,
			    quality,
			    rtt,
			    uploss,
			    downloss,
			    inst->quality.arg);
	info(APITAG "wcall(%p): netqh:%p (quality=%d) took %d ms\n",
	     wcall, inst->quality.netqh, quality, (int)(tmr_jiffies() - now));
}



int wcall_add(void *id,
	      struct wcall **wcallp,
	      const char *convid,
	      int conv_type)
{
	struct calling_instance *inst = id;
	struct wcall *wcall;
	struct zapi_ice_server *turnv = NULL;
	size_t turnc = 0;
	size_t i;
	int err;
	char convid_anon[ANON_ID_LEN];
	bool group = conv_type != WCALL_CONV_TYPE_ONEONONE;

	if (!id || !wcallp || !convid)
		return EINVAL;

	wcall = wcall_lookup(id, convid);
	if (wcall) {
		warning("wcall(%p): call_add: already have wcall "
			"for convid=%s\n", wcall, anon_id(convid_anon, convid));

		return EALREADY;
	}

	wcall = mem_zalloc(sizeof(*wcall), destructor);
	if (!wcall)
		return EINVAL;

	wcall->inst = inst;

	info(APITAG "wcall(%p): added for convid=%s inst=%p\n", wcall,
	     anon_id(convid_anon, convid), inst);
	str_dup(&wcall->convid, convid);

	lock_write_get(inst->lock);

	turnv = config_get_iceservers(inst->cfg, &turnc);
	if (turnc == 0) {
		info("wcall(%p): no turn servers\n", wcall);
	}

	if (group) {
		struct egcall* egcall;
		err = egcall_alloc(&egcall,
				   &inst->conf,
				   convid,
				   inst->userid,
				   inst->clientid);

		if (err) {
			warning("wcall(%p): add: could not alloc egcall: %m\n",
				wcall, err);
			goto out;
		}

		wcall->icall = egcall_get_icall(egcall);
		icall_set_callbacks(wcall->icall,
				    icall_send_handler,
				    icall_start_handler,
				    icall_answer_handler,
				    icall_media_estab_handler,
				    icall_audio_estab_handler,
				    icall_datachan_estab_handler,
				    icall_media_stopped_handler,
				    egcall_group_changed_handler,
				    egcall_leave_handler,
				    icall_close_handler,
				    egcall_metrics_handler,
				    icall_vstate_handler,
				    icall_audiocbr_handler,
				    icall_quality_handler,
				    wcall);
	}
	else {
		struct ecall* ecall;
		err = ecall_alloc(&ecall, &inst->ecalls,
				  ICALL_CONV_TYPE_ONEONONE,
				  &inst->conf, flowmgr_msystem(),
				  convid,
				  inst->userid,
				  inst->clientid);
		if (err) {
			warning("wcall(%p): call_add: ecall_alloc "
				"failed: %m\n", wcall, err);
			goto out;
		}

		wcall->icall = ecall_get_icall(ecall);
		icall_set_callbacks(wcall->icall,
				    icall_send_handler,
				    icall_start_handler, 
				    icall_answer_handler,
				    icall_media_estab_handler,
				    icall_audio_estab_handler,
				    icall_datachan_estab_handler,
				    icall_media_stopped_handler,
				    NULL, // group_changed_handler
				    NULL, // leave_handler
				    icall_close_handler,
				    NULL, // metrics_handler
				    icall_vstate_handler,
				    icall_audiocbr_handler,
				    icall_quality_handler,
				    wcall);
	}

	err = ICALL_CALLE(wcall->icall, set_quality_interval,
		inst->quality.interval);

	for (i = 0; i < turnc; ++i) {
		struct zapi_ice_server *turn = &turnv[i];

		err = ICALL_CALLE(wcall->icall, add_turnserver,
				  turn);
		if (err) {
			warning("wcall(%p): error adding turnserver (%m)\n",
				wcall, err);
		}
	}

	wcall->video.recv_state = WCALL_VIDEO_STATE_STOPPED;
	wcall->audio.cbr_state = AUDIO_CBR_STATE_UNSET;

	list_append(&inst->wcalls, &wcall->le, wcall);

 out:
	lock_rel(inst->lock);

	if (err)
		mem_deref(wcall);
	else
		*wcallp = wcall;

	return err;
}

static void mm_mcat_changed(enum mediamgr_state state, void *arg)
{
	struct calling_instance *inst = arg;

	wcall_mcat_changed(inst, state);
}


void wcall_i_mcat_changed(void *id, enum mediamgr_state state)
{
	struct le *le;
	struct calling_instance *inst = id;
	
	info("wcall: mcat changed to: %d inst=%p\n", state, inst);

	if (!inst) {
		warning("wcall_i mcat_changed: no instance\n");
		return;
	}

	lock_write_get(inst->lock);
	LIST_FOREACH(&inst->wcalls, le) {
		struct wcall *wcall = le->data;
	
		switch(state) {
		case MEDIAMGR_STATE_INCALL:
		case MEDIAMGR_STATE_INVIDEOCALL:
		case MEDIAMGR_STATE_RESUME:
			if (wcall->icall) {
				ICALL_CALL(wcall->icall, media_start);
			}
			break;
		
		case MEDIAMGR_STATE_HOLD:
			if (wcall->icall) {
				ICALL_CALL(wcall->icall, media_stop);
			}
			break;

		case MEDIAMGR_STATE_NORMAL:
		default:
			//if (wcall->icall) {
			//	ICALL_CALL(wcall->icall, media_stop);
			//}
			break;
		}
	}
	lock_rel(inst->lock);	
}

static void mm_audio_route_changed(enum mediamgr_auplay new_route, void *arg)
{
	struct calling_instance *inst = arg;

	wcall_audio_route_changed(inst, new_route);
}

void wcall_i_audio_route_changed(enum mediamgr_auplay new_route)
{
	const char *dev;
	switch (new_route) {

		case MEDIAMGR_AUPLAY_EARPIECE:
			dev = "earpiece";
			break;

		case MEDIAMGR_AUPLAY_SPEAKER:
			dev = "speaker";
			break;

		case MEDIAMGR_AUPLAY_BT:
			dev = "bt";
			break;

		case MEDIAMGR_AUPLAY_LINEOUT:
			dev = "lineout";
			break;

		case MEDIAMGR_AUPLAY_SPDIF:
			dev = "spdif";
			break;

		case MEDIAMGR_AUPLAY_HEADSET:
			dev = "headset";
			break;

		default:
			warning("wcall: Unknown Audio route %d \n", new_route);
			return;
	}
	msystem_set_auplay(dev);
}

AVS_EXPORT
int wcall_init(void)
{
	int err = 0;
	debug("wcall: init: initialized=%d\n\n", calling.initialized);

	if (calling.initialized)
		return EALREADY;

	calling.initialized = true;

	list_init(&calling.logl);
	list_init(&calling.instances);

	err = lock_alloc(&calling.lock);
	if (err)
		warning("wcall_init: could not allocate lock: %m\n", err);

#ifdef ANDROID
	/* DNS is initialized from wrapper */
#else
	dns_init(NULL);
#endif

	return err;
}

AVS_EXPORT
void wcall_close(void)
{
	struct le *le;

	debug("wcall: close: initialized=%d\n", calling.initialized);
	
	if (!calling.initialized)
		return;	

	lock_write_get(calling.lock);
	LIST_FOREACH(&calling.logl, le) {
		struct log_entry *loge = le->data;
		
		log_unregister_handler(&loge->logger);
	}
	list_flush(&calling.logl);
	list_flush(&calling.instances);

	lock_rel(calling.lock);	
	dns_close();

	calling.lock = mem_deref(calling.lock);
	calling.initialized = false;
}


static int config_req_handler(void *arg)
{
	struct calling_instance *inst = arg;
	int err = 0;

	if (!inst)
		return EINVAL;

	if (inst->cfg_reqh) {
		err = inst->cfg_reqh(inst, inst->arg);
	}

	return err;
}


static void config_update_handler(struct call_config *cfg, void *arg)
{
	struct calling_instance *inst = arg;
	bool first = false;	
	
	if (cfg == NULL)
		return;

	if (inst->call_config == NULL)
		first = true;
	inst->call_config = cfg;

	debug("wcall(%p): call_config: %d ice servers\n",
	      inst, cfg->iceserverc);
	
	if (first && inst->readyh) {
		int ver = WCALL_VERSION_3;

		inst->readyh(ver, inst->arg);
	}
}


AVS_EXPORT
void *wcall_create(const char *userid,
		   const char *clientid,
		   wcall_ready_h *readyh,
		   wcall_send_h *sendh,
		   wcall_incoming_h *incomingh,
		   wcall_missed_h *missedh,
		   wcall_answered_h *answerh,
		   wcall_estab_h *estabh,
		   wcall_close_h *closeh,
		   wcall_metrics_h *metricsh,
		   wcall_config_req_h *cfg_reqh,
		   wcall_audio_cbr_change_h *acbrh,
		   wcall_video_state_change_h *vstateh,
		   void *arg)
{
	return wcall_create_ex(userid,
			       clientid,
			       true,
			       readyh,
			       sendh,
			       incomingh,
			       missedh,
			       answerh,
			       estabh,
			       closeh,
			       metricsh,
			       cfg_reqh,
			       acbrh,
			       vstateh,
			       arg);
}

struct inst_dtor_entry {
	struct tmr tmr;
	struct wcall_marshal *marshal;
	void *inst;
	wcall_shutdown_h *shuth;
	void *shuth_arg;
};


static void ide_destructor(void *arg)
{
	struct inst_dtor_entry *ide = arg;

	tmr_cancel(&ide->tmr);
}


static void ide_handler(void *arg)
{
	struct inst_dtor_entry *ide = arg;

	info("wcall: derefing marshal: %p\n", ide->marshal);
	mem_deref(ide->marshal);
	info("wcall: derefing marshal: %p done!\n", ide->marshal);

	if (ide->shuth)
		ide->shuth(NULL, ide->shuth_arg);
	
	mem_deref(ide->inst);
	mem_deref(ide);
}


static void instance_destroy(struct calling_instance *inst)
{
	if (inst->thread_run) {
		inst->thread_run = false;

		debug("wcall: joining thread..\n");

		pthread_join(inst->tid, NULL);
		pthread_detach(inst->tid);
		inst->tid = 0;
	}

	tmr_cancel(&inst->tmr_roam);

	list_flush(&inst->wcalls);	
	list_flush(&inst->ctxl);

	lock_write_get(inst->lock);
	list_unlink(&inst->le);
	list_flush(&inst->ecalls);

	inst->userid = mem_deref(inst->userid);
	inst->clientid = mem_deref(inst->clientid);
	inst->mm = mem_deref(inst->mm);
	inst->msys = mem_deref(inst->msys);
	inst->cfg = mem_deref(inst->cfg);

	inst->readyh = NULL;
	inst->sendh = NULL;
	inst->incomingh = NULL;
	inst->estabh = NULL;
	inst->closeh = NULL;
	inst->vstateh = NULL;
	inst->acbrh = NULL;
	inst->cfg_reqh = NULL;
	inst->arg = NULL;

	lock_rel(inst->lock);

	inst->lock = mem_deref(inst->lock);
	inst->netprobe = mem_deref(inst->netprobe);

	{
		struct inst_dtor_entry *ide;
		ide = mem_zalloc(sizeof(*ide), ide_destructor);
		if (!ide)
			return;
		
		ide->inst = inst;
		ide->marshal = inst->marshal;

		if (!inst->shuth)
			ide_handler(ide);
		else {
			tmr_init(&ide->tmr);
			ide->shuth = inst->shuth;
			ide->shuth_arg = inst->shuth_arg;
			tmr_start(&ide->tmr, 0, ide_handler, ide);
		}
	}
}


static void instance_destructor(void *arg)
{
	struct calling_instance *inst = arg;
	
	info("instance_destructor(%p)\n", inst);
}


AVS_EXPORT
void *wcall_create_ex(const char *userid,
		      const char *clientid,
		      int use_mediamgr,
		      wcall_ready_h *readyh,
		      wcall_send_h *sendh,
		      wcall_incoming_h *incomingh,
		      wcall_missed_h *missedh,
		      wcall_answered_h *answerh,
		      wcall_estab_h *estabh,
		      wcall_close_h *closeh,
		      wcall_metrics_h *metricsh,
		      wcall_config_req_h *cfg_reqh,
		      wcall_audio_cbr_change_h *acbrh,
		      wcall_video_state_change_h *vstateh,
		      void *arg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct calling_instance *inst = NULL;
	int err;

	if (!str_isset(userid) || !str_isset(clientid))
		return NULL;

	info(APITAG "wcall: create userid=%s clientid=%s\n",
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid));

	inst = mem_zalloc(sizeof(*inst), instance_destructor);
	if (inst == NULL) {
		err = ENOMEM;
		goto out;
	}

	err = wcall_marshal_alloc(&inst->marshal);
	if (err) {
		warning("wcall_create: could not allocate marshal\n");
		goto out;
	}

	if (use_mediamgr != 0) {
		err = mediamgr_alloc(&inst->mm, mm_mcat_changed, inst);
		if (err) {
			warning("wcall: init: cannot allocate mediamgr "
				"inst=%p\n",
				inst);
			goto out;
		}
		debug("wcall: mediamgr=%p\n", inst->mm);
		mediamgr_register_route_change_h(inst->mm,
						 mm_audio_route_changed,
						 inst);
	}

	err = str_dup(&inst->userid, userid);
	if (err)
		goto out;
	
	err = str_dup(&inst->clientid, clientid);
	if (err)
		goto out;

	inst->readyh = readyh;
	inst->sendh = sendh;
	inst->incomingh = incomingh;
	inst->missedh = missedh;
	inst->answerh = answerh;
	inst->estabh = estabh;
	inst->closeh = closeh;
	inst->metricsh = metricsh;
	inst->cfg_reqh = cfg_reqh;
	inst->vstateh = vstateh;
	inst->acbrh = acbrh;
	inst->arg = arg;

	inst->conf.econf.timeout_setup = 60000;
	inst->conf.econf.timeout_term  =  5000;
	inst->conf.trace = 0;
	
	err = lock_alloc(&inst->lock);
	if (err)
		goto out;

	err = msystem_get(&inst->msys, "voe", NULL);
	if (err) {
		warning("wcall(%p): create, cannot init msystem: %m\n",
			inst, err);
		goto out;
	}

	/* Always enable Crypto-KASE for now .. */
	msystem_enable_kase(inst->msys, true);

	err = msystem_enable_datachannel(inst->msys, true);
	if (err) {
		warning("wcall(%p): create: enable datachannel failed (%m)\n",
			inst, err);
		goto out;
	}
	
	err = config_alloc(&inst->cfg,
			   config_req_handler,
			   config_update_handler,
			   inst);
	if (err) {
		warning("wcall(%p): create: config_alloc failed (%m)\n",
			inst, err);
		goto out;		
	}

	err = config_start(inst->cfg);
	if (err) {
		warning("wcall: config_start failed (%m)\n", err);
		goto out;
	}

	lock_write_get(calling.lock);
	list_append(&calling.instances, &inst->le, inst);
	lock_rel(calling.lock);
	
	err = async_cfg_wait(inst);

out:
	if (err) {
		wcall_i_destroy(inst);
		inst = NULL;
	}

	info(APITAG "wcall: create return inst=%p\n", inst);
	return inst;

}

AVS_EXPORT
void wcall_set_shutdown_handler(void *wuser,
				wcall_shutdown_h *shuth, void *arg)
{
	struct calling_instance *inst = wuser;

	inst->shuth = shuth;
	inst->shuth_arg = arg;
}

void wcall_i_destroy(void *id)
{
	struct calling_instance *inst = id;

	info(APITAG "wcall: destroy inst=%p\n", inst);
	
	if (!inst) {
		warning("wcall_destroy: no instance\n");
		return;
	}

	if (!instance_valid(inst)) {
		warning("wcall_destroy: not a valid instance: %p\n", inst);
		return;
	}

	instance_destroy(inst);
}


AVS_EXPORT
void wcall_destroy(void *wuser)	
{
	struct calling_instance *inst = wuser;

	if (inst->shuth)
		wcall_marshal_destroy(wuser);
	else
		wcall_i_destroy(wuser);
}

AVS_EXPORT
int wcall_i_start(struct wcall *wcall, int call_type, int conv_type,
		  int audio_cbr, void *extcodec_arg)
{
	int err = 0;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	bool cbr = audio_cbr != 0;
	char convid_anon[ANON_ID_LEN];

	if (!wcall_valid(wcall)) {
		warning("wcall(%p): invalid wcall: inst=%p\n", wcall, inst);
		return EINVAL;
	}
	
	info(APITAG "wcall(%p): start: convid=%s calltype=%s "
	     "convtype=%s audio_cbr=%s\n",
	     wcall, anon_id(convid_anon, wcall->convid),
	     wcall_call_type_name(call_type), wcall_conv_type_name(conv_type),
	     cbr ? "yes" : "no");

	if (wcall->disable_audio)
		wcall->disable_audio = false;

	bool is_video_call = call_type == WCALL_CALL_TYPE_VIDEO;
	
	wcall->video.video_call = is_video_call;
	if (wcall && wcall->icall) {
		if (WCALL_STATE_NONE == wcall->state) {
			set_state(wcall, WCALL_STATE_OUTGOING);
		}
		err = ICALL_CALLE(wcall->icall, start,
			call_type, cbr, NULL);
		if (err)
			goto out;
	}

    
	if (inst->mm) {
		enum mediamgr_state state;

		state = is_video_call ? MEDIAMGR_STATE_OUTGOING_VIDEO_CALL
			              : MEDIAMGR_STATE_OUTGOING_AUDIO_CALL;
		mediamgr_set_call_state(inst->mm, state);
	}
 out:
	return err;
}


int wcall_i_answer(struct wcall *wcall, int call_type, int audio_cbr,
		   void *extcodec_arg)
{
	int err = 0;
	struct calling_instance *inst;
	bool cbr = audio_cbr != 0;

	if (!wcall) {
		warning("wcall; answer: no wcall\n");
		return EINVAL;
	}
	
	info(APITAG "wcall(%p): answer calltype=%s\n",
	     wcall, wcall_call_type_name(call_type));

	if (wcall->disable_audio)
		wcall->disable_audio = false;
	
	inst = wcall->inst;
	if (!wcall->icall) {
		warning("wcall(%p): answer: no call object found\n", wcall);
		return ENOTSUP;
	}
	set_state(wcall, WCALL_STATE_ANSWERED);
	err = ICALL_CALLE(wcall->icall, answer,
		call_type, cbr, NULL);

	return err;
}


void wcall_i_resp(void *id, int status, const char *reason, void *arg)
{
	struct wcall_ctx *ctx = arg;
	struct calling_instance *inst = id;
	struct wcall *wcall = ctx ? ctx->context : NULL;
	struct le *le;

	info("wcall(%p): resp: status=%d reason=[%s] ctx=%p\n",
	     wcall, status, reason, ctx);
	
	lock_write_get(inst->lock);
	LIST_FOREACH(&inst->ctxl, le) {
		struct wcall_ctx *at = le->data;

		if (at == ctx) {
			goto out;
		}
	}

	warning("wcall(%p): resp: ctx:%p not found\n", wcall, ctx);
	ctx = NULL;

 out:
	lock_rel(inst->lock);
	mem_deref(ctx);
}


void wcall_i_config_update(void *id, int err, const char *json_str)
{
	struct calling_instance *inst = id;

	info("wcall(%p): config_update: err=%d json=%zu bytes\n",
	     inst, err, str_len(json_str));
	
	if (!inst)
		return;

	err = config_update(inst->cfg, err, json_str, str_len(json_str));
	if (err)
		warning("wcall(%p): config_update failed: %m\n", inst, err);
}


void wcall_i_recv_msg(void *id,
		      struct econn_message *msg,
		      uint32_t curr_time,
		      uint32_t msg_time,
		      const char *convid,
		      const char *userid,
		      const char *clientid)
{
	struct calling_instance *inst = id;
	struct wcall *wcall;
	int err = 0;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char dest_userid_anon[ANON_ID_LEN];
	char dest_clientid_anon[ANON_CLIENT_LEN];

	if (!inst) {
		warning("wcall_i_recv_msg: no instance\n");
		return;
	}

	wcall = wcall_lookup(id, convid);
	
	info("wcall(%p): c3_message_recv: convid=%s from=%s.%s to=%s.%s "
	     "msg=%H age=%u seconds inst=%p\n",
	     wcall, anon_id(convid_anon, convid),
	     anon_id(userid_anon, userid), anon_client(clientid_anon, clientid),
	     strlen(msg->dest_userid) > 0 ? anon_id(dest_userid_anon, msg->dest_userid) : "ALL",
	     strlen(msg->dest_clientid) > 0 ?
	         anon_client(dest_clientid_anon, msg->dest_clientid) : "ALL",
	     econn_message_brief, msg, msg->age, inst);

	if (econn_is_creator(inst->userid, userid, msg) &&
	    (msg->age * 1000) > inst->conf.econf.timeout_setup) {
		bool is_video = false;

		if (msg->u.setup.props) {
			const char *vr;

			vr = econn_props_get(msg->u.setup.props, "videosend");
			is_video = vr ? streq(vr, "true") : false;

			if (inst->missedh) {
				uint64_t now = tmr_jiffies();
				inst->missedh(convid, msg_time,
						userid, is_video ? 1 : 0,
						inst->arg);

				info("wcall(%p): inst->missedh (%s) "
				     "took %ld ms\n",
				     wcall, is_video ? "video" : "audio",
				     tmr_jiffies() - now);
			}
		}

		return;
	}
	
	if (!wcall) {
		if (msg->msg_type == ECONN_GROUP_START
		    && econn_message_isrequest(msg)) {
			err = wcall_add(id, &wcall, convid, true);
		}
		else if (msg->msg_type == ECONN_GROUP_CHECK
		    && !econn_message_isrequest(msg)) {
			err = wcall_add(id, &wcall, convid, true);
		}
		else if (econn_is_creator(inst->userid, userid, msg)) {
			err = wcall_add(id, &wcall, convid, false);

			if (err) {
				warning("wcall(%p): recv_msg: could not add call: "
					"%m\n", wcall, err);
				goto out;
			}
		}
		else {
			err = EPROTO;
			goto out;
		}
		if (err) {
			warning("wcall(%p): recv_msg: could not add call: "
				"%m\n", wcall, err);
			goto out;
		}
	}


	err = ICALL_CALLE(wcall->icall, msg_recv,
			  curr_time, msg_time, userid, clientid, msg);
	if (err) {
		warning("wcall(%p): recv_msg: recv_msg returned error: "
			"%m\n", wcall, err);
	}

 out:
	return;
}


static bool wcall_has_calls(struct calling_instance *inst)
{
	struct le *le;

	LIST_FOREACH(&inst->wcalls, le) {
		struct wcall *wcall = le->data;

		switch(wcall->state) {
		case WCALL_STATE_NONE:
		case WCALL_STATE_TERM_LOCAL:
		case WCALL_STATE_TERM_REMOTE:
			break;

		default:
			if (wcall->disable_audio)
				break;
			else
				return true;
		}
	}
	return false;
}


static void wcall_end_internal(struct wcall *wcall)
{
	info("wcall(%p): end\n", wcall);
	if (!wcall)
		return;

	if (!wcall->icall) {
		warning("wcall(%p): end: no call object found\n", wcall);
		return;
	}

	if (wcall->icall) {
		if (wcall->state != WCALL_STATE_TERM_REMOTE)
			set_state(wcall, WCALL_STATE_TERM_LOCAL);
		ICALL_CALL(wcall->icall, end);
	}

	wcall->disable_audio = true;
	if (!wcall_has_calls(wcall->inst)) {
		if (wcall->inst->mm) {
			mediamgr_set_call_state(wcall->inst->mm,
						MEDIAMGR_STATE_NORMAL);
		}
	}
}


int wcall_i_reject(struct wcall *wcall)
{
	struct calling_instance *inst;
	char convid_anon[ANON_ID_LEN];

	info(APITAG "wcall(%p): reject convid=%s\n", wcall,
	     wcall ? anon_id(convid_anon, wcall->convid) : "");

	if (!wcall)
		return EINVAL;

	inst = wcall->inst;

	info("wcall(%p): reject: convid=%s\n", wcall, anon_id(convid_anon, wcall->convid));


	wcall->disable_audio = true;
	if (!wcall_has_calls(wcall->inst)) {
		if (wcall->inst->mm) {
			mediamgr_set_call_state(wcall->inst->mm,
						MEDIAMGR_STATE_NORMAL);
		}
	}

	/* XXX Do nothing for now?
	 * Later, send REJECT message to all of your own devices
	 */

	if (inst->closeh) {
		inst->closeh(WCALL_REASON_STILL_ONGOING, wcall->convid,
			ECONN_MESSAGE_TIME_UNKNOWN, inst->userid, inst->arg);
	}

	return 0;
}


void wcall_i_end(struct wcall *wcall)
{
	char convid_anon[ANON_ID_LEN];

	info(APITAG "wcall(%p): end convid=%s\n", wcall,
	     wcall ? anon_id(convid_anon, wcall->convid) : "");

	if (wcall)
		wcall_end_internal(wcall);
}

AVS_EXPORT
void wcall_set_media_stopped_handler(void *wuser,
				     wcall_media_stopped_h *mstoph)
{
	struct calling_instance *inst = wuser;

	if (instance_valid(inst))
		inst->mstoph = mstoph;
	else {
		warning("wcall: set_audio_stopped_handler: "
			"invalid instance(%p)", inst);
	}
}


AVS_EXPORT
void wcall_set_data_chan_estab_handler(void *wuser,
				       wcall_data_chan_estab_h *dcestabh)
{
	struct calling_instance *inst = wuser;

	if (instance_valid(inst))
		inst->dcestabh = dcestabh;
	else {
		warning("wcall: set_data_chan_estab_handler: "
			"invalid instance(%p)", inst);
	}
}


#if 0 /* XXX Disabled for release-3.5 (enable again for proper integration with clients) */
static bool call_restart_handler(struct le *le, void *arg)
{
	struct wcall *wcall = list_ledata(le);
	
	(void)arg;

	if (!wcall)
		return false;

	if (wcall->icall) {
		info("wcall(%p): restarting call: %p in conv: %s\n",
		     wcall, wcall->icall, wcall->convid);

		ecall_restart(wcall->icall);
	}

	
	return false;
}
#endif


#if 0 /* XXX Disabled for release-3.5 (enable again for proper integration with clients) */
static void tmr_roaming_handler(void *arg)
{
	struct sa laddr;
	char ifname[64] = "";
	(void)arg;

	sa_init(&laddr, AF_INET);

	(void)net_rt_default_get(AF_INET, ifname, sizeof(ifname));
	(void)net_default_source_addr_get(AF_INET, &laddr);

	info("wcall: network_changed: %s|%j\n", ifname, &laddr);

	/* Go through all the calls, and restart flows on them */
	lock_write_get(inst->lock);
	list_apply(&inst->wcalls, true, call_restart_handler, NULL);
	lock_rel(inst->lock);
}
#endif


void wcall_i_network_changed(void)
{
	/* Reset the previous timer */
	//tmr_start(&inst->tmr_roam, 500, tmr_roaming_handler, NULL);
	info(APITAG "wcall: network_changed\n");
}


AVS_EXPORT
void wcall_set_state_handler(void *id, wcall_state_change_h *stateh)
{
	struct calling_instance *inst = id;

	info(APITAG "wcall: set_state_handler %p inst=%p\n", stateh, inst);

	if (!inst) {
		warning("wcall_set_state_handler: no instance\n");
		return;
	}

	inst->stateh = stateh;
}

void wcall_i_set_video_send_state(struct wcall *wcall, int state)
{
	char convid_anon[ANON_ID_LEN];
	enum icall_vstate vstate;

	if (!wcall)
		return;

	if (!wcall->inst)
		return;

	info(APITAG "wcall(%p): set_video_send_state convid=%s vstate=%s "
	     "state=%s\n",
	     wcall, anon_id(convid_anon, wcall->convid),
	     wcall_vstate_name(state),
	     wcall_state_name(wcall->state));

	switch (state) {
	case WCALL_VIDEO_STATE_BAD_CONN:
		/* not set from UI, ignore */
		return;
	case WCALL_VIDEO_STATE_STARTED:
		vstate = ICALL_VIDEO_STATE_STARTED;
		break;
	case WCALL_VIDEO_STATE_PAUSED:
		vstate = ICALL_VIDEO_STATE_PAUSED;
		break;
	case WCALL_VIDEO_STATE_STOPPED:
	default:
		vstate = ICALL_VIDEO_STATE_STOPPED;
		break;
	}

	if (wcall->icall) {
		ICALL_CALL(wcall->icall, set_video_send_state,
			vstate);
	}

	if (wcall->state == WCALL_STATE_MEDIA_ESTAB) {
		mediamgr_set_call_state(wcall->inst->mm,
					(state == WCALL_VIDEO_STATE_STARTED)
					        ? MEDIAMGR_STATE_INVIDEOCALL
					        : MEDIAMGR_STATE_INCALL);
	}
}


AVS_EXPORT
int wcall_is_video_call(void *id, const char *convid)
{
	struct wcall *wcall;
	char convid_anon[ANON_ID_LEN];

	wcall = wcall_lookup(id, convid);
	if (wcall) {
		info(APITAG "wcall(%p): is_video_call convid=%s is_video=%s\n",
		     wcall, anon_id(convid_anon, wcall->convid),
		     wcall->video.video_call ? "yes" : "no");	
		return wcall->video.video_call;
	}

	info(APITAG "wcall(%p): is_video_call convid=%s is_video=no\n",
	     wcall, anon_id(convid_anon, wcall->convid));
	return 0;
}


AVS_EXPORT
int wcall_debug(struct re_printf *pf, const void *id)
{
	struct le *le;	
	char convid_anon[ANON_ID_LEN];
	int err = 0;

	const struct calling_instance *inst = id;

	if (!inst) {
		re_hprintf(pf, "\n");
		return 0;
	}

	err = re_hprintf(pf, "# calls=%d\n", list_count(&inst->wcalls));
	LIST_FOREACH(&inst->wcalls, le) {
		struct wcall *wcall = le->data;

		err |= re_hprintf(pf, "WCALL %p in state: %s\n", wcall,
			wcall_state_name(wcall->state));
		err |= re_hprintf(pf, "convid: %s\n", anon_id(convid_anon, wcall->convid));
		if (wcall->icall && wcall->icall->debug) {
			err |= re_hprintf(pf, "\t%H\n", wcall->icall->debug,
					  wcall->icall);
		}
	}
	
	return err;
}


AVS_EXPORT
void wcall_set_trace(void *id, int trace)
{
	struct calling_instance *inst = id;

	if (!inst) {
		warning("wcall_set_trace: no instance\n");
		return;
	}

	inst->conf.trace = trace;
}


AVS_EXPORT
int wcall_get_state(void *id, const char *convid)
{
	struct wcall *wcall;
    
	wcall = wcall_lookup(id, convid);

	return wcall ? wcall->state : WCALL_STATE_UNKNOWN;
}


AVS_EXPORT
void wcall_iterate_state(void *id, wcall_state_change_h *stateh, void *arg)	
{
	struct calling_instance *inst = id;
	struct wcall *wcall;
	struct le *le;

	if (!inst) {
		warning("wcall_iterate_state: no instance\n");
		return;
	}

	lock_write_get(inst->lock);
	LIST_FOREACH(&inst->wcalls, le) {
		wcall = le->data;

		if (wcall->state != WCALL_STATE_NONE)
			stateh(wcall->convid, wcall->state, arg);
	}
	lock_rel(inst->lock);
}


AVS_EXPORT
void wcall_set_group_changed_handler(void *id, wcall_group_changed_h *chgh,
				     void *arg)
{
	struct calling_instance *inst = id;

	info(APITAG "wcall: set_group_changed_handler %p inst=%p\n", chgh, inst);

	if (!inst) {
		warning("wcall_set_group_changed_handler: no instance\n");
		return;
	}

	inst->group.chgh = chgh;
	inst->group.arg = arg;
}


AVS_EXPORT
struct wcall_members *wcall_get_members(void *id, const char *convid)
{
	struct wcall_members *members;
	struct wcall *wcall;

	if (!id)
		return NULL;

	wcall = wcall_lookup(id, convid);

	if (!wcall || !wcall->icall)
		return NULL;
	else {
		int err;
		
		err = ICALL_CALLE(wcall->icall, get_members,
			&members);

		return err ? NULL : members;
	}
}


AVS_EXPORT
void wcall_free_members(struct wcall_members *members)
{
	mem_deref(members);
}


AVS_EXPORT
void wcall_enable_privacy(void *id, int enabled)
{
	struct calling_instance *inst = id;

	info(APITAG "wcall: enable_privacy enabled=%d inst=%p\n", enabled, inst);

	if (!inst) {
		warning("wcall: enable_privacy -- no instance\n");
		return;
	}

	if (!inst->msys) {
		warning("wcall: enable_privacy -- no msystem\n");
		return;
	}

	msystem_enable_privacy(inst->msys, !!enabled);
}


const char *wcall_reason_name(int reason)
{
	switch (reason) {

	case WCALL_REASON_NORMAL:             return "Normal";
	case WCALL_REASON_ERROR:              return "Error";
	case WCALL_REASON_TIMEOUT:            return "Timeout";
	case WCALL_REASON_LOST_MEDIA:         return "LostMedia";
	case WCALL_REASON_CANCELED:           return "Canceled";
	case WCALL_REASON_ANSWERED_ELSEWHERE: return "Elsewhere";
	case WCALL_REASON_IO_ERROR:           return "I/O";
	case WCALL_REASON_STILL_ONGOING:      return "Ongoing";
	case WCALL_REASON_TIMEOUT_ECONN:      return "TimeoutEconn";
	case WCALL_REASON_DATACHANNEL:        return "DataChannel";
	case WCALL_REASON_REJECTED:           return "Rejected";

	default: return "???";
	}
}


/**
 * Return a borrowed reference to the Media-Manager
 */
struct mediamgr *wcall_mediamgr(void *id)
{
	struct calling_instance *inst = id;

	if (!inst) {
		warning("wcall_mediamgr: no instance\n");
		return NULL;
	}

	return inst->mm;
}


void wcall_handle_frame(struct avs_vidframe *frame)
{
	if (frame) {
		vie_capture_router_handle_frame(frame);
	}
}


struct wcall_marshal *wcall_get_marshal(void *wuser)
{
	struct calling_instance *inst = wuser;

	return inst ? inst->marshal : NULL;
}


static void wcall_log_handler(uint32_t level, const char *msg, void *arg)
{
	struct log_entry *loge = arg;
	int wlvl;

	log_mask_ipaddr(msg);

	switch (level) {
	case LOG_LEVEL_DEBUG:
		wlvl = WCALL_LOG_LEVEL_DEBUG;
		break;
		
	case LOG_LEVEL_INFO:
		wlvl = WCALL_LOG_LEVEL_INFO;
		break;
		
	case LOG_LEVEL_WARN:
		wlvl = WCALL_LOG_LEVEL_WARN;		
		break;

	case LOG_LEVEL_ERROR:
		wlvl = WCALL_LOG_LEVEL_ERROR;		
		break;

	default:
		wlvl = WCALL_LOG_LEVEL_ERROR;
		break;
	}
	
	
	if (loge->logh)
		loge->logh(wlvl, msg, loge->arg);
}


AVS_EXPORT
void wcall_set_log_handler(wcall_log_h *logh, void *arg)
{
	struct log_entry *loge;
		
	loge = mem_zalloc(sizeof(*loge), NULL);
	if (!loge)
		return;

	loge->logh = logh;
	loge->arg = arg;

	loge->logger.h = wcall_log_handler;
	loge->logger.arg = loge;

	log_register_handler(&loge->logger);

	lock_write_get(calling.lock);
	list_append(&calling.logl, &loge->le, loge);
	lock_rel(calling.lock);
}


static void netprobe_handler(int err, const struct netprobe_result *result,
			     void *arg)
{
	struct calling_instance *inst = arg;

	inst->netprobe = mem_deref(inst->netprobe);

	inst->netprobeh(err, result->rtt_avg,
			result->n_pkt_sent, result->n_pkt_recv,
			inst->netprobeh_arg);
}


int wcall_netprobe(void *id, size_t pkt_count, uint32_t pkt_interval_ms,
		   wcall_netprobe_h *netprobeh, void *arg)
{
	struct calling_instance *inst = id;
	struct zapi_ice_server *turnv = NULL;
	struct zapi_ice_server *turn;
	struct stun_uri uri;
	bool found = false;
	size_t turnc = 0;
	size_t i;
	int err;

	if (!id)
		return EINVAL;

	if (inst->netprobe)
		return EBUSY;

	inst->netprobeh = netprobeh;
	inst->netprobeh_arg = arg;

	turnv = config_get_iceservers(inst->cfg, &turnc);
	if (turnc == 0) {
		warning("wcall: netprobe: no turn servers\n");
		return ENOENT;
	}

	for (i = 0; i < turnc; ++i) {
		turn = &turnv[i];

		err = stun_uri_decode(&uri, turn->url);
		if (err)
			continue;

		if (STUN_SCHEME_TURN == uri.scheme) {
			found = true;
			break;
		}
	}
	if (!found) {
		warning("wcall: netprobe: no TURN servers found\n");
		return ENOENT;
	}

	info("wcall: running netprobe with TURN %J\n", &uri.addr);

	err = netprobe_alloc(&inst->netprobe, &uri.addr,
			     uri.proto, uri.secure,
			     turn->username, turn->credential,
			     pkt_count, pkt_interval_ms,
			     netprobe_handler, inst);
	if (err)
		return err;

	return 0;
}

AVS_EXPORT
int wcall_set_network_quality_handler(void *wuser,
				      wcall_network_quality_h *netqh,
				      int interval,
				      void *arg)
{
	struct calling_instance *inst = wuser;

	info(APITAG "wcall: set_quality_handler fn=%p int=%d inst=%p\n",
		netqh, interval, inst);

	if (!instance_valid(inst)) {
		warning("wcall_set_network_quality_handler: instance: %p "
			"not valid", inst);
		return EINVAL;
	}

	inst->quality.netqh = netqh;
	inst->quality.interval = (uint64_t)interval * 1000;
	inst->quality.arg = arg;

	return 0;
}

AVS_EXPORT
void wcall_set_video_handlers(wcall_render_frame_h *render_frame_h,
			      wcall_video_size_h *size_h,
			      void *arg)
{
	vie_set_video_handlers(NULL, render_frame_h, size_h, arg);
}

