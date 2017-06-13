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
#include <sys/time.h>

#include <re/re.h>

#include "avs.h"
#include "avs_version.h"
//#include "avs_voe.h"
#include "avs_vie.h"
//#include "avs_msystem.h"

#include "flowmgr.h"


/* Call signalling URL bases */

#define VOLUME_TIMEOUT 100 /* ms between volume updates */


static struct {
	struct list fml;
	struct list flowl;

	struct tmr vol_tmr;
	struct msystem *msys;
} fsys;


static const char *events[FLOWMGR_EVENT_MAX] = {
	"call.flow-add",
	"call.flow-delete",
	"call.flow-active",
	"call.remote-candidates-add",
	"call.remote-candidates-update",
	"call.remote-sdp"
};


static int process_event(bool *hp, struct flowmgr *fm,
			 const char *ctype, const char *content, size_t clen,
			 bool replayed);


const char *flowmgr_mediacat_name(enum flowmgr_mcat mcat)
{
	switch (mcat) {

	case FLOWMGR_MCAT_NORMAL: return "Normal";
	case FLOWMGR_MCAT_HOLD:   return "Hold";
	case FLOWMGR_MCAT_ACTIVE: return "Active";
	case FLOWMGR_MCAT_CALL:   return "Call";
	default: return "???";
	}
}


int flowmgr_append_convlog(struct flowmgr *fm, const char *convid,
			   const char *msg)
{
	struct call *call;

	call = dict_lookup(fm->calls, convid);

	info("flowmgr(%p): append_convlog: convid=%s(%p) msg=%s\n",
	     fm, convid, call, msg);

	return 0;
}


enum trace_type {
	TRACE_REQ  = 0,
	TRACE_RESP = 1,
	TRACE_WS   = 2
};


static void color_trace(enum trace_type type,
			int color, const char *fmt, ...)
{
#define WIDTH 128
	va_list ap;

	va_start(ap, fmt);

	char bufv[3][WIDTH] = {
		"----------------------------------------"
		"----------------------------------------"
		"----------------------------------------"
		"------>\n"
		,
		"<---------------------------------------"
		"----------------------------------------"
		"----------------------------------------"
		"-------\n"
		,
		"<- - - - - - - - - - - - - - - - - - - -"
		" - - - - - - - - - - - - - - - - - - - -"
		" - - - - - - - - - - - - - - - - - - - -"
		" - - - \n"
	};
	char *buf = bufv[type];
	char str[256];
	size_t len;
	size_t offset;
	ssize_t n = 0;

	re_snprintf(str, sizeof(str), "%v", fmt, &ap);
	len = min(WIDTH, strlen(str));

	offset = (WIDTH - len) / 2;

	memcpy(&buf[offset], str, len);

#ifndef ANDROID
	re_fprintf(stderr, "\x1b[%dm", color);
	n = write(STDERR_FILENO, buf, WIDTH);
	re_fprintf(stderr, "\x1b[;m");
#endif
	(void)n;

	va_end(ap);
#undef WIDTH
}


/* This is just a dummy handler fo waking up re_main() */


static void vol_tmr_handler(void *arg)
{
	struct le *le;
	bool using_voe = false;

	using_voe = msystem_is_using_voe(fsys.msys);

	LIST_FOREACH(&fsys.flowl, le) {
		struct flow *flow = le->data;

		flow_vol_handler(flow, using_voe);
	}

	if (fsys.flowl.head != NULL) {
		tmr_start(&fsys.vol_tmr, VOLUME_TIMEOUT,
			  vol_tmr_handler, NULL);
	}
}


void flowmgr_start_volume(void)
{
	if (!tmr_isrunning(&fsys.vol_tmr)) {
		tmr_start(&fsys.vol_tmr, VOLUME_TIMEOUT,
			  vol_tmr_handler, NULL);
	}
}


void flowmgr_cancel_volume(void)
{
	tmr_cancel(&fsys.vol_tmr);
}

struct list *flowmgr_flows(void)
{
	return &fsys.flowl;
}

int flowmgr_init(const char *msysname, const char *log_url,
		 int cert_type)
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

	err = msystem_get(&fsys.msys, msysname, cert_type, NULL);
	if (err) {
		warning("flowmgr: msystem_init failed: %m\n", err);
		goto out;
	}

	info("flowmgr: initialized -- %s [machine %H]\n",
	     avs_version_str(), sys_build_get, 0);

	if (log_url) {
		warning("flowmgr: init: log_url is deprecated\n");
	}

 out:
	if (err)
		fsys.msys = mem_deref(fsys.msys);

	return err;
}


