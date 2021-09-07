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
#include <unistd.h>

#include <re/re.h>
#include <avs.h>

#include <avs_wcall.h>
#include <avs_peerflow.h>
#include <avs_audio_level.h>

#include "wcall.h"

#ifdef __APPLE__
#       include <TargetConditionals.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define AUDIO_CBR_STATE_UNSET (-1)

#define APITAG "WAPI "

#define WCALL_VALID(_wcall) ((_wcall) && (_wcall)->inst && wcall_valid(_wcall))

static struct {
	bool initialized;
	int env;
	struct list instances;
	struct list logl;
	struct lock *lock;	
	int run_init;
	int run_err;
	pthread_t tid;
	uint32_t wuser_index;
	struct {
		wcall_mute_h *h;
		void *arg;
	} mute;
} calling = {
	.initialized = false,
	.instances = LIST_INIT,
	.logl = LIST_INIT,
	.lock = NULL,
	.run_init = 0,
	.run_err = 0,
	.wuser_index = 0,
};


struct log_entry {
	struct log logger;

	wcall_log_h *logh;
	void *arg;

	struct le le;
};

#define WU_MAGIC 0x57550000 /* WU */

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
	wcall_sft_req_h *sfth;
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
	wcall_media_estab_h *mestabh;
	wcall_media_stopped_h *mstoph;
	wcall_data_chan_estab_h *dcestabh;
	wcall_req_clients_h *clients_reqh;
	wcall_active_speaker_h *active_speakerh;
	wcall_shutdown_h *shuth;
	void *shuth_arg;
	struct {
		wcall_group_changed_h *chgh;
		void *arg;
		struct {
			wcall_participant_changed_h *chgh;
			void *arg;
		} json;
	} group;

	struct {
		wcall_mute_h *h;
		void *arg;
	} mute;
	
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

	struct sa *media_laddr;

	uint32_t wuser;
};

struct wcall {
	struct calling_instance *inst;
	char *convid;
	int conv_type;

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

static void wcall_end_internal(struct wcall *wcall);
static bool wcall_has_calls(void);

static void call_group_change_json(struct calling_instance *inst,
				   struct wcall *wcall);


struct calling_instance *wuser2inst(WUSER_HANDLE wuser)
{
	bool found = false;
	struct calling_instance *inst = NULL;
	struct le *le;

	if ((wuser & WU_MAGIC) != WU_MAGIC)
		return NULL;
	
	lock_write_get(calling.lock);
	for (le = calling.instances.head; le && !found; le = le->next) {
		inst = le->data;
		
		found = inst->wuser == wuser;
	}
	lock_rel(calling.lock);

	return found ? inst : NULL;

}

static WUSER_HANDLE inst2wuser(struct calling_instance *inst)
{
	struct calling_instance *ci;
	struct le *le;
	bool found = false;

	lock_write_get(calling.lock);
	for (le = calling.instances.head; le && !found; le = le->next) {
		ci = le->data;
		found = ci == inst;
	}
	lock_rel(calling.lock);

	return found ? ci->wuser : (WUSER_HANDLE)0;
}

static WUSER_HANDLE create_wuser(struct calling_instance *inst)
{
	WUSER_HANDLE wuser = WUSER_INVALID_HANDLE;

	if (inst) {
		wuser = WU_MAGIC + calling.wuser_index;
		calling.wuser_index++;
		calling.wuser_index &= 0xFFFF; /* wrap */

		inst->wuser = wuser;
	}

	return wuser;
}

static bool instance_valid(struct calling_instance *inst)
{
	bool found = false;
	struct le *le;

	if (!inst)
		return false;

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

	if (!instance_valid(inst)) {
		return false;
	}
	
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
	case WCALL_VIDEO_STATE_SCREENSHARE: return "SCREENSHARE";
	case WCALL_VIDEO_STATE_BAD_CONN:    return "BADCONN";
	case WCALL_VIDEO_STATE_PAUSED:      return "PAUSED";
	default: return "???";
	}
}

static void set_state(struct wcall *wcall, int st)
{
	bool trigger;
	struct calling_instance *inst;

	if (!wcall)
		return;

	trigger = wcall->state != st;
	inst = wcall->inst;

	info("wcall(%p): set_state: %s->%s\n",
	     wcall, wcall_state_name(wcall->state), wcall_state_name(st));

	wcall->state = st;

	if (trigger && inst && inst->stateh) {
		inst->stateh(wcall->convid, wcall->state, inst->arg);
	}
}


#if 0
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
#endif

struct wcall *wcall_lookup(struct calling_instance *inst, const char *convid)
{
	struct wcall *wcall;
	bool found = false;
	struct le *le;

	if (!inst || !convid)
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
				     const char *clientid,
				     int video_call,
				     int should_ring,
				     int conv_type,
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
		inst->incomingh(convid, msg_time, userid, clientid,
				video_call, should_ring, conv_type, inst->arg);
	}

	info(APITAG "wcall(%p): inst->incomingh took %llu ms \n",
	     inst, tmr_jiffies() - now);
}



static void icall_vstate_handler(struct icall *icall, const char *userid, const char *clientid,
	enum icall_vstate state, void *arg);
static void icall_audiocbr_handler(struct icall *icall, const char *userid,
	const char *clientid, int enabled, void *arg); 

static void icall_start_handler(struct icall *icall,
				uint32_t msg_time,
				const char *userid_sender,
				const char *clientid_sender,
				bool video,
				bool should_ring,
				enum icall_conv_type conv_type,
				void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	int ct = WCALL_CONV_TYPE_ONEONONE;

	(void)clientid_sender;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): egcall_start_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	set_state(wcall, WCALL_STATE_INCOMING);

	switch (conv_type) {
	case ICALL_CONV_TYPE_GROUP:
		ct = WCALL_CONV_TYPE_GROUP;
		break;
	case ICALL_CONV_TYPE_CONFERENCE:
		ct = WCALL_CONV_TYPE_CONFERENCE;
		break;
	case ICALL_CONV_TYPE_ONEONONE:
		ct = WCALL_CONV_TYPE_ONEONONE;
		break;
	default:
		warning("wcall(%p): incomingh unknown conv type %d\n", wcall, conv_type);

	}

	info(APITAG "wcall(%p): incomingh(%p) video:%s ring:%s conv:%s\n",
	     wcall, inst->incomingh,
	     video ? "yes" : "no",
	     should_ring ? "yes" : "no",
	     wcall_conv_type_name(ct));

	wcall->video.video_call = video;

	wcall->disable_audio = !should_ring;
	if (inst->mm && should_ring) {
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
						  clientid_sender,
						  video ? 1 : 0,
						  should_ring ? 1 : 0,
						  ct,
						  inst);
		}
		else {
			wcall_i_invoke_incoming_handler(wcall->convid, msg_time,
						userid_sender,
						clientid_sender,
						video ? 1 : 0,
						should_ring ? 1 : 0,
						ct,
						inst);
		}
	}
}


static void icall_answer_handler(struct icall *icall, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char convid_anon[ANON_ID_LEN];

	(void)icall;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): ecall_answer_handler: invalid wcall "
                        "inst=%p\n", wcall, inst);
		return;
	}

	info(APITAG "wcall(%p): answerh(%p) convid=%s\n", wcall, inst->answerh,
	     anon_id(convid_anon, wcall->convid));
	if (inst->answerh) {
		uint64_t now = tmr_jiffies();
		inst->answerh(wcall->convid, inst->arg);

		info(APITAG "wcall(%p): answerh took %llu ms \n",
		     wcall, tmr_jiffies() - now);
	}
	set_state(wcall, WCALL_STATE_ANSWERED);
}


