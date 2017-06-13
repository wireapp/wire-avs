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

#include "wcall.h"


static struct {
	struct mediamgr *mm;
	char *userid;
	char *clientid;
	struct ecall_conf conf;
	struct lock *lock;
	struct msystem *msys;

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
	wcall_state_change_h *stateh;
	wcall_video_state_change_h *vstateh;
	wcall_audio_cbr_enabled_h *acbrh;
	struct {
		wcall_group_changed_h *chgh;
		void *arg;
	} group;
	void *arg;
} calling = {
	.mm = NULL,
	.clientid = NULL,
	.conf = {
		.econf = {
			.timeout_setup = 30000,
			.timeout_term  =  5000,
		},
	},
	.lock = NULL,
	.ecalls = LIST_INIT,
	.wcalls = LIST_INIT,
	.ctxl = LIST_INIT,

	.readyh = NULL,
	.sendh = NULL,
	.incomingh = NULL,
	.missedh = NULL,
	.estabh = NULL,
	.closeh = NULL,
	.vstateh = NULL,
	.arg = NULL	 
};


struct wcall {
	char *convid;
	char *userid;
	char *clientid;

	struct ecall *ecall;

	struct {
		bool video_call;
		bool send_active;
		int recv_state;
	} video;

	struct {
		bool recv_cbr_state;
	} audio;
	struct {
		struct egcall *call;
	} group;
    
	int state; /* wcall state */

	struct le le;
};


struct wcall_ctx {
	struct wcall *wcall;
	void *context;

	struct le le; /* member of ctxl */
};


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


static void set_state(struct wcall *wcall, int st)
{
	bool trigger = wcall->state != st;
	
	info("wcall(%p): set_state: %s->%s\n",
	     wcall, wcall_state_name(wcall->state), wcall_state_name(st));
	
	wcall->state = st;

	if (trigger && calling.stateh) {
		calling.stateh(wcall->convid, wcall->state, calling.arg);
	}
}


static void call_config_handler(struct call_config *cfg)
{
	if (cfg == NULL)
		return;

	debug("wcall: call_config: %d ice servers\n", cfg->iceserverc);
	
	if (calling.readyh) {
		int ver = WCALL_VERSION_3;

		calling.readyh(ver, calling.arg);
	}
		
}

static void *cfg_wait_thread(void *arg)
{
	struct call_config *cfg = msystem_get_call_config(calling.msys);

	(void)arg;

	while (cfg == NULL && calling.thread_run) {
		debug("wcall: no cfg, waiting...\n");
		sys_msleep(100);	
		cfg = msystem_get_call_config(calling.msys);
	}

	call_config_handler(cfg);

	return NULL;
}

static int async_cfg_wait(void)
{
	int err;

	info("wcall: creating async config polling thread\n");
	
	calling.thread_run = true;
	err = pthread_create(&calling.tid, NULL, cfg_wait_thread, NULL);
	if (err) {
		calling.thread_run = false;
		return err;
	}

	return err;
}


struct wcall *wcall_lookup(const char *convid)
{
	struct wcall *wcall;
	bool found = false;
	struct le *le;

	if (!convid)
		return NULL;
	
	lock_write_get(calling.lock);
	for (le = calling.wcalls.head;
	     le != NULL && !found;
	     le = le->next) { 
		wcall = le->data;

		if (streq(convid, wcall->convid)) {
			found = true;
		}
	}
	lock_rel(calling.lock);
	
	return found ? wcall : NULL;
}

static void ecall_propsync_handler(void *arg);