static void config_handler(struct call_config *cfg, void *arg)
{
	struct flowmgr *fm = arg;
	struct le *le;

	fm->config.pending = false;
	fm->config.ready = true;

	msystem_set_call_config(fsys.msys, cfg);

	le = fm->postl.head;
	while(le) {
		struct call *call = le->data;
		
		le = le->next;

		call_postponed_flows(call);
		list_unlink(&call->post_le);
	}
}


int flowmgr_start(void)
{
	struct le *le;
	int err = 0;

	msystem_start(fsys.msys);
	
	LIST_FOREACH(&fsys.fml, le) {
		struct flowmgr *fm = le->data;

		fm->config.pending = true;
		err |= flowmgr_config_starth(fm, config_handler, fm);
	}

	return err;
}


struct call *flowmgr_call(struct flowmgr *fm, const char *convid)
{
	struct call *call;
	
	if (!fm)
		return NULL;

	call = dict_lookup(fm->calls, convid);

	return call;
}


void flowmgr_close(void)
{
	tmr_cancel(&fsys.vol_tmr);

	flowmgr_wakeup();
	marshal_close();
	// XXX What is the lifetime of msystem? 
	fsys.msys = mem_deref(fsys.msys);
}


void flowmgr_network_changed(struct flowmgr *fm)
{
	struct sa laddr;
	char ifname[64] = "";

	sa_init(&laddr, AF_INET);

	(void)net_rt_default_get(AF_INET, ifname, sizeof(ifname));
	(void)net_default_source_addr_get(AF_INET, &laddr);

	info("flowmgr(%p): network_changed (laddr %s|%J)\n",
	     fm, ifname, &laddr);

	if (!fm)
		return;
       
	/* Go through all the calls, and restart flows on them */
	dict_apply(fm->calls, call_restart_handler, fm);
}


void flowmgr_mcat_changed(struct flowmgr *fm, const char *convid,
			  enum flowmgr_mcat mcat)
{
	struct call *call;

	info("flowmgr(%p): mcat_changed: convid=%s mcat=%d\n",
	      fm, convid, mcat);

	call = dict_lookup(fm->calls, convid);

	call_mcat_changed(call, mcat);
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

	if (fm->trace) {
		color_trace(TRACE_REQ, 32, "%s %s", method, path);
	}
	if (fm->trace >= 2 && jobj) {

		re_fprintf(stderr, "\x1b[32m");
		jzon_dump(jobj);
		re_fprintf(stderr, "\x1b[;m");
	}
	
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
		info("flowmgr(%p): http_resp(%p): %d %s %b\n",
		     fm, rr, status, reason, content, clen);
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