AVS_EXPORT
const char *wcall_library_version(void)
{
	return avs_version_short();
}


static void icall_media_estab_handler(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      bool update,
				      void *arg)
{
	int err;
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char peer_userid_anon[ANON_ID_LEN];
	char convid_anon[ANON_ID_LEN];

	if (!WCALL_VALID(wcall)) {
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

	if (inst->mestabh) {
		inst->mestabh(wcall->convid, icall,
			      userid, clientid, inst->arg);
	}

	if (wcall->conv_type == WCALL_CONV_TYPE_ONEONONE) {
		if (inst->group.json.chgh) {
			call_group_change_json(inst, wcall);
		}
	}

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
	
	if (!WCALL_VALID(wcall)) {
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
		info(APITAG "wcall(%p): mstoph took %llu ms \n",
		     wcall, tmr_jiffies() - now);
	}
}


static int members_json(struct wcall *wcall, char **mjson, char **anon_str)
{
	struct wcall_members *members = NULL;
	struct json_object *tmembs = NULL;
	struct json_object *jmembs = NULL;
	struct mbuf *pmb = NULL;
	char uid_anon[ANON_ID_LEN];
	char cid_anon[ANON_CLIENT_LEN];
	size_t i;
	int err = 0;
	
	err = ICALL_CALLE(wcall->icall, get_members, &members);
	if (err)
		return EBADF;

	if (!members) {
		warning("wcall(%p): members_json: members is NULL\n", wcall);
		err = ENOSYS;
		goto out;
	}

	//info("wcall: members_json: %d members\n", members->membc);
	tmembs = jzon_alloc_object();
	jzon_add_str(tmembs, "convid", "%s", wcall->convid);

	jmembs = json_object_new_array();
	if (!mjson) {
		err = ENOSYS;
		goto out;
	}

	pmb = mbuf_alloc(512);
	mbuf_printf(pmb, "%d members:", members->membc);
	for(i = 0; i < members->membc; ++i) {
		struct wcall_member *memb = &members->membv[i];
		struct json_object *jmemb;

		jmemb = jzon_alloc_object();
		if (!jmemb)
			continue;

		jzon_add_str(jmemb, "userid", "%s", memb->userid);
		jzon_add_str(jmemb, "clientid", "%s", memb->clientid);
		jzon_add_int(jmemb, "aestab", memb->audio_state);
		jzon_add_int(jmemb, "vrecv", memb->video_recv);
		jzon_add_int(jmemb, "muted", memb->muted);

		json_object_array_add(jmembs, jmemb);

		/* add to info string */
		anon_id(uid_anon, memb->userid);
		anon_client(cid_anon, memb->clientid);
		mbuf_printf(pmb, "{[%s.%s] aestab: %d vrecv: %d muted: %d}",
			    uid_anon, cid_anon,
			    memb->audio_state,
			    memb->video_recv,
			    memb->muted);
		if (i < members->membc - 1)
			mbuf_printf(pmb, ",");
	}

	pmb->pos = 0;
	mbuf_strdup(pmb, anon_str, pmb->end);
	mem_deref(pmb);

	json_object_object_add(tmembs, "members", jmembs);
	
	jzon_encode(mjson, tmembs);

 out:
	mem_deref(members);
	mem_deref(tmembs);

	return err;
}

static void call_group_change_json(struct calling_instance *inst,
				   struct wcall *wcall)
{
	char *mjson = NULL;
	char *anon_json = NULL;
	int err;
	
	err = members_json(wcall, &mjson, &anon_json);
	if (err) {
		warning("wcall(%p): members_json failed: %m\n",
			wcall, err);
	}
	else if (inst->group.json.chgh) {
		uint64_t now;

		now = tmr_jiffies();

		info(APITAG "wcall(%p): group_chg_jsonh: %s\n",
		     wcall, anon_json);
		mem_deref(anon_json);
		
		inst->group.json.chgh(wcall->convid,
				      mjson,
				      inst->group.json.arg);
		info(APITAG "wcall(%p): group_chg_jsonh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}
	mem_deref(mjson);
}


static void icall_audio_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update,
				      void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char peer_userid_anon[ANON_ID_LEN];
	char peer_clientid_anon[ANON_CLIENT_LEN];
	char convid_anon[ANON_ID_LEN];

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): ecall_audio_estab_handler: "
			"invalid wcall inst=%p\n", wcall, inst);
		return;
	}

	info("wcall(%p): audio established(video=%d): "
	     "convid=%s peer_userid=%s peer_clientid=%s inst=%p mm=%p\n",
	     wcall, wcall->video.video_call,
	     anon_id(convid_anon, wcall->convid),
	     anon_id(peer_userid_anon, userid),
	     anon_client(peer_clientid_anon, clientid), inst, inst->mm);

	// TODO: check this, it should not be necessary
	msystem_stop_silencing();
	
	info(APITAG "wcall(%p): estabh(%p) peer_userid=%s\n",
	     wcall, inst->estabh, anon_id(peer_userid_anon, userid));

	if (!update && inst->estabh) {
		uint64_t now = tmr_jiffies();
		inst->estabh(wcall->convid, userid, clientid, inst->arg);

		info(APITAG "wcall(%p): estabh took %llu ms \n",
		     wcall, tmr_jiffies() - now);
	}
	if (wcall->conv_type == WCALL_CONV_TYPE_ONEONONE) {
		if (!update && inst->group.json.chgh)
			call_group_change_json(inst, wcall);
	}
}


static void icall_datachan_estab_handler(struct icall *icall,
					 const char *userid,
					 const char *clientid,
					 bool update,
					 void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char peer_userid_anon[ANON_ID_LEN];
	char peer_clientid_anon[ANON_CLIENT_LEN];
	char convid_anon[ANON_ID_LEN];

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): ecall_dce_estab_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	inst = wcall->inst;
	
	info("wcall(%p): data channel established for conversation %s "
	     "update=%d\n",
	     wcall, anon_id(convid_anon, wcall->convid), update);

	info(APITAG "wcall(%p): dcestabh(%p) "
	     "conv=%s peer_userid=%s peer_clientid=%s update=%d\n",
	     wcall, inst ? inst->dcestabh : NULL,
	     anon_id(convid_anon, wcall->convid),
	     anon_id(peer_userid_anon, userid),
	     anon_client(peer_clientid_anon, clientid),
	     update);

	if (inst && inst->dcestabh) {
		uint64_t now = tmr_jiffies();

		inst->dcestabh(wcall->convid,
			       userid,
			       clientid,
			       inst->arg);

		info(APITAG "wcall(%p): dcestabh took %llu ms \n",
		     wcall, tmr_jiffies() - now);
	}
}