static void egcall_start_handler(uint32_t msg_time,
				 const char *userid_sender,
				 bool should_ring,
				 void *arg)
{
	struct wcall *wcall = arg;

	set_state(wcall, WCALL_STATE_INCOMING);

	info("wcall(%p): call incomingh(%p) group:yes video:no ring:%s\n",
		wcall, calling.incomingh, should_ring ? "yes" : "no");

	if (calling.incomingh) {
		uint64_t now = tmr_jiffies();
		calling.incomingh(wcall->convid, msg_time, userid_sender,
				  0, should_ring ? 1 : 0, calling.arg);
		
		info("wcall(%p): calling.incomingh took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}
}
				 

static void ecall_conn_handler(uint32_t msg_time,
			       const char *userid_sender,
			       bool video_call,
			       void *arg)
{
	struct wcall *wcall = arg;

	// TODO: lookup userid

	wcall->video.video_call = video_call && ecall_has_video(wcall->ecall);

	info("wcall(%p): call incomingh(%p) group:no video:%s ring:yes\n",
		 wcall, calling.incomingh, video_call ? "yes" : "no");

	set_state(wcall, WCALL_STATE_INCOMING);

	if (calling.incomingh) {
		uint64_t now = tmr_jiffies();
		calling.incomingh(wcall->convid, msg_time, wcall->userid,
				  video_call ? 1 : 0, 1, calling.arg);
		
		info("wcall(%p): calling.incomingh took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}

	ecall_propsync_handler(arg);
}


static void ecall_answer_handler(void *arg)
{
	struct wcall *wcall = arg;

	info("wcall(%p): answer_handler on convid=%s\n", wcall, wcall->convid);
	if (calling.answerh)
		calling.answerh(wcall->convid, calling.arg);

	set_state(wcall, WCALL_STATE_ANSWERED);
	ecall_propsync_handler(arg);
}

static void ecall_media_estab_handler(struct ecall *ecall, bool update,
				      void *arg)
{
	struct wcall *wcall = arg;
	enum mediamgr_state mmst;

	(void)ecall;
	
	info("wcall(%p): media established(video=%d): "
	     "convid=%s userid=%s update=%d\n",
	     wcall, wcall->video.video_call,
	     wcall->convid, wcall->userid, update);
	
	if (wcall->video.video_call)
		mmst = MEDIAMGR_STATE_VIDEOCALL_ESTABLISHED;
	else
		mmst = MEDIAMGR_STATE_CALL_ESTABLISHED;
    
	set_state(wcall, WCALL_STATE_MEDIA_ESTAB);
    
	if (calling.mm) {
		mediamgr_set_call_state(calling.mm, mmst);
	}
}

static void ecall_audio_estab_handler(struct ecall *ecall, bool update,
				      void *arg)
{
	struct wcall *wcall = arg;

	(void)ecall;
	
	info("wcall(%p): audio established(video=%d): convid=%s userid=%s\n",
	     wcall, wcall->video.video_call, wcall->convid, wcall->userid);
    
	// TODO: check this, it should not be necessary
	voe_stop_silencing();
    
	info("wcall(%p): call estabh(%p) \n", wcall, calling.estabh);
	if (!update && calling.estabh) {
		uint64_t now = tmr_jiffies();
		calling.estabh(wcall->convid, wcall->userid, calling.arg);
		info("wcall(%p): calling.estabh took %ld ms \n",
		     wcall, tmr_jiffies() - now);
	}
}

static void ecall_datachan_estab_handler(void *arg, bool update)
{
	struct wcall *wcall = arg;

	(void)wcall;

	info("wcall(%p): data channel established for conversation %s "
	     "update=%d\n",
	     wcall, wcall->convid, update);
}


static void ecall_propsync_handler(void *arg)
{
	struct wcall *wcall = arg;
	int state = WCALL_VIDEO_RECEIVE_STOPPED;
	bool vstate_changed = false;
	const char *vr;

	if (!wcall) {
		warning("wcall(%p): propsyc_handler wcall is NULL, "
			"ignoring props\n", wcall);
		return;
	}

	if (!wcall->ecall) {
		warning("wcall: propsyc_handler ecall is NULL, "
			"ignoring props\n");
		return;
	}

	info("wcall(%p): propsync_handler, current recv_state %s \n",
	     wcall, wcall->video.recv_state ? "true" : "false");

	vr = ecall_props_get_remote(wcall->ecall, "videosend");
	if (vr) {
		vstate_changed = true;
		if (strcmp(vr, "true") == 0) {
			state = WCALL_VIDEO_RECEIVE_STARTED;
		}
	}

	vr = ecall_props_get_remote(wcall->ecall, "screensend");
	if (vr) {
		vstate_changed = true;
		if (strcmp(vr, "true") == 0) {
			state = WCALL_VIDEO_RECEIVE_STARTED;
		}
	}
    
	if (vstate_changed && state != wcall->video.recv_state) {
		info("wcall(%p): propsync_handler updating recv_state "
		     "%s -> %s\n",
		     wcall,
		     wcall->video.recv_state ? "true" : "false",
		     state ? "true" : "false");
		wcall->video.recv_state = state;
		info("wcall(%p): propsync_handler call vstateh(%p) \n",
		     wcall, calling.vstateh);
		if (calling.vstateh) {
			uint64_t now = tmr_jiffies();
			calling.vstateh(state, calling.arg);
			info("wcall(%p): calling.vstateh took %ld ms\n",
			     wcall, tmr_jiffies() - now);
		}
	}
    
	vr = ecall_props_get_remote(wcall->ecall, "audiocbr");
	if (vr) {
		info("wcall(%p): propsync_handler audiocbr = %s \n", wcall, vr);
		if (strcmp(vr, "true") == 0 && !wcall->audio.recv_cbr_state) {
			if (calling.acbrh) {
				uint64_t now = tmr_jiffies();
				calling.acbrh(calling.arg);
				info("wcall(%p): acbrh took %ld ms\n",
				     wcall, tmr_jiffies() - now);
			}
			wcall->audio.recv_cbr_state = true;
		}
	}
}


static int err2reason(int err)
{
	switch (err) {

	case 0:
		return WCALL_REASON_NORMAL;

	case ETIMEDOUT:
		return WCALL_REASON_TIMEOUT;

	case ECONNRESET:
		return WCALL_REASON_LOST_MEDIA;

	case ECANCELED:
		return WCALL_REASON_CANCELED;

	case EALREADY:
		return WCALL_REASON_ANSWERED_ELSEWHERE;

	case EIO:
		return WCALL_REASON_IO_ERROR;
            
	default:
		return WCALL_REASON_ERROR;
	}
}


static void ecall_close_handler(int err, const char *metrics_json, struct ecall *ecall,
	uint32_t msg_time, void *arg)
{
	struct wcall *wcall = arg;
	const char *userid;
	int reason;
	
	(void)ecall;
	reason = err2reason(err);

	info("wcall(%p): closeh(%p) called state=%s\n",
	     wcall, calling.closeh, wcall_state_name(wcall->state));

	/* If the call was already rejected, we don't want to
	 * do anything here
	 */
	if (wcall->state == WCALL_STATE_NONE)
		goto out;

	if (wcall->state == WCALL_STATE_TERM_LOCAL)
		userid = calling.userid;
	else {
		wcall->state = WCALL_STATE_TERM_REMOTE;
		userid = wcall->userid ? wcall->userid : calling.userid;
	}

	set_state(wcall, WCALL_STATE_NONE);
	if (calling.closeh) {
		calling.closeh(reason, wcall->convid, msg_time, userid, calling.arg);
	}
	if (calling.metricsh && metrics_json) {
		calling.metricsh(wcall->convid, metrics_json, calling.arg);
	}
out:
	mem_deref(wcall);
}


static void egcall_leave_handler(int reason, uint32_t msg_time, void *arg)
{
	struct wcall *wcall = arg;

	info("wcall(%p): left group call in conv: %s reason: %d\n", wcall, wcall->convid,
		reason);
	set_state(wcall, WCALL_STATE_INCOMING);
	if (calling.closeh) {
		int wreason = WCALL_REASON_NORMAL;

		switch (reason) {
		case EGCALL_REASON_STILL_ONGOING:
			wreason = WCALL_REASON_STILL_ONGOING;
			break;
		case EGCALL_REASON_ANSWERED_ELSEWHERE:
			wreason = WCALL_REASON_ANSWERED_ELSEWHERE;
			break;
		default:
			wreason = WCALL_REASON_NORMAL;
			break;
		}
		calling.closeh(wreason, wcall->convid, msg_time, calling.userid, calling.arg);
	}
}


static void egcall_group_changed_handler(void *arg)
{
	struct wcall *wcall = arg;

	(void)wcall;
	
	if (calling.group.chgh)
		calling.group.chgh(wcall->convid, calling.group.arg);
}

static void egcall_metrics_handler(const char *metrics_json, void *arg)
{
	struct wcall *wcall = arg;
	if (calling.metricsh && metrics_json) {
		calling.metricsh(wcall->convid, metrics_json, calling.arg);
	}
}

static void egcall_close_handler(int err, uint32_t msg_time, void *arg)
{
	ecall_close_handler(err, NULL, NULL, msg_time, arg);
}

static void ctx_destructor(void *arg)
{
	struct wcall_ctx *ctx = arg;

	lock_write_get(calling.lock);
	list_unlink(&ctx->le);
	lock_rel(calling.lock);
}

static int ctx_alloc(struct wcall_ctx **ctxp, void *context)
{
	struct wcall_ctx *ctx;

	if (!ctxp)
		return EINVAL;
	
	ctx = mem_zalloc(sizeof(*ctx), ctx_destructor);
	if (!ctx)
		return ENOMEM;

	ctx->context = context;
	lock_write_get(calling.lock);
	list_append(&calling.ctxl, &ctx->le, ctx);
	lock_rel(calling.lock);

	*ctxp = ctx;
	
	return 0;
}

static int ecall_send_handler(const char *userid,
			      struct econn_message *msg, void *arg)
{	
	struct wcall *wcall = arg;
	struct wcall_ctx *ctx;
	void *context = wcall;
	char *str = NULL;
	int err = 0;

	if (calling.sendh == NULL)
		return ENOSYS;

	err = ctx_alloc(&ctx, context);
	if (err)
		return err;

	err = econn_message_encode(&str, msg);
	if (err)
		return err;
	
	info("wcall(%p): ecall_send_handler: msg=%H ctx=%p\n",
	     wcall, econn_message_brief, msg, ctx);

	err = calling.sendh(ctx, wcall->convid, userid, calling.clientid,
			    (uint8_t *)str, strlen(str), calling.arg);
	mem_deref(str);

	return err;
}


static void destructor(void *arg)
{
	struct wcall *wcall = arg;
	bool has_calls;

	info("wcall(%p): dtor -- started\n", wcall);
	
	lock_write_get(calling.lock);
	list_unlink(&wcall->le);
	has_calls = calling.wcalls.head != NULL;
	lock_rel(calling.lock);

	if (!has_calls) {
		if (calling.mm) {
			mediamgr_set_call_state(calling.mm,
						MEDIAMGR_STATE_NORMAL);
		}
	}

	mem_deref(wcall->ecall);	
	mem_deref(wcall->group.call);
	mem_deref(wcall->userid);
	mem_deref(wcall->convid);

	info("wcall(%p): dtor -- done\n", wcall);
}


int wcall_add(struct wcall **wcallp,
	      const char *convid,
	      bool group)
{
	struct wcall *wcall;
	struct msystem_turn_server *turnv = NULL;
	size_t turnc = 0;
	size_t i;
	int err;

	if (!wcallp || !convid)
		return EINVAL;

	wcall = wcall_lookup(convid);
	if (wcall) {
		warning("wcall: call_add: already have wcall=%p "
			"for convid=%s\n", wcall, convid);

		return EALREADY;
	}

	wcall = mem_zalloc(sizeof(*wcall), destructor);
	if (!wcall)
		return EINVAL;

	info("wcall(%p): added for convid=%s\n", wcall, convid);
	str_dup(&wcall->convid, convid);

	lock_write_get(calling.lock);

	turnc = msystem_get_turn_servers(&turnv, calling.msys);
	if (turnc == 0) {
		err = ENOSYS;
		goto out;
	}

	if (group) {
		err = egcall_alloc(&wcall->group.call,
				   &calling.conf,
				   convid,
				   calling.userid,
				   calling.clientid,
				   ecall_send_handler,
				   egcall_start_handler,
				   ecall_media_estab_handler,
				   ecall_audio_estab_handler,
				   egcall_group_changed_handler,
				   egcall_leave_handler,
				   egcall_close_handler,
				   egcall_metrics_handler,
				   wcall);
		if (err) {
			warning("wcall(%p): add: could not alloc egcall: %m\n",
				wcall, err);
			goto out;
		}
	}
	else {
		err = ecall_alloc(&wcall->ecall, &calling.ecalls,
				  &calling.conf, flowmgr_msystem(),
				  convid,
				  calling.userid,
				  calling.clientid,
				  ecall_conn_handler,
				  ecall_answer_handler,
				  ecall_media_estab_handler,
				  ecall_audio_estab_handler,
				  ecall_datachan_estab_handler,
				  ecall_propsync_handler,
				  ecall_close_handler,
				  ecall_send_handler,
				  wcall);
		if (err) {
			warning("wcall: call_add: ecall_alloc "
				"failed: %m\n", err);
			goto out;
		}
	}
	for (i = 0; i < turnc; ++i) {
		struct msystem_turn_server *turn = &turnv[i];

		if (group) {
			err = egcall_set_turnserver(wcall->group.call,
						    &turn->srv,
						    turn->user,
						    turn->pass);
			if (err)
				goto out;
		}
		else {
			info("wcall: setting ecall turn: %J %s:%s\n",
			     &turn->srv, turn->user, turn->pass);
			err = ecall_set_turnserver(wcall->ecall,
						   &turn->srv,
						   turn->user,
						   turn->pass);
			if (err)
				goto out;
		}
	}

	wcall->video.recv_state = WCALL_VIDEO_RECEIVE_STOPPED;
	wcall->audio.recv_cbr_state = false;

	list_append(&calling.wcalls, &wcall->le, wcall);
	
 out:
	lock_rel(calling.lock);

	if (err)
		mem_deref(wcall);
	else
		*wcallp = wcall;

	return err;
}


static void mm_mcat_changed(enum mediamgr_state state, void *arg)
{
	(void)arg;
	
	wcall_mcat_changed(state);
}


void wcall_i_mcat_changed(enum mediamgr_state state)
{
	struct le *le;
	
	info("wcall: mcat changed to: %d\n", state);

	lock_write_get(calling.lock);
	LIST_FOREACH(&calling.wcalls, le) {
		struct wcall *wcall = le->data;
	
		switch(state) {
		case MEDIAMGR_STATE_INCALL:
		case MEDIAMGR_STATE_INVIDEOCALL:
		case MEDIAMGR_STATE_RESUME:
			if (wcall->group.call) {
				egcall_media_start(wcall->group.call);
			}
			else {
				ecall_media_start(wcall->ecall);
			}
			break;
		
		case MEDIAMGR_STATE_HOLD:
			if (wcall->group.call) {
				egcall_media_stop(wcall->group.call);
			}
			else {
				ecall_media_stop(wcall->ecall);
			}
			break;

		case MEDIAMGR_STATE_NORMAL:
		default:
			//ecall_media_stop(wcall->ecall);
			break;
		}
	}
	lock_rel(calling.lock);	
}

AVS_EXPORT
int wcall_init(const char *userid,
	       const char *clientid,
	       wcall_ready_h *readyh,
	       wcall_send_h *sendh,
	       wcall_incoming_h *incomingh,
	       wcall_missed_h *missedh,
	       wcall_answered_h *answerh,
	       wcall_estab_h *estabh,
	       wcall_close_h *closeh,
	       wcall_metrics_h *metricsh,
	       void *arg)
{
	struct call_config *call_config;
	int err;

	if (!str_isset(userid) || !str_isset(clientid))
		return EINVAL;

	debug("wcall: init\n");
	
	memset(&calling, 0, sizeof(calling));

	err = mediamgr_alloc(&calling.mm, mm_mcat_changed, NULL);
	if (err) {
		warning("wcall: init: cannot allocate mediamgr\n");
		goto out;
	}
	
	err = str_dup(&calling.userid, userid);
	if (err)
		goto out;
	
	err = str_dup(&calling.clientid, clientid);
	if (err)
		goto out;

	calling.readyh = readyh;
	calling.sendh = sendh;
	calling.incomingh = incomingh;
	calling.missedh = missedh;
	calling.answerh = answerh;
	calling.estabh = estabh;
	calling.closeh = closeh;
	calling.metricsh = metricsh;
	calling.arg = arg;

	calling.conf.econf.timeout_setup = 30000;
	calling.conf.econf.timeout_term  =  5000;
	calling.conf.trace = 0;
	
	err = lock_alloc(&calling.lock);
	if (err)
		goto out;

	err = msystem_get(&calling.msys, "voe", TLS_KEYTYPE_EC, NULL);
	if (err) {
		warning("wcall: cannot init msystem: %m\n", err);
		goto out;
	}

#if 0
	// XXX: Enable Privacy mode for now
	msystem_enable_privacy(calling.msys, true);
#endif

	err = msystem_enable_datachannel(calling.msys, true);
	if (err) {
		warning("wcall: init: failed to enable datachannel (%m)\n",
			err);
		goto out;
	}

	err = wcall_marshal_init();
	if (err)
		goto out;

	call_config = msystem_get_call_config(calling.msys);
	info("wcall: call_config=%p\n", call_config);
	if (!call_config)
		err = async_cfg_wait();
	else
		call_config_handler(call_config);
out:
	if (err)
		wcall_close();

	return err;
}


AVS_EXPORT
void wcall_close(void)
{
	debug("wcall: close\n");

	if (calling.thread_run) {
		calling.thread_run = false;

		debug("wcall: joining thread..\n");

		pthread_join(calling.tid, NULL);
		calling.tid = 0;
	}

	list_flush(&calling.wcalls);	

	lock_write_get(calling.lock);
	
	list_flush(&calling.ecalls);
	list_flush(&calling.ctxl);

	calling.userid = mem_deref(calling.userid);
	calling.clientid = mem_deref(calling.clientid);
	calling.mm = mem_deref(calling.mm);
	calling.msys = mem_deref(calling.msys);

	calling.readyh = NULL;
	calling.sendh = NULL;
	calling.incomingh = NULL;
	calling.estabh = NULL;
	calling.closeh = NULL;
	calling.vstateh = NULL;
	calling.acbrh = NULL;
	calling.arg = NULL;

	wcall_marshal_close();
	
	lock_rel(calling.lock);
	calling.lock = mem_deref(calling.lock);

	debug("wcall: closed.\n");
}


AVS_EXPORT
int wcall_i_start(struct wcall *wcall, int is_video_call, int group)
{
	int err = 0;

	info("wcall(%p): start: convid=%s video=%s\n", wcall, wcall->convid,
		(is_video_call != 0) ? "yes" : "no");

	wcall->video.video_call = (is_video_call != 0);
	if (wcall && wcall->ecall && wcall->state != WCALL_STATE_NONE) {
		info("wcall(%p): start: found call in state '%s'\n",
		     wcall, econn_state_name(ecall_state(wcall->ecall)));
		if (ecall_state(wcall->ecall) == ECONN_PENDING_INCOMING) {
			err = ecall_set_video_send_active(wcall->ecall,
							  (bool)is_video_call);
			if (err)
				goto out;

			err = ecall_answer(wcall->ecall);
			if (!err)
				set_state(wcall, WCALL_STATE_ANSWERED);
				
			goto out;
		}
		else {
			err = EALREADY;
			goto out;
		}
	}
	else {
		set_state(wcall, WCALL_STATE_OUTGOING);

		if (group) {
			err = egcall_start(wcall->group.call);
		}
		else {
			ecall_set_video_send_active(wcall->ecall,
						    (bool)is_video_call);
			err = ecall_start(wcall->ecall);
		}
		if (err)
			goto out;
	}


    
	mediamgr_set_call_state(calling.mm, MEDIAMGR_STATE_SETUP_AUDIO_PERMISSIONS);
    
 out:
	return err;
}


int wcall_i_answer(struct wcall *wcall, int group)
{
	int err = 0;

	if (!wcall)
		return EINVAL;

	if (group) {
		if (!wcall->group.call) {
			warning("wcall(%p): no group call\n", wcall);
			return ENOTSUP;
		}
	}
	else {
		if (!wcall->ecall) {
			warning("wcall(%p): no call\n", wcall);
			return ENOTSUP;
		}
	}

	set_state(wcall, WCALL_STATE_ANSWERED);

	if (group) {
		err = egcall_answer(wcall->group.call);
	}
	else {
		err = ecall_answer(wcall->ecall);
	}

	mediamgr_set_call_state(calling.mm, MEDIAMGR_STATE_SETUP_AUDIO_PERMISSIONS);
    
	return err;
}


void wcall_i_resp(int status, const char *reason, void *arg)
{
	struct wcall_ctx *ctx = arg;
	struct wcall *wcall = ctx ? ctx->context : NULL;
	struct le *le;

	info("wcall(%p): resp: status=%d reason=[%s] ctx=%p\n",
	     wcall, status, reason, ctx);
	
	lock_write_get(calling.lock);
	LIST_FOREACH(&calling.ctxl, le) {
		struct wcall_ctx *at = le->data;

		if (at == ctx) {
			goto out;
		}
	}

	warning("wcall(%p): resp: ctx:%p not found\n", wcall, ctx);
	ctx = NULL;

 out:
	lock_rel(calling.lock);
	mem_deref(ctx);
}


void wcall_i_recv_msg(struct econn_message *msg,
		      uint32_t curr_time,
		      uint32_t msg_time,
		      const char *convid,
		      const char *userid,
		      const char *clientid)
{
	struct wcall *wcall;
	bool handled;
	int err = 0;

	wcall = wcall_lookup(convid);
	
	info("wcall(%p): recv_msg: convid=%s userid=%s clientid=%s "
	     "msg=%H age=%u seconds\n",
	     wcall, convid, userid, clientid, econn_message_brief, msg,
	     msg->age);

	if (econn_is_creator(calling.userid, userid, msg) &&
	    (msg->age * 1000) > calling.conf.econf.timeout_setup) {
		bool is_video = false;

		if (msg->u.setup.props) {
			const char *vr;

			vr = econn_props_get(msg->u.setup.props, "videosend");
			is_video = vr ? streq(vr, "true") : false;

			if (calling.missedh) {
				uint64_t now = tmr_jiffies();
				calling.missedh(convid, msg_time,
						userid, is_video ? 1 : 0,
						calling.arg);

				info("wcall(%p): calling.missedh (%s) "
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
			err = wcall_add(&wcall, convid, true);
		}
		else if (msg->msg_type == ECONN_GROUP_CHECK
		    && !econn_message_isrequest(msg)) {
			err = wcall_add(&wcall, convid, true);
		}
		else if (econn_is_creator(calling.userid, userid, msg)) {
			err = wcall_add(&wcall, convid, false);

			if (err) {
				warning("wcall: recv_msg: could not add call: "
					"%m\n", err);
				goto out;
			}
		}
		else {
			err = EPROTO;
			goto out;
		}
		if (err) {
			warning("wcall: recv_msg: could not add call: "
				"%m\n", err);
			goto out;
		}
	}

	if (!wcall->userid)
		str_dup(&wcall->userid, userid);
	

	handled = egcall_msg_recv(wcall->group.call,
				  curr_time, msg_time,
				  userid, clientid, msg);
	if (handled) {
		err = 0;
	}
	else {
		ecall_msg_recv(wcall->ecall,
			       curr_time,
			       msg_time,
			       userid,
			       clientid,
			       msg);
	}

 out:
	return;
}


static void wcall_end_internal(struct wcall *wcall, int group)
{
	info("wcall(%p): end\n", wcall);
	if (!wcall)
		return;

	if (group) {
		if (!wcall->group.call) {
			warning("wcall(%p): no group-call\n", wcall);
			return;
		}

		egcall_end(wcall->group.call);
	}
	else {
		if (!wcall->ecall) {
			warning("wcall(%p): no call\n", wcall);
			return;
		}
		if (wcall->state != WCALL_STATE_TERM_REMOTE)
			set_state(wcall, WCALL_STATE_TERM_LOCAL);

		ecall_end(wcall->ecall);
	}
	if (calling.mm) {
		mediamgr_set_call_state(calling.mm, MEDIAMGR_STATE_NORMAL);
	}
}


int wcall_i_reject(struct wcall *wcall, int group)
{
	if (!wcall)
		return EINVAL;
	
	info("wcall(%p): reject: convid=%s group=%d\n", wcall, wcall->convid, group);

	/* XXX Do nothing for now?
	 * Later, send REJECT message to all of your own devices
	 */

	if (calling.closeh) {
		calling.closeh(WCALL_REASON_STILL_ONGOING, wcall->convid,
			ECONN_MESSAGE_TIME_UNKNOWN, calling.userid, calling.arg);
	}

	return 0;
}


void wcall_i_end(struct wcall *wcall, int group)
{
	if (!wcall)
		return;
	
	info("wcall(%p): end: convid=%s\n", wcall, wcall->convid);
	if (wcall)
		wcall_end_internal(wcall, group);
}


AVS_EXPORT
void wcall_set_video_state_handler(wcall_video_state_change_h *vstateh)
{
	calling.vstateh = vstateh;
}

#if 0
static bool call_restart_handler(struct le *le, void *arg)
{
	struct wcall *wcall = list_ledata(le);
	
	(void)arg;

	if (!wcall)
		return false;

	if (wcall->ecall) {
		info("wcall(%p): restarting call: %p in conv: %s\n",
		     wcall, wcall->ecall, wcall->convid);
		marshal_ecall_restart(calling.ecall_marshal, wcall->ecall);
	}

	
	return false;
}
#endif

void wcall_i_network_changed(void)
{
#if 0 /* XXX Disabled for release-3.3 */	
	struct sa laddr;
	char ifname[64] = "";

	sa_init(&laddr, AF_INET);

	(void)net_rt_default_get(AF_INET, ifname, sizeof(ifname));
	(void)net_default_source_addr_get(AF_INET, &laddr);

	info("wcall: network_changed: %s|%j\n", ifname, &laddr);

	/* Go through all the calls, and restart flows on them */
	lock_write_get(calling.lock);
	list_apply(&calling.wcalls, true, call_restart_handler, NULL);
	lock_rel(calling.lock);
#endif
}

AVS_EXPORT
void wcall_set_audio_cbr_enabled_handler(wcall_audio_cbr_enabled_h *acbrh)
{
	calling.acbrh = acbrh;
}

AVS_EXPORT
void wcall_set_state_handler(wcall_state_change_h *stateh)
{
	calling.stateh = stateh;
}

void wcall_i_set_video_send_active(struct wcall *wcall, bool active)
{
	if (!wcall)
		return;

	info("wcall(%p): set_video_send_active: convid=%s active=%d\n",
	     wcall, wcall->convid, active);

	ecall_set_video_send_active(wcall->ecall, active);	
}


AVS_EXPORT
int wcall_is_video_call(const char *convid)
{
	struct wcall *wcall;

	wcall = wcall_lookup(convid);
	if (wcall) {
		info("wcall(%p): is_video_call: convid=%s is_video=%s\n", wcall, convid,
			wcall->video.video_call ? "yes" : "no");	
		return wcall->video.video_call;
	}

	info("wcall(%p): is_video_call: convid=%s is_video=no\n", wcall, convid);	
	return 0;
}


AVS_EXPORT
int wcall_debug(struct re_printf *pf, void *ignored)
{
	struct msystem_turn_server *turnv = NULL;
	size_t turnc = 0;
	struct le *le;	
	int err = 0;

	(void)ignored;

	turnc = msystem_get_turn_servers(&turnv, calling.msys);	
	err = re_hprintf(pf, "turnc=%zu\n", turnc);

	err = re_hprintf(pf, "# calls=%d\n", list_count(&calling.wcalls));
	LIST_FOREACH(&calling.wcalls, le) {
		struct wcall *wcall = le->data;

		err |= re_hprintf(pf, "WCALL %p in state: %s\n", wcall,
			wcall_state_name(wcall->state));
		err |= re_hprintf(pf, "convid: %s\n", wcall->convid);
		if (wcall->group.call) {
			err |= re_hprintf(pf, "\t%H\n", egcall_debug,
					  wcall->group.call);
		}
		else if (wcall->ecall) {
			err |= re_hprintf(pf, "\t%H\n", ecall_debug,
					  wcall->ecall);
		}
	}
	
	return err;
}


AVS_EXPORT
void wcall_set_trace(int trace)
{
	calling.conf.trace = trace;
}


AVS_EXPORT
int wcall_get_state(const char *convid)
{
	struct wcall *wcall;
    
	wcall = wcall_lookup(convid);

	return wcall ? wcall->state : WCALL_STATE_UNKNOWN;
}


AVS_EXPORT
struct ecall *wcall_ecall(const char *convid)
{
	struct wcall *wcall;
    
	wcall = wcall_lookup(convid);
    
	return wcall ? wcall->ecall : NULL;
}


AVS_EXPORT
void wcall_propsync_request(const char *convid)
{
	int err;

	err = ecall_propsync_request(wcall_ecall(convid));
	if (err) {
		warning("ecall_propsync_request failed\n");
	}
}


AVS_EXPORT
void wcall_iterate_state(wcall_state_change_h *stateh, void *arg)	
{
	struct wcall *wcall;
	struct le *le;
	
	lock_write_get(calling.lock);
	LIST_FOREACH(&calling.wcalls, le) {
		wcall = le->data;

		if (wcall->state != WCALL_STATE_NONE)
			stateh(wcall->convid, wcall->state, arg);
	}
	lock_rel(calling.lock);
}

AVS_EXPORT
void wcall_enable_audio_cbr(int enabled)
{
	msystem_enable_cbr(calling.msys, enabled);
}


AVS_EXPORT
void wcall_set_group_changed_handler(wcall_group_changed_h *chgh,
				     void *arg)
{
	calling.group.chgh = chgh;
	calling.group.arg = arg;
}


AVS_EXPORT
struct wcall_members *wcall_get_members(const char *convid)
{
	struct wcall_members *members;
	struct wcall *wcall;

	wcall = wcall_lookup(convid);

	if (!wcall || !wcall->group.call)
		return NULL;
	else {
		int err;
		
		err =  egcall_get_members(&members, wcall->group.call);

		return err ? NULL : members;
	}
}


AVS_EXPORT
void wcall_free_members(struct wcall_members *members)
{
	mem_deref(members);
}