	if (fm->trace >= 1) {

		if (status < 200 || status >= 300)
			color_trace(TRACE_RESP, 31, "%d %s", status, reason);
		else
			color_trace(TRACE_RESP, 32, "%d %s [%s]",
				    status, reason, ctype);
	}
	if (fm->trace >= 2 && content && clen) {
		re_fprintf(stderr, "\x1b[32m");
		jzon_dump(jobj);
		re_fprintf(stderr, "\x1b[;m");
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


static int flow_add(struct call *call, const char *flowid,
		    bool has_creator, bool is_creator,
		    const char *creator, struct json_object *jflow)
{
	struct flow *flow;
	const char *remote_user;
	bool active = false;
	int err;

	if (!msystem_is_initialized(fsys.msys)) {
		warning("flowmgr: mediasystem is not initialized!\n");
		return ENOSYS;
	}

	if (!call)
		return EINVAL;

	/* flow already exists? */
	flow = call_find_flow(call, flowid);
	if (flow)
		return 0;

	remote_user = jzon_str(jflow, "remote_user");
	if (!remote_user) {
		warning("flowmgr: flow_add: remote_user is missing\n");
	}
	if (has_creator) {
		if (creator == NULL)
			is_creator = true;
		else if (remote_user)
			is_creator = !streq(creator, remote_user);
	}

	jzon_bool(&active, jflow, "active");
	err = flow_alloc(&flow, call, flowid, remote_user,
			 is_creator, active);
	if (err) {
		warning("flowmgr(%p): flow_add: cannot allocate flow: %m\n",
			call_flowmgr(call), err);
		goto out;
	}

	flow_act_handler(flow, jflow);

 out:
	if (err)
		flow = mem_deref(flow);

	return err;
}


struct event {
	struct le le;
	char *ctype;
	struct mbuf *content;
};


static void event_replay(struct flowmgr *fm)
{
	struct le *le;
	struct event *ev;
	int max_num;

	if (!fm)
		return;

	max_num = list_count(&fm->eventq);

	info("flowmgr(%p): event replay (count=%u)\n",
	     fm, list_count(&fm->eventq));

	le = fm->eventq.head;
	while (le && max_num) {

		ev = le->data;
		le = le->next;

		--max_num;

		info("flowmgr(%p): eventq pop: ctype=%s\n", fm, ev->ctype);
		process_event(NULL, fm, ev->ctype,
			      (char*)ev->content->buf,
			      ev->content->end, true);

		mem_deref(ev);
	}
}


static int flows_add_list(struct flowmgr *fm, struct list *addl)
{
	struct le *le;
	int n = 0;
	int err = 0;

	LIST_FOREACH(addl, le) {
		struct flow_elem *flel = le->data;

		err = flow_add(flel->call, flel->flowid,
			       flel->has_creator, flel->is_creator,
			       flel->creator, flel->jflow);
		if (err)
			return err;
		++n;
	}

	if (n > 0)
		event_replay(fm);

	return err;
}


static int flows_del_list(struct list *dell)
{
	struct le *le;
	int err;

	LIST_FOREACH(dell, le) {
		struct flow_elem *flel = le->data;
		struct rr_resp *rr;

		err = rr_alloc(&rr, call_flowmgr(flel->call),
			       flel->call, call_ghost_flow_handler, flel);
		if (err)
			return err;

		flow_delete(flel->call, flel->flowid, rr, 0);
	}

	return 0;
}

static void flel_destructor(void *arg)
{
	struct flow_elem *flel = arg;

	mem_deref((void *)flel->creator);
}


static int add_flows(struct json_object *jobj, struct call *call,
		     bool is_creator)
{
	struct json_object *jflows;
	int n;
	int i, err = 0;
	struct list addl = LIST_INIT;
	bool has_creator = false;

	err = jzon_array(&jflows, jobj, "flows");
	if (err) {
		warning("flowmgr(%p): add_flows: no 'flows' array\n",
			call_flowmgr(call));
		return err;
	}

	n = json_object_array_length(jflows);

	/* get the first element in the 'flows' array */
	for (i = 0; i < n; ++i) {
		struct flow *flow;
		struct json_object *jflow;
		struct flow_elem *flel;
		const char *flowid;
		const char *sdp_step;
		const char *cid = NULL;
		char *creator = NULL;		
		bool active;

		jflow = json_object_array_get_idx(jflows, i);
		if (!jflow) {
			warning("flowmgr(%p): add_flow: flows[%d] missing\n",
				call_flowmgr(call), i);
			return ENOENT;
		}

		flowid = jzon_str(jflow, "id");

		/* Check if we have this flow already,
		 * i.e. added by call.flow-add
		 */
		flow = call_find_flow(call, flowid); 
		if (flow) {
			info("flowmgr(%p): add_flow: flow %s "
			     "already exists: flow=%p\n",
			     call_flowmgr(call), flowid, flow);
			continue;
		}

		sdp_step = jzon_str(jflow, "sdp_step");

#if 1
		err = jzon_bool(&active, jflow, "active");
		if (err) {
			warning("flowmgr(%p): add_flow: no active flag\n",
				call_flowmgr(call));
			return err;
		}
#endif

		/* The semantics of the creator attribute is:
		 * creator == NULL means that the POST has created the flow
		 * creator == user-id means that someone else has created
		 *
		 * Also keep in mind that the creator flag gives us info
		 * about where we are coming from i.e. flow-add event or
		 * POST for flows...
		 */
		err = jzon_is_null(jflow, "creator");
		if (err == 0)
			has_creator = true;
		else if (err == ENOENT)
			has_creator = false;
		else {
			has_creator = true;
			cid = jzon_str(jflow, "creator");
			if (cid)
				str_dup(&creator, cid);
			else
				creator = NULL;
		}
		
		flel = mem_zalloc(sizeof(*flel), flel_destructor);
		flel->call = call;
		flel->flowid = flowid;
		flel->has_creator = has_creator;
		flel->is_creator = is_creator;
		flel->creator = creator;
		flel->jflow = jflow;

		/* Check for ghost flows, defined as:
		 * sdp_step != "pending" and flow is not active
		 */
		if (sdp_step && !streq(sdp_step, "pending")) {
			if (active) {
				flel->is_creator = false;
				flel->has_creator = false;
				list_append(&addl, &flel->le, flel);
			}
			else {
				info("flowmgr(%p): ghost-flow "
				     "with idx=%u and sdp_step=%s deleting\n",
				     call_flowmgr(call), i, sdp_step);
				list_append(&call->ghostl, &flel->le, flel);
			}
		}
		else
			list_append(&addl, &flel->le, flel);
	}

	if (call->ghostl.head)
		flows_del_list(&call->ghostl);
	
	err = flows_add_list(call_flowmgr(call), &addl);
	list_flush(&addl);

	return err;
}


static void post_flows_resp(int status, struct rr_resp *rr,
			   struct json_object *jobj, void *arg)
{
	struct call *call = rr->call;
	struct flowmgr *fm = call_flowmgr(call);
	int err;	

	(void)arg;

	if (status < 200 || status >= 300) {
		warning("flowmgr(%p): post_flows_resp: failed: status = %d\n",
			fm, status);

		err = EPROTO;
		goto out;
	}

	err = add_flows(jobj, call, true);
	if (err) {
		warning("flowmgr(%p): add_flows() failed (%m)\n", fm, err);
	}

	info("flowmgr(%p): post flows -- %u flows\n",
	     fm, dict_count(call->flows));

	call_purge_users(call);
	
 out:
	if (err) {
		if (fm && fm->errh)
			fm->errh(err, call_convid(call), fm->sarg);
	}
}


static int flows_add_handler(struct call *call, struct json_object *jobj)
{
	struct flowmgr *fm = call_flowmgr(call);
	int err;

	err = add_flows(jobj, call, false);
	if (err)
		return err;

	info("flowmgr(%p): add flows -- %u flows\n",
	     fm, dict_count(call->flows));

	event_replay(fm);

	return err;
}


int flowmgr_post_flows(struct call *call)
{
	struct rr_resp *rr = NULL;
	struct flowmgr *fm = call_flowmgr(call);
	struct json_object *jsdp = NULL;
	char url[256];
	int err;

	if (!call)
		return EINVAL;

	/* Post for flows */
	err = rr_alloc(&rr, fm, call, post_flows_resp, NULL);
	if (err) {
		warning("flowmgr(%p): flowmgr_post_flows: "
			"cannot allocate rest response (%m)\n",
			fm, err);
		return err;
	}

	jsdp = call_userflow_sdp(call);

	re_snprintf(url, sizeof(url),
		    jsdp ? CREQ_POST_FLOWS : CREQ_FLOWS, call_convid(call));

	err = flowmgr_send_request(fm, call, rr, url, HTTP_POST,
				   jsdp ? CTYPE_JSON : NULL, jsdp);
	if (err) {
		warning("flowmgr(%p): flowmgr_post_flows: rest_req failed"
			" (%m)\n", fm, err);
		goto out;
	}

 out:
	mem_deref(jsdp);
	
	if (err && rr_isvalid(rr))
		mem_deref(rr);

	return err;
}

int flowmgr_acquire_flows(struct flowmgr *fm, const char *convid,
			  const char *sessid,
			  flowmgr_netq_h *qh, void *arg)
{
	struct call *call;
	bool callocated = false;
	int err;

	if (!fm || !convid)
		return EINVAL;

	info("flowmgr(%p): acquire_flows: convid=%s sessid=%s\n",
	     fm, convid, sessid);

	err = call_lookup_alloc(&call, &callocated, fm, convid);
	if (err)
		return err;
	
	call->start_ts = tmr_jiffies();

	if (sessid)
		call_set_sessid(call, sessid);

	call_set_active(call, true);

	if (fm->config.pending) {
		info("flowmgr(%p): %s: config pending wait with POST\n",
		     fm, __func__);
		list_append(&fm->postl, &call->post_le, call);
		err = 0;
		goto out;
	}
	
	err = call_post_flows(call);
	if (err) {
		warning("flowmgr: %s: call_post failed for call:%p(%d)\n",
			__func__, call, err);
		goto out;
	}

 out:
	if (err) {
		if (callocated)
			mem_deref(call);
	}

	return err;
}


/* note: username is optional */
int flowmgr_user_add(struct flowmgr *fm, const char *convid,
		     const char *userid, const char *username)
{
	struct call *call;
	struct userflow *uf;
	bool call_alloc = false;
	bool uf_alloc = false;
	int err;

	if (!fm || !convid || !userid)
		return EINVAL;

	info("flowmgr(%p): user_add: convid=%s userid=%s\n",
	     fm, convid, userid);

	err = call_lookup_alloc(&call, &call_alloc, fm, convid);
	if (err) {
		warning("flowmgr: user_add: lookup (%m)\n", err);
		goto out;
	}

	err = call_userflow_lookup_alloc(&uf, &uf_alloc,
					 call, userid, username);
	if (err) {
		warning("flowmgr: user_add: userflow (%m)\n", err);
		goto out;
	}

 out:
	if (err) {
		if (call_alloc)
			mem_deref(call);
		if (uf_alloc)
			mem_deref(uf);
	}
	
	return err;
}


size_t flowmgr_users_count(const struct flowmgr *fm, const char *convid)
{
	struct call *call;

	if (!fm || !convid)
		return 0;

	call = dict_lookup(fm->calls, convid);
	if (!call)
		return 0;

	return dict_count(call->users);
}


void flowmgr_set_active(struct flowmgr *fm, const char *convid, bool active)
{
#if 0		
	struct call *call;
	
	if (!fm || !convid)
		return;
	
	debug("flowmgr(%p): set_active: convid=%s\n", fm, convid);

	call = dict_lookup(fm->calls, convid);

	if (call == NULL) {
		info("flowmgr(%p): flowmgr_set_active: no call on: %s\n",
		     fm, convid);
		return;
	}

	call_set_active(call, active);
#endif
}


void flowmgr_release_flows(struct flowmgr *fm, const char *convid)
{
	struct call *call;

	if (!fm)
		return;

	call = dict_lookup(fm->calls, convid);

	info("flowmgr(%p): release_flows: convid=%s call=%p\n",
	     fm, convid, call);

	if (call == NULL) {
		info("flowmgr(%p): flowmgr_release_flows: no call on:"
			" %s\n", fm, convid);
		return;
	}

	info("* * * * * * FLOWMGR CALL SUMMARY: * * * * * *\n");
	info("%H\n", call_debug, call);
	info("* * * * * * * * * * * * * * * * * * * * * * *\n");

	call_set_active(call, false);
	
	if (fm->use_metrics)
		flowmgr_send_metrics(fm, convid, "complete");

	call_cancel(call);

	dict_remove(fm->calls, call_convid(call));
}


int flowmgr_sort_participants(struct list *partl)
{
	conf_pos_sort(partl);

	return 0;
}


static bool release_flows(char *key, void *val, void *arg)
{
	struct flowmgr *fm = arg;
	const char *convid = key;

	(void)val;

	flowmgr_release_flows(fm, convid);

	return false;
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

	tmr_cancel(&fm->config.tmr);

	flowmgr_config_stop(fm);

	close_requests(fm);

	if (fm->calls)
		dict_apply(fm->calls, release_flows, fm);

	mem_deref(fm->calls);

	list_flush(&fm->eventq);
	list_flush(&fm->postl);

	list_unlink(&fm->le);
}


int flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
		  flowmgr_err_h *errh, void *arg)
{
	struct flowmgr *fm;
	int err;

	if (!fmp || !reqh) {
		return EINVAL;
	}

	fm = mem_zalloc(sizeof(*fm), fm_destructor);
	if (!fm)
		return ENOMEM;

	info("flowmgr(%p): alloc: (%s)\n", fm, avs_version_str());
	
	err = dict_alloc(&fm->calls);
	if (err) {
		goto out;
	}

	fm->reqh = reqh;
	fm->errh = errh;
	fm->sarg = arg;

	/* Set these elsewhere, for more control... */
	flowmgr_enable_metrics(fm, false);

	list_append(&fsys.fml, &fm->le, fm);
	if (msystem_is_started(fsys.msys)) {
		fm->config.pending = true;
		flowmgr_config_starth(fm, config_handler, fm);
	}

 out:
	if (err) {
		mem_deref(fm);
	}
	else {
		*fmp = fm;
	}

	return err;
}


int flowmgr_set_event_handler(struct flowmgr *fm, flowmgr_event_h *evh,
			      void *arg)
{
	if (!fm)
		return EINVAL;

	fm->evh = evh;
	fm->evarg = arg;

	return 0;
}


void flowmgr_set_media_handlers(struct flowmgr *fm, flowmgr_mcat_chg_h *cath,
			        flowmgr_volume_h *volh, void *arg)
{
	if (!fm)
		return;

	fm->cath = cath;
	fm->volh = volh;
	fm->marg = arg;
}


void flowmgr_set_media_estab_handler(struct flowmgr *fm,
				     flowmgr_media_estab_h *mestabh,
				     void *arg)
{
	if (!fm)
		return;

	fm->mestabh = mestabh;
	fm->mestab_arg = arg;
}


void flowmgr_set_log_handlers(struct flowmgr *fm,
			      flowmgr_log_append_h *appendh,
			      flowmgr_log_upload_h *uploadh,
			      void *arg)
{
	if (!fm)
		return;

	fm->log.appendh = appendh;
	fm->log.uploadh = uploadh;
	fm->log.arg = arg;
}


void flowmgr_set_conf_pos_handler(struct flowmgr *fm,
				  flowmgr_conf_pos_h *conf_posh,
				  void *arg)
{
	if (!fm)
		return;

	fm->conf_posh = conf_posh;
	fm->conf_pos_arg = arg;
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
	struct call *call;
	bool created = false;
	int err = 0;

	if (!fm || !convid)
		return;

	info("flowmgr(%p): set_sessid: convid=%s sessid=%s\n",
	     fm, convid, sessid);
    
	call = dict_lookup(fm->calls, convid);
	if (!call) {
		if (!call) {
			err = call_alloc(&call, fm, convid);
			if (err)
				goto out;

			created = true;
		}
	}

	err = call_set_sessid(call, sessid);
    
 out:
	if (err) {
		if (created)
			mem_deref(call);
	}
}


int  flowmgr_interruption(struct flowmgr *fm, const char *convid,
			  bool interrupted)
{
	struct call *call;
	
	if (!fm)
		return EINVAL;

	call = dict_lookup(fm->calls, convid);
	if (!call)
		return ENOENT;

	call_interruption(call, interrupted);
	
	return 0;
}


void flowmgr_enable_trace(struct flowmgr *fm, int trace)
{
	if (!fm)
		return;

	fm->trace = trace;
}


void flowmgr_enable_metrics(struct flowmgr *fm, bool metrics)
{
	if (!fm)
		return;

	fm->use_metrics = metrics;
}


void flowmgr_enable_logging(struct flowmgr *fm, bool logging)
{
	if (!fm)
		return;

	warning("flowmgr: enable_logging is deprecated\n");
}


struct flowmgr *flowmgr_free(struct flowmgr *fm)
{
	return mem_deref(fm);
}


static void event_destructor(void *data)
{
	struct event *ev = data;

	list_unlink(&ev->le);
	mem_deref(ev->ctype);
	mem_deref(ev->content);
}


static void event_enqueue(struct flowmgr *fm,
			  const char *ctype, const char *content, size_t clen)
{
	struct event *ev;

	ev = mem_zalloc(sizeof(*ev), event_destructor);

	str_dup(&ev->ctype, ctype);
	ev->content = mbuf_alloc(clen);
	mbuf_write_mem(ev->content, (void *)content, clen);

	list_append(&fm->eventq, &ev->le, ev);

	return;
}

int flowmgr_process_event(bool *hp, struct flowmgr *fm,
			  const char *ctype, const char *content, size_t clen)
{
	return process_event(hp, fm, ctype, content, clen, false);
}


static int process_event(bool *hp, struct flowmgr *fm,
			 const char *ctype, const char *content, size_t clen,
			 bool replayed)
{
	struct json_object *jobj = NULL;
	const char *ev;
	const char *convid;
	const char *flowid;
	struct call *call = NULL;
	struct flow *flow = NULL;
	bool handled = true;
	bool created = false;
	enum flowmgr_event event;
	int err = 0;

	if (!fm || !ctype)
		return EINVAL;

	if (!streq(ctype, CTYPE_JSON)) {
		warning("flowmgr: process_event: ctype %s\n", ctype);
		return EPROTO;
	}

	err = jzon_decode(&jobj, content, clen);
	if (err) {
		warning("flowmgr(%p): process_event: JSON parse error"
			" [%zu bytes]\n", fm, clen);
		goto out;
	}

	ev = jzon_str(jobj, "type");

	convid = jzon_str(jobj, "conversation");
	flowid = jzon_str(jobj, "flow");

	if (!convid) {
		err = EPROTO;
		goto out;
	}

	/* check if we have a call for this conversation */
	call = dict_lookup(fm->calls, convid);

	info("flowmgr(%p): event(%zu bytes) %b\n", fm, clen, content, clen);

	if (streq(ev, events[FLOWMGR_EVENT_FLOW_ADD]))
		event = FLOWMGR_EVENT_FLOW_ADD;
	else if (streq(ev, events[FLOWMGR_EVENT_FLOW_DEL]))
		event = FLOWMGR_EVENT_FLOW_DEL;
	else if (streq(ev, events[FLOWMGR_EVENT_FLOW_ACT]))
		event = FLOWMGR_EVENT_FLOW_ACT;
	else if (streq(ev, events[FLOWMGR_EVENT_CAND_ADD]))
		event = FLOWMGR_EVENT_CAND_ADD;
	else if (streq(ev, events[FLOWMGR_EVENT_CAND_UPD]))
		event = FLOWMGR_EVENT_CAND_UPD;
	else if (streq(ev, events[FLOWMGR_EVENT_SDP]))
		event = FLOWMGR_EVENT_SDP;
	else {
		handled = false;
		goto out;
	}

	if (call && flowid) {
		flow = call_find_flow(call, flowid);

		if (!flow) {
			if (event == FLOWMGR_EVENT_FLOW_DEL) {
				info("flowmgr(%p): process_event: "
				     "flow '%s' already deleted\n",
				     fm, flowid);
			}
			else {
				info("flowmgr(%p): process_event(%s): "
				     "cannot find flow '%s'"
				     " in [%u entries] -- queuing..\n",
				     fm, ev, flowid,
				     call_count_flows(call));

				event_enqueue(fm, ctype, content, clen);

				err = 0;
				goto out;
			}
			err = EPROTO;
			goto out;
		}
	}

	if (handled) {

		if (fm->evh) {
			fm->evh(event, convid, flowid, jobj, fm->evarg);
		}

		if (fm->trace) {
			color_trace(TRACE_WS, 33, "%s", ev);
		}
		if (fm->trace >= 2) {
			re_fprintf(stderr, "\x1b[33m");
			jzon_dump(jobj);
			re_fprintf(stderr, "\x1b[;m");
		}
	}

	switch (event) {

	case FLOWMGR_EVENT_FLOW_ADD:
		if (!call) {
			err = call_alloc(&call, fm, convid);
			if (err)
				goto out;
			
			created = true;
		}
		err = flows_add_handler(call, jobj);
		break;

	case FLOWMGR_EVENT_FLOW_DEL:
		/* If we are here it means we have deleted a flow,
		 * if the flow is active we need to make sure we collect 
		 * the stats in order to avoid successfull calls to be
		 * marked as failed.
		 */
		if (call && dict_count(call->flows) == 1) {
			if (fm->use_metrics)
				flowmgr_send_metrics(fm, convid, "complete");
		}
		
		err = flow_del_handler(flow);
		if (call && dict_count(call->flows) == 0)
			dict_remove(fm->calls, convid);	
		break;

	case FLOWMGR_EVENT_FLOW_ACT:
		err = flow_act_handler(flow, jobj);
		break;

	case FLOWMGR_EVENT_CAND_ADD:
	case FLOWMGR_EVENT_CAND_UPD:
		err = flow_cand_handler(flow, jobj);
		break;

	case FLOWMGR_EVENT_SDP:
		err = flow_sdp_handler(flow, jobj, replayed);
		break;

	default:
		info("flowmgr(%p): event (%s) ignored\n", fm, ev);
		break;
	}

 out:
	mem_deref(jobj);

	if (err) {
		if (created)
			mem_deref(call);
	}
	else {
		if (hp)
			*hp = handled;
	}

	return err;
}


int flowmgr_has_active(struct flowmgr *fm, bool *has_active)
{
	struct call *call;

	if (!fm || !has_active)
		return EINVAL;

	call = dict_apply(fm->calls, call_active_handler, NULL);

	*has_active = call != NULL;

	return 0;
}


int flowmgr_has_media(struct flowmgr *fm, const char *convid,
		      bool *has_media)
{
	struct call *call;

	if (!fm || !has_media)
		return EINVAL;

	call = dict_lookup(fm->calls, convid);

	*has_media = call ? call->rtp_started : false;

	return 0;
	
}




struct flowmgr *flowmgr_rr_flowmgr(const struct rr_resp *rr)
{
	return rr ? rr->fm : NULL;
}


const char **flowmgr_events(int *nevs)
{
	if (nevs == NULL)
		return NULL;

	*nevs = FLOWMGR_EVENT_MAX;

	return events;
}


int flowmgr_send_metrics(struct flowmgr *fm, const char *convid,
			 const char *path)
{
	struct call *call;
	struct json_object *jobj;
	char url[256];
	bool handled;
	int err = 0;

	if (!fm || !convid)
		return EINVAL;

	call = dict_lookup(fm->calls, convid);
	if (!call)
		return ENOENT;

	if (dict_count(call->flows) == 0)
		return 0;

	jobj = json_object_new_object();
	if (!jobj)
		return ENOMEM;

	json_object_object_add(jobj, "version",
			       json_object_new_string(avs_version_str()));
	handled = call_stats_prepare(call, jobj);
	json_object_object_add(jobj, "success",
			       json_object_new_boolean(handled));

#if 0
	jzon_dump(jobj);
#endif
	if (!path) 
		re_snprintf(url, sizeof(url), CREQ_METRICS, call_convid(call));
	else {
		re_snprintf(url, sizeof(url), CREQ_METRICS"/%s",
			    call_convid(call), path);
	}

	err = flowmgr_send_request(fm, call, NULL,
				   url, HTTP_POST, CTYPE_JSON, jobj);
	if (err) {
		warning("flowmgr(%p): send_metrics: rest_req failed (%m)\n",
			fm, err);
		goto out;
	}

 out:
	mem_deref(jobj);

	return err;
}


int flowmgr_debug(struct re_printf *pf, const struct flowmgr *fm)
{
	const struct call_config *cfg;
	unsigned i;
	int err = 0;

	if (!fm)
		return 0;

	err |= re_hprintf(pf, "***** FLOWMGR *****\n");

	if (str_isset(fm->self_userid))
		err |= re_hprintf(pf, "self_userid: %s\n", fm->self_userid);

	cfg = &fm->config.cfg;

	err |= re_hprintf(pf, "iceservers: (%zu)\n", cfg->iceserverc);
	for (i=0; i<cfg->iceserverc; i++) {
		const struct zapi_ice_server *srv = &cfg->iceserverv[i];
		err |= re_hprintf(pf, "  %u: url=%s username=%s\n",
				  i, srv->url, srv->username);
	}

	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, "number of calls: %u\n", dict_count(fm->calls));
	dict_apply(fm->calls, call_debug_handler, pf);