static void icall_vstate_handler(struct icall *icall, const char *userid,
				 const char *clientid, enum icall_vstate state,
				 void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char userid_anon[ANON_ID_LEN];
	char convid_anon[ANON_ID_LEN];

	(void)clientid;

	if (!wcall) {
		warning("wcall(%p): vstateh wcall is NULL, "
			"ignoring props\n", wcall);
		return;
	}
	
	if (!WCALL_VALID(wcall)) {
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
	info(APITAG "wcall(%p): vstateh(%p) icall=%p conv=%s "
	     "user=%s state=%d\n",
	     wcall, inst->vstateh, icall, anon_id(convid_anon, wcall->convid),
	     anon_id(userid_anon, userid), wstate);

	if (inst->vstateh) {
		uint64_t now = tmr_jiffies();
		inst->vstateh(wcall->convid, userid, clientid,
			      wstate, inst->arg);
		info(APITAG "wcall(%p): vstateh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}
	if (wcall->conv_type != WCALL_CONV_TYPE_CONFERENCE
	    && inst->group.json.chgh) {
		call_group_change_json(inst, wcall);
	}
}


static void icall_audiocbr_handler(struct icall *icall, const char *userid, 
	const char *clientid, int enabled, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	(void)clientid;

	if (!WCALL_VALID(wcall)) {
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

	info(APITAG "wcall(%p): acbrh(%p) user=%s client=%s cbr=%d\n", wcall, inst->acbrh,
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid), enabled);
	if (inst->acbrh) {
		uint64_t now = tmr_jiffies();
		inst->acbrh(userid, clientid, enabled, inst->arg);
		info(APITAG "wcall(%p): acbrh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}
}


static void icall_muted_changed_handler(struct icall *icall,
					const char *userid,
					const char *clientid,
					int mute_state,
					void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char userid_anon[ANON_ID_LEN];
	char convid_anon[ANON_ID_LEN];

	(void)clientid;

	if (!wcall) {
		warning("wcall(%p): icall_mute_changed_handler wcall is NULL, "
			"ignoring props\n", wcall);
		return;
	}
	
	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): icall_mute_changed_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	if (!icall) {
		warning("wcall(%p): icall_mute_changed_handler icall is NULL, "
			"ignoring props\n", wcall);
		return;
	}

	info(APITAG "wcall(%p): mute_changed_handler icall=%p conv=%s "
	     "user=%s state=%d\n",
	     wcall, icall, anon_id(convid_anon, wcall->convid),
	     anon_id(userid_anon, userid), mute_state);

	if (inst->group.json.chgh)
		call_group_change_json(inst, wcall);
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

	case ENOONEJOINED:
		return WCALL_REASON_NOONE_JOINED;

	case EEVERYONELEFT:
		return WCALL_REASON_EVERYONE_LEFT;

	default:
		/* TODO: please convert new errors */
		warning("wcall: default reason (%d) (%m)\n", err, err);
		return WCALL_REASON_ERROR;
	}
}


static void icall_close_handler(struct icall *icall, int err,
				const char *metrics_json, uint32_t msg_time,
				const char *userid, const char *clientid,
				void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	int reason;

	if (!WCALL_VALID(wcall)) {
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
			     msg_time, userid, clientid, inst->arg);
		info(APITAG "wcall(%p): closeh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}

	info(APITAG "wcall(%p): metricsh(%p) json=%p\n", wcall, inst->metricsh, metrics_json);

	if (inst->metricsh && metrics_json) {
		uint64_t now = tmr_jiffies();
		inst->metricsh(wcall->convid, metrics_json, inst->arg);
		info(APITAG "wcall(%p): metricsh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}
out:
	mem_deref(wcall);
}


static void egcall_leave_handler(struct icall* icall, int reason, uint32_t msg_time, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): egcall_leave_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return;
	}

	set_state(wcall, WCALL_STATE_INCOMING);
	if (inst->closeh) {
		int wreason = WCALL_REASON_NORMAL;

		switch (reason) {
		case ICALL_REASON_STILL_ONGOING:
			wreason = WCALL_REASON_STILL_ONGOING;
			break;
		case ICALL_REASON_ANSWERED_ELSEWHERE:
			wreason = WCALL_REASON_ANSWERED_ELSEWHERE;
			break;
		case ICALL_REASON_REJECTED:
			wreason = WCALL_REASON_REJECTED;
			break;
		case ICALL_REASON_OUTDATED_CLIENT:
			wreason = WCALL_REASON_OUTDATED_CLIENT;
			break;
		case ICALL_REASON_TIMEOUT:
			wreason = WCALL_REASON_TIMEOUT;
			break;
		case ICALL_REASON_NOONE_JOINED:
			wreason = WCALL_REASON_NOONE_JOINED;
			break;
		case ICALL_REASON_EVERYONE_LEFT:
			wreason = WCALL_REASON_EVERYONE_LEFT;
			break;
		default:
			wreason = WCALL_REASON_NORMAL;
			break;
		}
		uint64_t now = tmr_jiffies();
		info(APITAG "wcall(%p): egcall_leave_handler: closeh(%p) "
		     "group=yes state=%s reason=%s\n",
		     wcall, inst->closeh, wcall_state_name(wcall->state),
		     wcall_reason_name(reason));

		inst->closeh(wreason, wcall->convid, msg_time,
			     inst->userid, inst->clientid, inst->arg);
		info(APITAG "wcall(%p): egcall_leave_handler: closeh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}
	wcall->disable_audio = true;
	if (!wcall_has_calls() && inst->mm) {
		mediamgr_set_call_state(inst->mm,
					MEDIAMGR_STATE_NORMAL);
	}
}


static void egcall_group_changed_handler(struct icall *icall, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	(void)icall;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): egcall_group_changed_handler: "
			"invalid wcall inst=%p\n", wcall, inst);
		return;
	}

	if (inst->group.chgh) {
		uint64_t now = tmr_jiffies();
		info(APITAG "wcall(%p): group_changedh\n", wcall);
		inst->group.chgh(wcall->convid, inst->group.arg);
		info(APITAG "wcall(%p): group_changedh took %llu ms\n",
		     wcall, tmr_jiffies() - now);
	}
	
	if (inst->group.json.chgh) {
		call_group_change_json(inst, wcall);
	}
}

static void egcall_metrics_handler(struct icall *icall,
				   const char *metrics_json,
				   void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;

	(void)icall;

	if (!WCALL_VALID(wcall)) {
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

static int targets_json(struct wcall *wcall, struct list *targets, char **mjson)
{
	struct le *le;
	struct json_object *tclients = NULL;
	struct json_object *jclients = NULL;	
	int err = 0;

	tclients = jzon_alloc_object();
	jclients = json_object_new_array();
	if (!mjson) {
		err = ENOSYS;
		goto out;
	}

	LIST_FOREACH(targets, le) {
		struct icall_client *client = le->data;
		struct json_object *jclient;

		jclient = jzon_alloc_object();
		if (!jclient)
			continue;

		jzon_add_str(jclient, "userid", "%s", client->userid);
		jzon_add_str(jclient, "clientid", "%s", client->clientid);

		json_object_array_add(jclients, jclient);
	}
	
	json_object_object_add(tclients, "clients", jclients);
	
	jzon_encode(mjson, tclients);

 out:
	mem_deref(tclients);

	return err;
}

static int icall_send_handler(struct icall *icall,
			      const char *userid,
			      struct econn_message *msg,
			      struct list *targets,
			      void *arg)
{	
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	struct wcall_ctx *ctx;
	void *context = wcall;
	char *str = NULL;
	char *tjson = NULL;
	char *tstr = NULL;
	int err = 0;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct le *le;
	size_t ntargets = 0;

	(void)icall;

	if (!WCALL_VALID(wcall)) {
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

	if (targets)
		ntargets = list_count(targets);

	if (ntargets > 0) {
		char uid_anon[ANON_ID_LEN];
		char cid_anon[ANON_CLIENT_LEN];
		struct mbuf *tmb;

		tmb = mbuf_alloc(512);
		LIST_FOREACH(targets, le) {
			struct icall_client *client = le->data;

			anon_id(uid_anon, client->userid),
			anon_client(cid_anon, client->clientid),
			mbuf_printf(tmb, "[%s.%s]", uid_anon, cid_anon);
			if (le->next)
				mbuf_printf(tmb, " ");
		}
		tmb->pos = 0;
		mbuf_strdup(tmb, &tstr, tmb->end);

		mem_deref(tmb);
	}
	
	info("wcall(%p): c3_message_send: convid=%s from=%s.%s to=%s "
	     "msg=%H ctx=%p\n",
	     wcall, anon_id(convid_anon, wcall->convid),
	     anon_id(userid_anon, userid), anon_client(clientid_anon, inst->clientid),
	     ntargets == 0 ? "ALL" : tstr,
	     econn_message_brief, msg, ctx);

	if (ntargets > 0) {
		err = targets_json(wcall, targets, &tjson);
		if (err)
			goto out;
	}

	err = inst->sendh(ctx, wcall->convid, userid, inst->clientid,
			  tjson, NULL, (uint8_t *)str, strlen(str),
			  msg->transient ? 1 : 0, inst->arg);

out:
	mem_deref(str);
	mem_deref(tstr);
	mem_deref(tjson);

	return err;
}

static int icall_sft_handler(struct icall *icall,
			     const char *url,
			     struct econn_message *msg,
			     void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	void *context = wcall;
	struct wcall_ctx *ctx;
	char *str = NULL;
	int err = 0;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): icall_sft_handler: invalid wcall "
			"inst=%p\n", wcall, inst);
		return ENODEV;
	}
	
	if (inst->sfth == NULL)
		return ENOSYS;

	err = ctx_alloc(&ctx, inst, context);
	if (err)
		goto out;

	err = econn_message_encode(&str, msg);
	if (err)
		goto out;

	info("wcall(%p): c3_message_send: convid=%s from=%s.%s to=SFT msg=%H ctx=%p\n",
	     wcall, anon_id(convid_anon, wcall->convid),
	     anon_id(userid_anon, inst->userid), anon_client(clientid_anon, inst->clientid),
	     econn_message_brief, msg, ctx);

	err = inst->sfth(ctx, url, (uint8_t*)str, strlen(str), inst->arg);

out:
	mem_deref(str);

	return err;
}

static void destructor(void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall->inst;
	bool has_calls;

	info("wcall(%p): dtor -- started\n", wcall);
	
	lock_write_get(inst->lock);
	list_unlink(&wcall->le);
	has_calls = wcall_has_calls();
	lock_rel(inst->lock);

	if (!has_calls) {
		if (inst->mm) {
			mediamgr_set_call_state(inst->mm,
						MEDIAMGR_STATE_NORMAL);
		}
		else {
			msystem_set_muted(false);		
		}
	}

	mem_deref(wcall->icall);
	mem_deref(wcall->convid);

	info("wcall(%p): dtor -- done\n", wcall);
}


static void icall_quality_handler(struct icall *icall,
				  const char *userid,
				  const char *clientid,
				  int rtt, int uploss, int downloss,
				  void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	int quality = WCALL_QUALITY_NORMAL;
	uint64_t now;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): ecall_quality_handler wcall not valid\n",
			wcall);
		return;
	}

	if (!inst->quality.netqh)
		return;

	if (uploss == ICALL_NETWORK_PROBLEM
	    && downloss == ICALL_NETWORK_PROBLEM)
		quality = WCALL_QUALITY_NETWORK_PROBLEM;
	if (rtt > 800 || uploss > 20 || downloss > 20)
		quality = WCALL_QUALITY_POOR;
	else if (rtt > 400 || uploss > 5 || downloss > 5)
		quality = WCALL_QUALITY_MEDIUM;

	info(APITAG "wcall(%p): calling netqh:%p %s.%s rtt=%d up=%d dn=%d q=%d\n",
	     wcall, inst->quality.netqh,
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid),
	     rtt, uploss, downloss, quality);
	now = tmr_jiffies();
	inst->quality.netqh(wcall->convid,
			    userid,
			    clientid,
			    quality,
			    rtt,
			    uploss,
			    downloss,
			    inst->quality.arg);
	info(APITAG "wcall(%p): netqh:%p (quality=%d) took %llu ms\n",
	     wcall, inst->quality.netqh, quality, tmr_jiffies() - now);
}


