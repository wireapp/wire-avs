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
#define _POSIX_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

#include <re/re.h>

#include "avs.h"
#include "avs_version.h"
#include "avs_vie.h"

#include "flowmgr.h"


static struct {
	struct list fml;

	struct msystem *msys;
} fsys;


int flowmgr_append_convlog(struct flowmgr *fm, const char *convid,
			   const char *msg)
{
	return ENOSYS;
}


/* This is just a dummy handler fo waking up re_main() */


void flowmgr_start_volume(void)
{
}


void flowmgr_cancel_volume(void)
{
}


int flowmgr_init(const char *msysname)
{
	int err;

	if (fsys.msys != NULL)
		return 0;

	info("flowmgr_init: msys=%s\n", msysname);

	err = marshal_init();
	if (err) {
		error("flow-manager: failed to init marshalling\n");

		return err;
	}

	err = msystem_get(&fsys.msys, msysname, NULL);
	if (err) {
		warning("flowmgr: msystem_init failed: %m\n", err);
		goto out;
	}

	info("flowmgr: initialized -- %s [machine %H]\n",
	     avs_version_str(), sys_build_get, 0);

	msystem_set_tid(fsys.msys, pthread_self());

 out:
	if (err)
		fsys.msys = mem_deref(fsys.msys);

	return err;
}


#if 0
static void config_handler(struct call_config *cfg, void *arg)
{
	struct flowmgr *fm = arg;

	fm->config.pending = false;
	fm->config.ready = true;

	msystem_set_call_config(fsys.msys, cfg);

#if 0
	le = fm->postl.head;
	while(le) {
		struct call *call = le->data;
		
		le = le->next;

		//call_postponed_flows(call);
		//list_unlink(&call->post_le);
	}
#endif
}
#endif

int flowmgr_start(void)
{
	struct le *le;
	int err = 0;

	msystem_start(fsys.msys);
	
	LIST_FOREACH(&fsys.fml, le) {
		struct flowmgr *fm = le->data;

		(void)fm;
		//err |= flowmgr_config_starth(fm, config_handler, fm);
	}

	return err;
}


void flowmgr_close(void)
{
	flowmgr_wakeup();
	marshal_close();
	// XXX What is the lifetime of msystem? 
	fsys.msys = mem_deref(fsys.msys);
}


void flowmgr_network_changed(struct flowmgr *fm)
{
}


void flowmgr_mcat_changed(struct flowmgr *fm, const char *convid,
			  enum flowmgr_mcat mcat)
{
}


static bool rr_exist(const struct flowmgr *fm, struct rr_resp *rr)
{
	struct le *le;

	if (!fm || !rr)
		return false;

	for (le = fm->rrl.head; le; le = le->next) {
		if (rr == le->data)
			return true;
	}

	return false;
}


int flowmgr_send_request(struct flowmgr *fm, struct call *call,
		         struct rr_resp *rr,
		         const char *path, const char *method,
		         const char *ctype, struct json_object *jobj)
{
	char *json = NULL;
	int err;

	if (!fm)
		return EINVAL;

	if (jobj) {
		err = jzon_encode(&json, jobj);
		if (err) {
			warning("flowmgr: send_req:"
				" could not encode JSON-object (%p)\n", jobj);
			return err;
		}
	}

	if (rr) {
		re_snprintf(rr->debug, sizeof(rr->debug),
			    "%s %s", method, path);
	}

	info("flowmgr(%p) http_req(%p) %s %s %s\n",
	     fm, rr, method, path, json);
	
	err = fm->reqh(rr, path, method,
		       ctype, json, str_len(json), fm->sarg);
	if (err) {
		warning("flowmgr: send_req: fm->reqh failed"
			" [%s %s %s %zu] (%m)\n",
			method, path, ctype, str_len(json), err
			);
	}
	mem_deref(json);
	return err;
}


int flowmgr_resp(struct flowmgr *fm, int status, const char *reason,
		 const char *ctype, const char *content, size_t clen,
		 struct rr_resp *rr)
{
	struct json_object *jobj = NULL;
	int err = 0;