	err |= re_hprintf(pf, "***** ******* *****\n");

	return err;
}


int flowmgr_wakeup(void)
{
	return msystem_push(fsys.msys, 0, NULL);	
}


bool flowmgr_can_send_video(struct flowmgr *fm, const char *convid)
{
	struct call *call;

	if (convid) {
		call = dict_lookup(fm->calls, convid);
	}
	else {
		call = dict_apply(fm->calls, call_active_handler, NULL);
	}

	if (!call) {
		warning("flowmgr(%p): can_send_video: conv %s not found\n",
			fm, convid ? convid : "NULL");
		return false;
	}

	return call_can_send_video(call);
}

void flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state)
{
	struct call *call;

	if (!fm)
		return;
	
	if (convid) {
		call = dict_lookup(fm->calls, convid);
	}
	else {
		call = dict_apply(fm->calls, call_active_handler, NULL);
	}

	if (!call) {
		warning("flowmgr(%p): set_video_send_state: conv %s not found\n",
			fm, convid ? convid : "NULL");
		return;
	}

	call_set_video_send_active(call, state == FLOWMGR_VIDEO_SEND);
}


bool flowmgr_is_sending_video(struct flowmgr *fm,
			      const char *convid, const char *partid)
{
	struct call *call;

	if (!fm)
		return false;

	if (convid) {
		call = dict_lookup(fm->calls, convid);
	}
	else {
		call = dict_apply(fm->calls, call_active_handler, NULL);
	}

	if (!call) {
		warning("flowmgr(%p): is_sending_video: conv %s not found\n",
			fm, convid ? convid : "NULL");
		return false;
	}

	return call_is_sending_video(call, partid);
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
	if (!fm)
		return;

	fm->username.nameh = usernameh;
	fm->username.arg = arg;
}