static  void icall_req_clients_handler(struct icall *icall, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	uint64_t now;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): icall_req_clients_handler wcall not valid\n",
			wcall);
		return;
	}

	if (!inst->clients_reqh)
		return;

	info(APITAG "wcall(%p): calling clients_reqh:%p \n",
	     wcall, inst->clients_reqh);
	now = tmr_jiffies();
	inst->clients_reqh(inst2wuser(inst), wcall->convid, inst->arg);

	info(APITAG "wcall(%p): clients_reqh took %llu ms\n",
	     wcall, tmr_jiffies() - now);
}


static void icall_aulevel_handler(struct icall *icall, struct list *levell, void *arg)
{
	struct wcall *wcall = arg;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	char *json_str = NULL;
	char *info_str = NULL;
	uint64_t now;
	int err = 0;

	if (!WCALL_VALID(wcall)) {
		warning("wcall(%p): icall_aulevel_handler wcall not valid\n",
			wcall);
		return;
	}

	info("icall_aulevel_handler(%p): %d levels\n", wcall, list_count(levell));
	
	if (!inst->active_speakerh)
		return;
	
	err = audio_level_json(levell,
			       inst->userid, inst->clientid,
			       &json_str, &info_str);
	if (err) {
		warning("icall_aulevel_handler(%p): could not create json\n", wcall);
		return;
	}
		
	info(APITAG "wcall(%p): calling active_speakerh:%p with: %s\n",
	     wcall, inst->clients_reqh, info_str);
	now = tmr_jiffies();
	
	inst->active_speakerh(inst2wuser(inst), wcall->convid, json_str, inst->arg);
	
	info(APITAG "wcall(%p): active_speakerh took %llu ms\n",
	     wcall, tmr_jiffies() - now);

	mem_deref(json_str);
	mem_deref(info_str);
}