	if (status >= 400) {
		warning("flowmgr_resp(%p): rr=%p status=%d content=%b\n",
			fm, rr, status, content, clen);
	}

	if (!fm)
		return EINVAL;
	
	if (!rr || !rr_exist(fm, rr)) {
	        if (rr)
			warning("flowmgr(%p): rr_resp %p does not exist\n",
				fm, rr);
			
		if (content && clen) {
			info("flowmgr(%p): http_resp(norr): %d %s %b\n",
			     fm, status, reason, content, clen);
		}
		else {
			info("flowmgr(%p): http_resp(norr): %d %s\n",
			     fm, status, reason);
		}

		return rr ? ENOENT : EINVAL;
	}

	if (content && clen) {
		info("flowmgr(%p): http_resp(%p): %d %s (%zu bytes)\n",
		     fm, rr, status, reason, clen);
	}
	else {
		info("flowmgr(%p): http_resp(%p): %d %s\n",
		     fm, rr, status, reason);
	}

	if (ctype && !streq(ctype, CTYPE_JSON)) {
		warning("avs: flowmgr(%p): rest_resp: "
			"invalid content type: %s\n",
			fm, ctype);
		err = EPROTO;
		goto out;
	}

	if (ctype && content && clen > 0) {

		err = jzon_decode(&jobj, content, clen);
		if (err) {
			warning("flowmgr(%p): flowmgr_resp: JSON parse error"
				" [%zu bytes]\n",
				fm, clen);
			goto out;
		}
	}

	if (rr)
		rr_response(rr);
	if (rr && rr->resph) {
		rr->resph(status, rr, jobj, rr->arg);
	}

 out:
	mem_deref(jobj);
	mem_deref(rr);

	return err;
}


int flowmgr_post_flows(struct call *call)
{
	return ENOSYS;
}


int flowmgr_acquire_flows(struct flowmgr *fm, const char *convid,
			  const char *sessid,
			  flowmgr_netq_h *qh, void *arg)
{
	return ENOSYS;
}


/* note: username is optional */
int flowmgr_user_add(struct flowmgr *fm, const char *convid,
		     const char *userid, const char *username)
{
	return ENOSYS;
}


void flowmgr_set_active(struct flowmgr *fm, const char *convid, bool active)
{
}


void flowmgr_release_flows(struct flowmgr *fm, const char *convid)
{
}


int flowmgr_sort_participants(struct list *partl)
{
	return ENOSYS;
}


static void close_requests(struct flowmgr *fm)
{
	struct le *le = fm->rrl.head;

	while (le) {
		struct rr_resp *rr = le->data;
		le = le->next;

		if (rr->resph)
			rr->resph(499, rr, NULL, rr->arg);

		mem_deref(rr);
	}
}


static void fm_destructor(void *arg)
{
	struct flowmgr *fm = arg;

	info("flowmgr(%p): destructor\n", fm);

	//tmr_cancel(&fm->config.tmr);

	//flowmgr_config_stop(fm);

	close_requests(fm);

	list_unlink(&fm->le);
}


int flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
		  flowmgr_err_h *errh, void *arg)
{
	struct flowmgr *fm;
	int err=0;

	if (!fmp || !reqh) {
		return EINVAL;
	}

	fm = mem_zalloc(sizeof(*fm), fm_destructor);
	if (!fm)
		return ENOMEM;

	info("flowmgr(%p): alloc: (%s)\n", fm, avs_version_str());
	
	fm->reqh = reqh;
	fm->errh = errh;
	fm->sarg = arg;


	list_append(&fsys.fml, &fm->le, fm);
	if (msystem_is_started(fsys.msys)) {
		//fm->config.pending = true;
		//flowmgr_config_starth(fm, config_handler, fm);
	}

	// out:
	if (err) {
		mem_deref(fm);
	}
	else {
		*fmp = fm;
	}

	return err;
}


void flowmgr_set_media_handlers(struct flowmgr *fm, flowmgr_mcat_chg_h *cath,
			        flowmgr_volume_h *volh, void *arg)
{
}