int flowmgr_is_ready(struct flowmgr *fm, bool *is_ready)
{
	if (!fm || !is_ready)
		return EINVAL;

	*is_ready = fm->config.ready;

	return 0;
}


const char *flowmgr_get_username(struct flowmgr *fm, const char *userid)
{
	if (!fm->username.nameh)
		return NULL;
	
	return fm->username.nameh(userid, fm->username.arg);
}


void flowmgr_set_self_userid(struct flowmgr *fm, const char *userid)
{
	if (!fm)
		return;

	info("flowmgr(%p): setting self_userid to: %s\n", fm, userid);
	
	str_ncpy(fm->self_userid, userid, sizeof(fm->self_userid));
}


const char *flowmgr_get_self_userid(struct flowmgr *fm)
{
	return fm ? fm->self_userid : NULL;
}
	

void flowmgr_refresh_access_token(struct flowmgr *fm,
				  const char *token, const char *type)
{
	if (!fm)
		return;

	info("flowmgr(%p): access_token: %s %s\n", fm, token, type);

	str_ncpy(fm->rest.token.access_token, token,
		 sizeof(fm->rest.token.access_token));
	str_ncpy(fm->rest.token.token_type, type,
		 sizeof(fm->rest.token.token_type));
	
	if (fm->rest.cli) {
		rest_client_set_token(fm->rest.cli, &fm->rest.token);
	}

	fm->config.pending = true;
	flowmgr_config_starth(fm, config_handler, fm);
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