int wcall_add(struct calling_instance *inst,
	      struct wcall **wcallp,
	      const char *convid,
	      int conv_type)
{	
	struct wcall *wcall;
	struct zapi_ice_server *turnv = NULL;
	size_t turnc = 0;
	struct zapi_ice_server *sftv = NULL;
	size_t sftc = 0;
	size_t i;
	int err;
	char convid_anon[ANON_ID_LEN];

	if (!inst || !wcallp || !convid)
		return EINVAL;

	wcall = wcall_lookup(inst, convid);
	if (wcall) {
		warning("wcall(%p): call_add: already have wcall "
			"for convid=%s\n", wcall, anon_id(convid_anon, convid));

		return EALREADY;
	}

	wcall = mem_zalloc(sizeof(*wcall), destructor);
	if (!wcall)
		return EINVAL;

	wcall->inst = inst;
	wcall->conv_type = conv_type;

	info(APITAG "wcall(%p): added for convid=%s inst=%p\n", wcall,
	     anon_id(convid_anon, convid), inst);
	str_dup(&wcall->convid, convid);

	lock_write_get(inst->lock);

	turnv = config_get_iceservers(inst->cfg, &turnc);
	if (turnc == 0) {
		info("wcall(%p): no turn servers\n", wcall);
	}

	sftv = config_get_sftservers(inst->cfg, &sftc);
	if (sftc == 0) {
		info("wcall(%p): no sft servers\n", wcall);
		if (WCALL_CONV_TYPE_CONFERENCE == conv_type) {
			info("wcall(%p): reverting conference to legacy "
			     "group call due to no SFT servers\n", wcall);
			conv_type = WCALL_CONV_TYPE_GROUP;
		}
	}

	switch (conv_type) {
	case WCALL_CONV_TYPE_ONEONONE: {
		struct ecall* ecall;
		err = ecall_alloc(&ecall, &inst->ecalls,
				  ICALL_CONV_TYPE_ONEONONE,
				  &inst->conf, inst->msys,
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
				    NULL, // icall_sft_handler,
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
				    icall_muted_changed_handler,
				    icall_quality_handler,
				    NULL, // no_relay_handler,
				    icall_req_clients_handler,
				    icall_aulevel_handler,
				    wcall);		
		}
		break;

	case WCALL_CONV_TYPE_GROUP: {
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
				    NULL, // icall_sft_handler,
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
				    icall_muted_changed_handler,
				    icall_quality_handler,
				    NULL, // no_relay_handler,
				    icall_req_clients_handler,
				    icall_aulevel_handler,
				    wcall);
		}
		break;

	case WCALL_CONV_TYPE_CONFERENCE: {
		struct ccall* ccall;
		err = ccall_alloc(&ccall,
				   &inst->conf,
				   convid,
				   inst->userid,
				   inst->clientid);

		if (err) {
			warning("wcall(%p): add: could not alloc ccall: %m\n",
				wcall, err);
			goto out;
		}

		wcall->icall = ccall_get_icall(ccall);
		icall_set_callbacks(wcall->icall,
				    icall_send_handler,
				    icall_sft_handler,
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
				    NULL, // muted_changed_handler,
				    icall_quality_handler,
				    NULL, // no_relay_handler,
				    icall_req_clients_handler,
				    icall_aulevel_handler,
				    wcall);

		ccall_set_config(ccall, inst->cfg);

		}
		break;
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

	for (i = 0; i < sftc; ++i) {
		struct zapi_ice_server *sft = &sftv[i];

		err = ICALL_CALLE(wcall->icall, add_sft,
				  sft->url);
		if (err) {
			warning("wcall(%p): error adding sft (%m)\n",
				wcall, err);
		}
	}

	if (inst->media_laddr) {
		err = ICALL_CALLE(wcall->icall, set_media_laddr,
				  inst->media_laddr);
		if (err) {
			warning("wcall(%p): error setting media laddr: %m\n",
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

AVS_EXPORT
void wcall_set_media_laddr(WUSER_HANDLE wuser, struct sa *laddr)
{
	struct calling_instance *inst;
	struct sa *maddr;

	if (!laddr)
		return;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_media_laddr: invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	inst->media_laddr = mem_deref(inst->media_laddr);
	maddr = mem_zalloc(sizeof(*maddr), NULL);
	if (!maddr) {
		warning("wcall(%p): could not alloc media laddr\n", inst);
		return;
	}
	sa_cpy(maddr, laddr);
	inst->media_laddr = maddr;
}


void wcall_i_mcat_changed(struct calling_instance *inst,
			  enum mediamgr_state state)
{
	struct le *le;
	
	info("wcall: mcat changed to: %d inst=%p\n", state, inst);

	if (!inst) {
		warning("wcall_i mcat_changed: no instance\n");
		return;
	}

	lock_write_get(inst->lock);
	le = inst->wcalls.head;
	while(le) {
		struct wcall *wcall = le->data;
		le = le->next;
		lock_rel(inst->lock);
	
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

		case MEDIAMGR_STATE_ERROR:
			if (wcall) {
				wcall_end_internal(wcall);
			}
			break;

		case MEDIAMGR_STATE_NORMAL:
		default:
			//if (wcall->icall) {
			//	ICALL_CALL(wcall->icall, media_stop);
			//}
			break;
		}
		lock_write_get(inst->lock);
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
int wcall_setup(void)
{
	int err = 0;
	info(APITAG "wcall_setup: starting...\n");

	err = libre_init();
	if (err) {
		warning("wcall_main: libre_init failed (%m)\n", err);
		return err;
	}

	err = avs_init(0);
	if (err) {
		warning("wcall_main: avs_init failed (%m)\n", err);
		return err;
	}

	// TODO: remove flowmgr
	err = flowmgr_init("voe");
	if (err) {
		error("wcall_main: failed to init flowmgr\n");
		return err;
	}

	log_set_min_level(LOG_LEVEL_DEBUG);

	return err;
}

AVS_EXPORT
int wcall_init(int env)
{
	int err = 0;
	info(APITAG "wcall: init: initialized=%d env=%d\n", calling.initialized, env);

#if ENABLE_PEERFLOW
	/* Ensure that Android linker pulls in all wcall symbols */
	wcall_get_members(WUSER_INVALID_HANDLE, NULL);

	peerflow_set_funcs();
#endif

	if (calling.initialized)
		return EALREADY;

	calling.initialized = true;
	calling.env = env;

	msystem_set_env(env);

	list_init(&calling.logl);
	list_init(&calling.instances);

	err = lock_alloc(&calling.lock);
	if (err)
		warning("wcall_init: could not allocate lock: %m\n", err);

#if (defined ANDROID || defined __EMSCRIPTEN__)
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
#ifndef __EMSCRIPTEN__
	dns_close();
#endif

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
		err = inst->cfg_reqh(inst->wuser, inst->arg);
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
WUSER_HANDLE wcall_create(const char *userid,
			  const char *clientid,
			  wcall_ready_h *readyh,
			  wcall_send_h *sendh,
			  wcall_sft_req_h *sfth,
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
	bool use_mediamgr;

#ifdef __EMSCRIPTEN__
	use_mediamgr = false;
#else
	use_mediamgr = true;
#endif
		
	return wcall_create_ex(userid,
			       clientid,
			       use_mediamgr,
			       "voe",
			       readyh,
			       sendh,
			       sfth,
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
	struct calling_instance *inst;
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
		ide->shuth(ide->inst->wuser, ide->shuth_arg);
	
	mem_deref(ide->inst);
	mem_deref(ide);
}


static void instance_destroy(struct calling_instance *inst)
{
	struct le *le;

	if (inst->thread_run) {
		inst->thread_run = false;

		debug("wcall: joining thread..\n");

		pthread_join(inst->tid, NULL);
		pthread_detach(inst->tid);
		inst->tid = 0;
	}

	uintptr_t vuser = inst->wuser;
	msystem_unregister_listener((void*)vuser);
	tmr_cancel(&inst->tmr_roam);

	/* Dont call list_flush as we expect a valid list
	   in the wcall destructor */
	le = inst->wcalls.head;
	while(le) {
		if (le->data) {
			mem_deref(le->data);
			le = inst->wcalls.head;
		}
		else {
			le = le->next;
		}
	}
	list_flush(&inst->ctxl);

	lock_write_get(inst->lock);
	list_unlink(&inst->le);
	list_flush(&inst->ecalls);

	inst->userid = mem_deref(inst->userid);
	inst->clientid = mem_deref(inst->clientid);
	inst->mm = mem_deref(inst->mm);
	inst->msys = mem_deref(inst->msys);
	inst->cfg = mem_deref(inst->cfg);
	inst->media_laddr = mem_deref(inst->media_laddr);

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

static void msys_mute_handler(bool muted, void *arg)
{
	WUSER_HANDLE wuser = (WUSER_HANDLE)arg;
	struct calling_instance *inst;
	struct le *le;
	uint64_t now = tmr_jiffies();

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: msys_mute_handler: invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	info(APITAG "wcall(%p): calling muteh: %p muted=%d\n",
	     inst, inst->mute.h, muted);

	if (inst->mute.h) {
		inst->mute.h(muted ? 1 : 0, inst->mute.arg);
	}
	info(APITAG "wcall(%p): inst->muteh took %llu ms \n",
	     inst, tmr_jiffies() - now);

	LIST_FOREACH(&inst->wcalls, le) {
		struct wcall *wcall = le->data;

		if (wcall) {
			ICALL_CALL(wcall->icall, update_mute_state);
			call_group_change_json(inst, wcall);
		}
	}
	
}


AVS_EXPORT
WUSER_HANDLE wcall_create_ex(const char *userid,
			     const char *clientid,
			     int use_mediamgr,
			     const char *msys_name,
			     wcall_ready_h *readyh,
			     wcall_send_h *sendh,
			     wcall_sft_req_h *sfth,
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
	WUSER_HANDLE wuser = WUSER_INVALID_HANDLE;			
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct calling_instance *inst = NULL;
	int err;

	if (!str_isset(userid) || !str_isset(clientid))
		return WUSER_INVALID_HANDLE;

	info(APITAG "wcall: create userid=%s clientid=%s\n",
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid));

	inst = mem_zalloc(sizeof(*inst), instance_destructor);
	if (inst == NULL) {
		err = ENOMEM;
		goto out;
	}

	wuser = create_wuser(inst);

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
	inst->sfth = sfth;
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

	uintptr_t vuser = inst->wuser;
	err = msystem_get(&inst->msys, msys_name, NULL,
			  msys_mute_handler, (void*)vuser);
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
	
	//err = async_cfg_wait(inst);

out:
	if (err) {
		wcall_i_destroy(inst);
		inst = NULL;
		wuser = WUSER_INVALID_HANDLE;
	}

	
	info(APITAG "wcall: create return inst=%p hnd=0x%08X\n",
	     inst, wuser);
	
	return wuser;
}

AVS_EXPORT
void wcall_set_shutdown_handler(WUSER_HANDLE wuser,
				wcall_shutdown_h *shuth, void *arg)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_shutdown_handler: invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	inst->shuth = shuth;
	inst->shuth_arg = arg;
}

void wcall_i_destroy(struct calling_instance *inst)
{
	info(APITAG "wcall: destroy inst=%p\n", inst);
	
	if (!inst) {
		warning("wcall_destroy: no instance\n");
		return;
	}

	instance_destroy(inst);
}


AVS_EXPORT
void wcall_destroy(WUSER_HANDLE wuser)	
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: destroy: invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	if (inst->shuth)
		wcall_marshal_destroy(inst);
	else
		wcall_i_destroy(inst);
}

AVS_EXPORT
int wcall_i_start(struct wcall *wcall,
		  int call_type, int conv_type,
		  int audio_cbr)
{
	int err = 0;
	struct calling_instance *inst = wcall ? wcall->inst : NULL;
	bool cbr = audio_cbr != 0;
	char convid_anon[ANON_ID_LEN];

	call_type = (call_type == WCALL_CALL_TYPE_FORCED_AUDIO) ?
		    WCALL_CALL_TYPE_NORMAL : call_type;

	if (!WCALL_VALID(wcall)) {
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
		if (is_video_call) {
			ICALL_CALL(wcall->icall,
				   set_video_send_state,
				   ICALL_VIDEO_STATE_STARTED);
		}
		else {
			ICALL_CALL(wcall->icall,
				   set_video_send_state,
				   ICALL_VIDEO_STATE_STOPPED);
		}

		err = ICALL_CALLE(wcall->icall, start,
			call_type, cbr);
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


int wcall_i_answer(struct wcall *wcall,
		   int call_type, int audio_cbr)
{
	int err = 0;
	bool cbr = audio_cbr != 0;

	if (!wcall) {
		warning("wcall; answer: no wcall\n");
		return EINVAL;
	}

	call_type = (call_type == WCALL_CALL_TYPE_FORCED_AUDIO) ?
		    WCALL_CALL_TYPE_NORMAL : call_type;

	info(APITAG "wcall(%p): answer calltype=%s cbr=%d\n",
	     wcall, wcall_call_type_name(call_type), audio_cbr);

	if (wcall->disable_audio)
		wcall->disable_audio = false;
	
	if (!wcall->icall) {
		warning("wcall(%p): answer: no call object found\n", wcall);
		return ENOTSUP;
	}
	set_state(wcall, WCALL_STATE_ANSWERED);

	if (call_type == WCALL_CALL_TYPE_VIDEO) {
		ICALL_CALL(wcall->icall,
			   set_video_send_state,
			   ICALL_VIDEO_STATE_STARTED);
	}
	else {
		ICALL_CALL(wcall->icall,
			   set_video_send_state,
			   ICALL_VIDEO_STATE_STOPPED);
	}
	
	err = ICALL_CALLE(wcall->icall, answer,
			  call_type, cbr);

	return err;
}


void wcall_i_resp(struct calling_instance *inst,
		  int status, const char *reason, void *arg)
{
	struct wcall_ctx *ctx = arg;
	struct wcall *wcall = ctx ? ctx->context : NULL;
	struct le *le;

	info(APITAG "wcall(%p): resp: status=%d reason=[%s] ctx=%p\n",
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


void wcall_i_config_update(struct calling_instance *inst,
			   int err, const char *json_str)
{
	info("wcall(%p): config_update: err=%d json=%zu bytes\n",
	     inst, err, str_len(json_str));
	
	if (!inst)
		return;

	err = config_update(inst->cfg, err, json_str, str_len(json_str));
	if (err)
		warning("wcall(%p): config_update failed: %m\n", inst, err);
}

void wcall_i_sft_resp(struct calling_instance *inst,
		      int status, struct econn_message *msg, void *arg)
{
	struct wcall_ctx *ctx = arg;
	struct wcall *wcall = ctx ? ctx->context : NULL;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct le *le;

	if (!ctx || !wcall) {
		warning("wcall(%p): sft_resp: ctx:%p not valid\n", wcall, ctx);
		return;
	}

	lock_write_get(inst->lock);
	LIST_FOREACH(&inst->ctxl, le) {
		struct wcall_ctx *at = le->data;

		if (at == ctx) {
			info("wcall(%p): c3_message_recv: convid=%s from=SFT to=%s.%s msg=%H ctx=%p\n",
			     wcall, anon_id(convid_anon, wcall->convid),
			     anon_id(userid_anon, inst->userid),
			     anon_client(clientid_anon, inst->clientid),
			     econn_message_brief, msg, inst);

			ICALL_CALLE(wcall->icall, sft_msg_recv,
				    status, msg);
			goto out;
		}
	}

	warning("wcall(%p): sft_resp: ctx:%p not found\n", wcall, ctx);
	ctx = NULL;

 out:
	lock_rel(inst->lock);
	mem_deref(ctx);
}


void wcall_i_recv_msg(struct calling_instance *inst,
		      struct econn_message *msg,
		      uint32_t curr_time,
		      uint32_t msg_time,
		      const char *convid,
		      const char *userid,
		      const char *clientid)
{
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

	wcall = wcall_lookup(inst, convid);
	
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
					      userid, clientid,
					      is_video ? 1 : 0,
					      inst->arg);

				info("wcall(%p): inst->missedh (%s) "
				     "took %llu ms\n",
				     wcall, is_video ? "video" : "audio",
				     tmr_jiffies() - now);
			}
		}

		return;
	}
	
	if (!wcall) {
		if (msg->msg_type == ECONN_GROUP_START
		    && econn_message_isrequest(msg)) {
			err = wcall_add(inst, &wcall, convid,
					WCALL_CONV_TYPE_GROUP);
		}
		else if (msg->msg_type == ECONN_GROUP_CHECK
		    && !econn_message_isrequest(msg)) {
			err = wcall_add(inst, &wcall, convid,
					WCALL_CONV_TYPE_GROUP);
		}
		else if (msg->msg_type == ECONN_CONF_START
		    && econn_message_isrequest(msg)) {
			err = wcall_add(inst, &wcall, convid,
					WCALL_CONV_TYPE_CONFERENCE);
		}
		else if (msg->msg_type == ECONN_CONF_CHECK
		    && !econn_message_isrequest(msg)) {
			err = wcall_add(inst, &wcall, convid,
					WCALL_CONV_TYPE_CONFERENCE);
		}
		else if (econn_is_creator(inst->userid, userid, msg)) {
			err = wcall_add(inst, &wcall, convid,
					WCALL_CONV_TYPE_ONEONONE);

			if (err) {
				warning("wcall(%p): recv_msg: could not "
					"add call: %m\n", wcall, err);
				goto out;
			}
		}
		else {
			err = EPROTO;
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


static bool wcall_has_calls(void)
{
	struct le *instle;

	LIST_FOREACH(&calling.instances, instle) {
		struct calling_instance *inst = instle->data;
		struct le *le;
		
		LIST_FOREACH(&inst->wcalls, le) {
			struct wcall *wcall = le->data;

			if (!wcall)
				continue;
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

	if (wcall->state != WCALL_STATE_TERM_REMOTE)
		set_state(wcall, WCALL_STATE_TERM_LOCAL);
	ICALL_CALL(wcall->icall, end);

	wcall->disable_audio = true;
	if (!wcall_has_calls()) {
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
	int reason = WCALL_REASON_REJECTED;

	info(APITAG "wcall(%p): reject convid=%s\n", wcall,
	     wcall ? anon_id(convid_anon, wcall->convid) : "");

	if (!wcall)
		return EINVAL;

	inst = wcall->inst;

	info("wcall(%p): reject: convid=%s\n", wcall, anon_id(convid_anon, wcall->convid));

	switch (wcall->conv_type) {
	case WCALL_CONV_TYPE_GROUP:
	case WCALL_CONV_TYPE_CONFERENCE:
		reason = WCALL_REASON_STILL_ONGOING;
		break;

	case WCALL_CONV_TYPE_ONEONONE:
	default:
		reason = WCALL_REASON_REJECTED;
		break;
	}

	wcall->disable_audio = true;
	if (!wcall_has_calls()) {
		if (wcall->inst->mm) {
			mediamgr_set_call_state(wcall->inst->mm,
						MEDIAMGR_STATE_NORMAL);
		}
	}

	if (wcall->state == WCALL_STATE_INCOMING) {
		ICALL_CALL(wcall->icall, reject);
	
		if (inst->closeh) {
			uint64_t now = tmr_jiffies();
			info(APITAG "wcall(%p): wcall_reject: closeh(%p) "
			     "state=%s reason=%s\n",
			     wcall, inst->closeh, wcall_state_name(wcall->state),
			     wcall_reason_name(reason));

			inst->closeh(reason, wcall->convid,
				ECONN_MESSAGE_TIME_UNKNOWN, inst->userid,
				inst->clientid, inst->arg);
			info(APITAG "wcall(%p): wcall_reject: closeh took %llu ms\n",
			     wcall, tmr_jiffies() - now);
		}
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
void wcall_set_media_estab_handler(WUSER_HANDLE wuser,
				   wcall_media_estab_h *mestabh)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_media_estab_h: invalid wuser=0x%08X\n",
			wuser);
		return;
	}
	
	inst->mestabh = mestabh;
}


AVS_EXPORT
void wcall_set_media_stopped_handler(WUSER_HANDLE wuser,
				     wcall_media_stopped_h *mstoph)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_media_stopped_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return;
	}	

	inst->mstoph = mstoph;
}


AVS_EXPORT
void wcall_set_data_chan_estab_handler(WUSER_HANDLE wuser,
				       wcall_data_chan_estab_h *dcestabh)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_data_chan_estab_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return;
	}	
	
	inst->dcestabh = dcestabh;
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
void wcall_set_state_handler(WUSER_HANDLE wuser, wcall_state_change_h *stateh)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_state_handler: invalid wuser=0x%08X\n",
			wuser);
		return;
	}

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
	case WCALL_VIDEO_STATE_SCREENSHARE:
		vstate = ICALL_VIDEO_STATE_SCREENSHARE;
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
		ICALL_CALL(wcall->icall,
			   set_video_send_state,
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
int wcall_is_video_call(WUSER_HANDLE wuser, const char *convid)
{
	struct calling_instance *inst;
	struct wcall *wcall;
	char convid_anon[ANON_ID_LEN];
	
	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: is_video_call: invalid wuser=0x%08X\n",
			wuser);
		return 0;
	}

	wcall = wcall_lookup(inst, convid);
	if (wcall) {
		info(APITAG "wcall(%p): is_video_call convid=%s is_video=%s\n",
		     wcall, anon_id(convid_anon, wcall->convid),
		     wcall->video.video_call ? "yes" : "no");	
		return wcall->video.video_call;
	}

	info(APITAG "wcall(%p): is_video_call convid=%s is_video=no\n",
	     wcall, wcall ? anon_id(convid_anon, wcall->convid) : "NULL");
	
	return 0;
}


int  wcall_debug(struct re_printf *pf, WUSER_HANDLE wuser)
{
	struct calling_instance *inst;
	struct le *le;	
	char convid_anon[ANON_ID_LEN];
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: debug: invalid wuser=0x%08X\n",
			wuser);
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
int  wcall_stats(struct re_printf *pf, WUSER_HANDLE wuser)
{
	struct calling_instance *inst;
	struct le *le;	
	int err = 0;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: stats: invalid wuser=0x%08X\n",
			wuser);
		re_hprintf(pf, "\n");
		return 0;
	}

	LIST_FOREACH(&inst->wcalls, le) {
		struct wcall *wcall = le->data;

		if (wcall->icall && wcall->icall->stats) {
			err |= re_hprintf(pf, "%H\n", wcall->icall->stats,
					  wcall->icall);
		}
	}
	
	return err;	
}


AVS_EXPORT
void wcall_set_trace(WUSER_HANDLE wuser, int trace)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_trace: invalid wuser=0x%08X\n",
			wuser);
		return;
	}
	
	inst->conf.trace = trace;
}