void flowmgr_set_media_estab_handler(struct flowmgr *fm,
				     flowmgr_media_estab_h *mestabh,
				     void *arg)
{
}




void flowmgr_set_conf_pos_handler(struct flowmgr *fm,
				  flowmgr_conf_pos_h *conf_posh,
				  void *arg)
{
}


void flowmgr_set_video_handlers(struct flowmgr *fm, 
				flowmgr_video_state_change_h *state_change_h,
				flowmgr_render_frame_h *render_frame_h,
				flowmgr_video_size_h *size_h,
				void *arg)
{
	vie_set_video_handlers(state_change_h, render_frame_h, size_h, arg);
}


void flowmgr_set_sessid(struct flowmgr *fm,
			const char *convid, const char *sessid)
{
}


int  flowmgr_interruption(struct flowmgr *fm, const char *convid,
			  bool interrupted)
{
	return ENOSYS;
}


void flowmgr_enable_metrics(struct flowmgr *fm, bool metrics)
{
}


struct flowmgr *flowmgr_free(struct flowmgr *fm)
{
	return mem_deref(fm);
}


int flowmgr_process_event(bool *hp, struct flowmgr *fm,
			  const char *ctype, const char *content, size_t clen)
{
	warning("NOT IMPLEMENTED: flowmgr_process_event\n");
	return ENOSYS;
}


int flowmgr_has_active(struct flowmgr *fm, bool *has_active)
{
	return ENOSYS;
}


int flowmgr_has_media(struct flowmgr *fm, const char *convid,
		      bool *has_media)
{
	return ENOSYS;
}


struct flowmgr *flowmgr_rr_flowmgr(const struct rr_resp *rr)
{
	return rr ? rr->fm : NULL;
}


const char **flowmgr_events(int *nevs)
{
	if (nevs == NULL)
		return NULL;

	*nevs = 0;

	return NULL;
}


int flowmgr_wakeup(void)
{
	return msystem_push(fsys.msys, 0, NULL);	
}


bool flowmgr_can_send_video(struct flowmgr *fm, const char *convid)
{
	return false;
}


void flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state)
{
	struct call *call=0;

	if (!fm)
		return;
	

	if (!call) {
		warning("flowmgr(%p): set_video_send_state: conv %s not found\n",
			fm, convid ? convid : "NULL");
		return;
	}

	//call_set_video_send_active(call, state == FLOWMGR_VIDEO_SEND);
}


bool flowmgr_is_sending_video(struct flowmgr *fm,
			      const char *convid, const char *partid)
{
	return false;
}


void flowmgr_handle_frame(struct avs_vidframe *frame)
{
	if (frame) {
		vie_capture_router_handle_frame(frame);
	}
}


bool flowmgr_is_using_voe(void)
{
	return msystem_is_using_voe(fsys.msys);
}


void flowmgr_set_username_handler(struct flowmgr *fm,
				  flowmgr_username_h *usernameh, void *arg)
{
}


int flowmgr_is_ready(struct flowmgr *fm, bool *is_ready)
{
	if (!fm || !is_ready)
		return EINVAL;

	//*is_ready = fm->config.ready;
	*is_ready = true;
	
	return 0;
}


const char *flowmgr_get_username(struct flowmgr *fm, const char *userid)
{
	return "";
}


void flowmgr_set_self_userid(struct flowmgr *fm, const char *userid)
{
}


const char *flowmgr_get_self_userid(struct flowmgr *fm)
{
	return "";
}
	

void flowmgr_refresh_access_token(struct flowmgr *fm,
				  const char *token, const char *type)
{
	(void)token;
	(void)type;

	if (!fm)
		return;

	warning("flowmgr_refresh_access_token unused\n");


	//fm->config.pending = true;
	//flowmgr_config_starth(fm, config_handler, fm);
}

void flowmgr_set_audio_state_handler(struct flowmgr *fm,
			flowmgr_audio_state_change_h *state_change_h,
			void *arg)
{
	voe_set_audio_state_handler(state_change_h, arg);
}


struct msystem *flowmgr_msystem(void)
{
	return fsys.msys;
}