AVS_EXPORT
int wcall_get_state(WUSER_HANDLE wuser, const char *convid)
{
	struct calling_instance *inst;
	struct wcall *wcall;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: get_state: invalid wuser=0x%08X\n",
			wuser);
		return EINVAL;
	}
	
	wcall = wcall_lookup(inst, convid);

	return wcall ? wcall->state : WCALL_STATE_UNKNOWN;
}


AVS_EXPORT
void wcall_iterate_state(WUSER_HANDLE wuser,
			 wcall_state_change_h *stateh, void *arg)	
{
	struct calling_instance *inst;
	struct wcall *wcall;
	struct le *le;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: iterate_state: invalid wuser=0x%08X\n",
			wuser);
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
void wcall_set_group_changed_handler(WUSER_HANDLE wuser,
				     wcall_group_changed_h *chgh,
				     void *arg)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_group_changed_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	info(APITAG "wcall: set_group_changed_handler %p inst=%p\n",
	     chgh, inst);

	if (!inst) {
		warning("wcall_set_group_changed_handler: no instance\n");
		return;
	}

	inst->group.chgh = chgh;
	inst->group.arg = arg;
}

AVS_EXPORT
void wcall_set_participant_changed_handler(WUSER_HANDLE wuser,
					   wcall_participant_changed_h *chgh,
					   void *arg)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_participant_changed_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	info(APITAG "wcall: set_participant_changed_handler %p inst=%p\n",
	     chgh, inst);

	if (!inst) {
		warning("wcall_set_group_changed_handler: no instance\n");
		return;
	}

	inst->group.json.chgh = chgh;
	inst->group.json.arg = arg;
}


AVS_EXPORT
struct wcall_members *wcall_get_members(WUSER_HANDLE wuser, const char *convid)
{
	struct calling_instance *inst;
	struct wcall_members *members;
	struct wcall *wcall;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: get_members: invalid wuser=0x%08X\n",
			wuser);
		return NULL;
	}

	wcall = wcall_lookup(inst, convid);

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
void wcall_enable_privacy(WUSER_HANDLE wuser, int enabled)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: enable_privacy: invalid wuser=0x%08X\n",
			wuser);
		return;
	}

	info(APITAG "wcall: enable_privacy enabled=%d inst=%p\n",
	     enabled, inst);

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
	case WCALL_REASON_OUTDATED_CLIENT:    return "OutdatedClient";
	case WCALL_REASON_NOONE_JOINED:       return "NooneJoined";
	case WCALL_REASON_EVERYONE_LEFT:      return "EveryoneLeft";

	default: return "???";
	}
}


/**
 * Return a borrowed reference to the Media-Manager
 */
struct mediamgr *wcall_mediamgr(WUSER_HANDLE wuser)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: mediamgr: invalid wuser=0x%08X\n",
			wuser);
		return NULL;
	}
	
	return inst->mm;
}


void wcall_handle_frame(struct avs_vidframe *frame)
{
	if (!frame)
		return;
	
#if ENABLE_PEERFLOW
	capture_source_handle_frame(frame);
#endif
}


struct wcall_marshal *wcall_get_marshal(struct calling_instance *inst)
{
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

	log_enable_stderr(false);
	
	loge->logh = logh;
	loge->arg = arg;

	loge->logger.h = wcall_log_handler;
	loge->logger.arg = loge;

	log_register_handler(&loge->logger);

	lock_write_get(calling.lock);
	list_append(&calling.logl, &loge->le, loge);
	lock_rel(calling.lock);
}


#if USE_AVSLIB
static void netprobe_handler(int err, const struct netprobe_result *result,
			     void *arg)
{
	struct calling_instance *inst = arg;

	inst->netprobe = mem_deref(inst->netprobe);

	inst->netprobeh(err, result->rtt_avg,
			result->n_pkt_sent, result->n_pkt_recv,
			inst->netprobeh_arg);
}


int wcall_netprobe(WUSER_HANDLE wuser,
		   size_t pkt_count, uint32_t pkt_interval_ms,
		   wcall_netprobe_h *netprobeh, void *arg)
{
	struct calling_instance *inst;
	struct zapi_ice_server *turnv = NULL;
	struct zapi_ice_server *turn;
	struct stun_uri uri;
	bool found = false;
	size_t turnc = 0;
	size_t i;
	int err;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: netprobe: invalid wuser=0x%08X\n", wuser);
		return EINVAL;
	}
	
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
#endif


AVS_EXPORT
int wcall_set_network_quality_handler(WUSER_HANDLE wuser,
				      wcall_network_quality_h *netqh,
				      int interval,
				      void *arg)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_network_quality_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return EINVAL;
	}	

	info(APITAG "wcall: set_quality_handler fn=%p int=%d inst=%p\n",
		netqh, interval, inst);

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
	iflow_set_video_handlers(render_frame_h, size_h, arg);
}


AVS_EXPORT
int wcall_i_dce_send(struct wcall *wcall, struct mbuf *mb)
{
	return ICALL_CALLE(wcall->icall, dce_send, mb);
}

AVS_EXPORT
void wcall_thread_main(int *err, int *initialized)
{
	int e;

	*err = 0;
	*initialized = 0;
    
	e = wcall_init(WCALL_ENV_DEFAULT);
	if (e) {
		error("wcall_main: failed to init wcall\n");
		goto out;
	}

	*initialized = e == 0;
	*err = e;

	re_main(NULL);

out:
	flowmgr_close();
	avs_close();

	info("wcall_main: done\n");
	return;
}


AVS_EXPORT
void wcall_set_req_clients_handler(WUSER_HANDLE wuser,
				   wcall_req_clients_h *reqch)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_req_clients_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return;
	}	
	
	info(APITAG "wcall: set_req_clients_handler %p inst=%p\n", reqch, inst);
	
	inst->clients_reqh = reqch;
}


AVS_EXPORT	
void wcall_set_active_speaker_handler(WUSER_HANDLE wuser,
				      wcall_active_speaker_h *activeh)
{
	struct calling_instance *inst;

	inst = wuser2inst(wuser);
	if (!inst) {
		warning("wcall: set_req_clients_handler: "
			"invalid wuser=0x%08X\n",
			wuser);
		return;
	}	
	
	info(APITAG "wcall: set_active_speaker_handler %p inst=%p\n", activeh, inst);
	
	inst->active_speakerh = activeh;	
}


void wcall_i_set_clients_for_conv(struct wcall *wcall, const char *json)
{
	struct json_object *jobj, *jclients;
	size_t nclients, i;
	struct list clientl = LIST_INIT;
	
	size_t len;
	int err = 0;

	if (!wcall) {
		warning("wcall; set_clients_for_conv: no wcall\n");
		return;
	}
	
	if (!wcall->icall) {
		warning("wcall; set_clients_for_conv: no icall\n");
		return;
	}

	info(APITAG "wcall(%p): set_clients_for_conv\n", wcall);

	len = strlen(json);
	err = jzon_decode(&jobj, json, len);
	if (err)
		return;

#if 0
	jzon_dump(jobj);
#endif

	err = jzon_array(&jclients, jobj, "clients");
	if (err)
		goto out;

	if (!jzon_is_array(jclients)) {
		warning("json object is not an array\n");
		goto out;
	}

	nclients = json_object_array_length(jclients);

	for (i = 0; i < nclients; ++i) {
		const char *uid, *cid;
		struct json_object *jcli;
		struct icall_client *cli;

		jcli = json_object_array_get_idx(jclients, i);
		if (!jcli) {
			goto out;
		}

		uid = jzon_str(jcli, "userid");
		cid = jzon_str(jcli, "clientid");
		if (uid && cid) {
			cli = icall_client_alloc(uid, cid);
			list_append(&clientl, &cli->le, cli);
		}
	}

	ICALL_CALL(wcall->icall, set_clients,
		&clientl);

out:
	mem_deref(jobj);
	list_flush(&clientl);
	return;
}

AVS_EXPORT
void wcall_poll(void)
{
	re_poll();
}

AVS_EXPORT
int wcall_get_mute(WUSER_HANDLE wuser)
{
	(void)wuser;

	return msystem_get_muted();
}

void wcall_i_set_mute(int muted)
{
	info(APITAG "wcall: set_mute: muted=%d\n", muted);
	
	msystem_set_muted(muted != 0);
}


AVS_EXPORT
void wcall_set_mute_handler(WUSER_HANDLE wuser, wcall_mute_h *muteh, void *arg)
{
	struct calling_instance *inst = wuser2inst(wuser);

	if (inst == NULL) {
		warning("wcall: set_mute_handler: invalid wuser=0x%08x\n",
			wuser);
		return;
	}

	inst->mute.h = muteh;
	inst->mute.arg = arg;
}

AVS_EXPORT
int wcall_set_proxy(const char *host, int port)
{
	return msystem_set_proxy(host, port);
}

/*
static void *avs_thread(void *arg)
{
	wcall_thread_main(&calling.run_err, &calling.run_init);
	return NULL;
}

AVS_EXPORT
int wcall_run(void)
{
	calling.run_init = calling.run_err = 0;

	pthread_create(&calling.tid, NULL, avs_thread, NULL);
	while(!calling.run_init && calling.run_err == 0)
		usleep(100000);

	return calling.run_err;
}
*/

