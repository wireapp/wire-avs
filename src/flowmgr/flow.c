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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>

#include <re/re.h>

#include "avs.h"

#include "avs_voe.h"
#include "avs_voe_stats.h"
#include "avs_vie.h"

#include "flowmgr.h"


struct cand {
	struct le le;
	char sdp[256];
	char mid[64];
	int idx;
};

typedef void (mflow_lsdp_h) (const char *type, const char *sdp, void *arg);

static void flow_estabh(struct flow *flow);


void flow_delete(struct call *call, const char *flowid,
		 struct rr_resp *rr, int err)
{
	char path[256];
	char reason[64] = "released";

	if (!call || !flowid)
		return;
	
	info("flowmgr(%p): call(%p): flow(%s): delete: err=%d\n",
	     call->fm, call, flowid, err);
	
	switch(err) {
	case ETIMEDOUT:
		str_ncpy(reason, "timeout", sizeof(reason));
		break;

	default:
		break;
	}

	snprintf(path, sizeof(path), CREQ_FLOWS"/%s?reason=%s",
		 call->convid, flowid, reason);

	flowmgr_send_request(call->fm, call, rr, path,
			     HTTP_DELETE, NULL, NULL);
}


static void delete_flow(struct flow *flow)
{
	if (!flow || !flow->call || !flow->call->fm)
		return;

	if (!flow->deleted) {
		flow_delete(flow->call, flow->flowid, NULL, flow->err);
		flow->deleted = true;
	}

	flow->est_st = FLOWMGR_ESTAB_NONE;	
	flow_estabh(flow);

	call_remove_flow(flow->call, flow->flowid);
}


static void release_mediaflow(struct flow *flow)
{
	msystem_cancel_volume();

	if (flow_is_active(flow))
		call_mestab(flow->call, false);

	userflow_release_mediaflow(flow->userflow);
	flow->est_st &= ~FLOWMGR_ESTAB_RTP;
}


static void flow_destructor(void *arg)
{
	struct flow *flow = arg;
	struct call *call = flow->call;

	info("flowmgr(%p): call(%p): flow(%p -- %s) destructor\n",
	     call->fm, call, flow, flow->flowid);

	flow->cp = mem_deref(flow->cp);
	call_remove_conf_part(call, flow);
	delete_flow(flow);

	list_unlink(&flow->le);
	release_mediaflow(flow);

	flow->flowid = mem_deref(flow->flowid);
	flow->remoteid = mem_deref(flow->remoteid);

	if (flow->userflow) {
		userflow_set_flow(flow->userflow, NULL);
		dict_remove(call->users, flow->userflow->userid);
		flow->userflow = mem_deref(flow->userflow);
	}

	list_flush(&flow->pendingl);
}


static const char *flow_estab_name(enum flowmgr_estab estab)
{
	if (estab & FLOWMGR_ESTAB_RTP)
		return "RTP";

	switch (estab) {

	case FLOWMGR_ESTAB_NONE:   return "None";
	case FLOWMGR_ESTAB_ICE:    return "Ice";
	case FLOWMGR_ESTAB_ACTIVE: return "Active";
	case FLOWMGR_ESTAB_MEDIA:  return "Media";
	default: return "???";
	}
}


static void stop_media(struct flow *flow)
{
	struct mediaflow *mf;

	if (!flow)
		return;

	mf = userflow_mediaflow(flow->userflow);

	if (call_mcat(flow->call) == FLOWMGR_MCAT_HOLD)
		mediaflow_hold_media(mf, true);
	else
		mediaflow_stop_media(mf);
}


static void flow_estabh(struct flow *flow)
{
	struct call *call = (struct call *)flow->call;
	struct flowmgr *fm;
	enum flowmgr_mcat mcat = FLOWMGR_MCAT_NORMAL;
	enum flowmgr_estab est_st;
	struct flow *best_flow = NULL;

	debug("flowmgr: flow_estabh: call=%p flow=%p[%s] est_st=%d\n",
	      call, flow, flow_flowid(flow), flow->est_st);

	if (!call)
		best_flow = flow;
	else {
		best_flow = call_best_flow(call);
		if (!best_flow)
			best_flow = flow;
	}

	est_st = best_flow->est_st;
	if ((est_st & FLOWMGR_ESTAB_MEDIA) == FLOWMGR_ESTAB_MEDIA) {
		mcat = FLOWMGR_MCAT_CALL;
	}	
	else if (est_st == FLOWMGR_ESTAB_ACTIVE) {
		mcat = FLOWMGR_MCAT_ACTIVE;
		stop_media(flow);
	}
	else if (est_st == FLOWMGR_ESTAB_ICE) {
		mcat = FLOWMGR_MCAT_HOLD;
		stop_media(flow);
	}
	else if ((est_st & FLOWMGR_ESTAB_RTP)
	      && (est_st & FLOWMGR_ESTAB_ACTIVE)) {
		mcat = call_mcat(call);
	}
	else {
		mcat = FLOWMGR_MCAT_NORMAL;
		stop_media(flow);
	}

	if (!call)
		return;

	fm = call->fm;

	info("flowmgr(%p): call(%p): flow(%p -- %s): est_st: %s mcat:%s->%s",
	     fm, call, flow, flow->flowid,
	     flow_estab_name(flow->est_st),
	     flowmgr_mediacat_name(call_mcat(call)),
	     flowmgr_mediacat_name(mcat));

	/* To properly support multi party flows we should check that
	 * all known flows are either in-active or have RTP
	 */
	if (est_st & FLOWMGR_ESTAB_RTP) {
		call_rtp_started(call, true);
	}

	info("flowmgr: flow_estabh: call.mcat:%s->%s\n",
	     flowmgr_mediacat_name(call_mcat(call)),
	     flowmgr_mediacat_name(mcat));
	
	if (call_mcat(call) == mcat)
		flow_update_media(flow);
	else {
		call_mcat_change(call, mcat);
	}
}


void flow_mediaflow_estab(struct flow *flow,
			  const char *crypto, const char *codec,
			  const char *ltype, const char *rtype,
			  const struct sa *sa)
{
	uint64_t now = tmr_jiffies();
	struct call *call = flow->call;


	info("flowmgr(%p): call(%p): flow(%p -- %s): mediaflow established "
	     "est_st=%d crypto=%s codec=%s ice=%s-%s.%J\n",
	     call ? call->fm : NULL, call, flow, flow->flowid,
	     flow->est_st, crypto, codec, ltype, rtype, sa);

	flow->estab = true;
	flow->stats.sa = *sa;
	flow->stats.estab_time = (int)(now - flow->startts);
	str_ncpy(flow->stats.ltype, ltype, sizeof(flow->stats.ltype));
	str_ncpy(flow->stats.rtype, rtype, sizeof(flow->stats.rtype));
	str_ncpy(flow->stats.crypto, crypto, sizeof(flow->stats.crypto));
	str_ncpy(flow->stats.codec, codec, sizeof(flow->stats.codec));

	flow->estabts = tmr_jiffies();
	flow->est_st |= FLOWMGR_ESTAB_ICE;

	flow_estabh(flow);
}


void flow_ice_resp(int status, struct rr_resp *rr,
		   struct json_object *jobj, void *arg)
{
	struct call *call = rr->call;
	struct flow *flow = arg;

	if (!call) {
		warning("flowmgr: ice_resp: no call\n");
		return;
	}
	
	if (!call_has_flow(call, flow)) {
		warning("flowmgr: ice_resp: no flow\n");
		return;
	}

	if (status < 200 || status >= 300) {
		warning("flowmgr: ice_resp: ice request failed status=%d\n",
			status);

		if (flow_has_ice(flow))
			info("ice_resp failed but ice already working :)\n");
		else
			flow_error(flow, EPROTO);
	}
}


void flow_rtp_start(struct flow *flow, bool started, bool video_started)
{
	if (!flow)
		return;

	if (started) {
		flow->est_st |= FLOWMGR_ESTAB_RTP;
		call_add_conf_part(flow->call, flow);
	}	
	else
		flow->est_st &= ~FLOWMGR_ESTAB_RTP;

	if (video_started) {
		call_video_rcvd(flow->call, video_started);
	}

	flow_estabh(flow);
	call_mestab_check(flow->call);
}




int flow_alloc(struct flow **flowp, struct call *call,
	       const char *flowid, const char *remoteid,
	       bool creator, bool active)
{
	struct userflow *uf;
	struct flow *flow;
	bool allocated = false;	
	char tag[32];
	int err;

	if (!call || !flowid || !remoteid)
		return EINVAL;

	flow = mem_zalloc(sizeof(*flow), flow_destructor);
	if (flow == NULL)
		return ENOMEM;

	info("flowmgr(%p): call(%p): flow(%p -- %s): alloc remoteid=%s "
	     "creator=%d active=%d\n",
	     call->fm, call, flow, flowid, remoteid, creator, active);
	
	flow->call = call;
	flow->creator = creator;

	err = str_dup(&flow->flowid, flowid);
	if (err)
		goto out;

	err = str_dup(&flow->remoteid, remoteid);
	if (err)
		goto out;

	err = call_userflow_lookup_alloc(&uf, &allocated,
					 call, remoteid, NULL);
	if (err)
		goto out;

	flow->userflow = mem_ref(uf);
	/* XXX temporary warning that userflow is already set */
	if (flow->userflow->flow) {
		warning("%s(%p): userflow:%p already set to flow:%p\n",
			__func__, flow, flow->userflow, flow->userflow->flow);
		userflow_set_flow(flow->userflow->flow->userflow, NULL);
	}
	userflow_set_flow(flow->userflow, flow);
	flow->startts = tmr_jiffies();	

	flow->ix = call_add_flow(call, flow->userflow, flow);

/* CALLING2.0 This doesn't work with eager flows. We will end up sending
 * RTP packets on non-active flows. Wait for lazy flows for this
 */	
#if CALLING2_0
	flow_activate(flow, call->active);
#endif
	if (flow->ix < 0) {
		err = ENOENT;

		warning("flowmgr: flow_alloc: cannot add to dictionary: "
			"[%p] flowid='%s' flow=%p (%m)\n",
			call->flows, flowid, flow, err);

		goto out;
	}

	list_append(msystem_flows(), &flow->le, flow);
	msystem_start_volume();
	
	/* Flow is now owned by the dictionary */
	mem_deref(flow);

	re_snprintf(tag, sizeof(tag), "%u", flow->ix);
	mediaflow_set_tag(userflow_mediaflow(flow->userflow), tag);
	if (active && allocated && creator) {
		/* If we are here, it means we got flows which we didn't have
		 * a user for, so we should send an offer...
		 */
		userflow_set_state(uf, USERFLOW_STATE_OFFER);
		userflow_generate_offer(uf);
	}

 out:
	if (err) {
		if (allocated)
			flow->userflow = mem_deref(flow->userflow);
		mem_deref(flow);
	}
	else if (flowp)
		*flowp = flow;

	return err;
}


bool flow_good_handler(char *key, void *val, void *arg)
{
	struct flow *flow = val;

	(void)key;
	(void)arg;

	return flow->err == 0;
}


bool flow_is_active(const struct flow *flow)
{
	return (flow->est_st & FLOWMGR_ESTAB_ACTIVE) == FLOWMGR_ESTAB_ACTIVE;
}


bool flow_count_active_handler(char *key, void *val, void *arg)
{
	uint32_t *cnt = arg;
	struct flow *flow = val;

	if (flow->est_st & FLOWMGR_ESTAB_ACTIVE)
		*cnt = (*cnt) + 1;

	return false;
}


bool flow_lookup_part_handler(char *key, void *val, void *arg)
{
	struct flow *flow = val;
	const char *partid = arg;

	(void)key;

	if (flow->remoteid && partid) {
		return streq(flow->remoteid, partid);
	}
	else {
		return false;
	}
}

bool flow_best_handler(char *key, void *val, void *arg)
{
	enum flowmgr_estab *est = arg;
	struct flow *flow = val;

	(void)key;

	if (flow->est_st <= *est)
		return false;
	else {
		*est = flow->est_st;
		return true;
	}	
}


static void flow_restart(struct flow *flow)
{
	if (!flow)
		return;

	call_remove_conf_part(flow->call, flow);
	release_mediaflow(flow);
	userflow_set_state(flow->userflow, USERFLOW_STATE_RESTART);
	userflow_alloc_mediaflow(flow->userflow);
}


bool flow_restart_handler(char *key, void *val, void *arg)
{
	struct flow *flow = val;

	(void)key;
	(void)arg;

	info("flowmgr: flow restart handler\n");

	flow_restart(flow);
	userflow_generate_offer(flow->userflow);
	
	return false;
}


static void volume_handler(struct flow *flow, float invol, float outvol)
{
	struct flowmgr *fm = call_flowmgr(flow->call);

	if (fm && fm->volh) {
		fm->volh(call_convid(flow->call),
			 flow->remoteid,
			 invol, outvol, fm->marg);
	}
}


void flow_update_media(struct flow *flow)
{
	struct call *call = flow->call;
	struct mediaflow *mf;
	int err;

	if (!call)
		return;
	
	if (call_cat_chg_pending(call))
		return;

	mf = userflow_mediaflow(flow->userflow);
	if (call_mcat(call) == FLOWMGR_MCAT_CALL) {
		if ((flow->est_st & FLOWMGR_ESTAB_MEDIA) ==
		    FLOWMGR_ESTAB_MEDIA) {
			flow_set_volh(flow, volume_handler);
			err = mediaflow_start_media(mf);			
			if (err) {
				warning("flowmgr: mediaflow start media"
					" failed (%m)\n", err);
			}
			return;
		}
	}

	flow_set_volh(flow, NULL);
	stop_media(flow);
}


int flow_interruption(struct flow *flow, bool interrupted)
{
	int err = 0;
	
	if (!flow)
		return EINVAL;

	if (interrupted)
		stop_media(flow);
	else {
		err = mediaflow_start_media(
				    userflow_mediaflow(flow->userflow));
	}

	return err;
}


int flow_activate(struct flow *flow, bool active)
{
	if (!flow)
		return EINVAL;

	info("flowmgr: flow activate: active: %d->%d userflow=%p\n",
	     flow->active, active, flow->userflow);


	if (flow->active == active) {
		info("flowmgr: flow activate: same active state, ignoring.\n");
		return 0;
	}

	flow->active = active;

	if (active) {
		struct flow *uff = flow->userflow->flow;

		if (flow->call && !flow->call->active) {
			info("flowmgr: %s: flow:%p activating call: %p\n",
			     __func__, flow, flow->call);

			call_set_active(flow->call, true);
		}

		if (uff && uff != flow) {
			warning("%s(%p): userflow:%p already set to flow:%p\n",
				__func__, flow, flow->userflow, uff);
			userflow_set_flow(uff->userflow, NULL);
		}
		userflow_set_flow(flow->userflow, flow);

		if (flow->creator) {
			userflow_set_state(flow->userflow,
					   USERFLOW_STATE_OFFER);
			userflow_generate_offer(flow->userflow);
		}
		
		flow->est_st |= FLOWMGR_ESTAB_ACTIVE;
	}
	else {
		flow->est_st &= ~FLOWMGR_ESTAB_ACTIVE;
		call_remove_conf_part(flow->call, flow);
		call_mestab_check(flow->call);
	}

	flow_estabh(flow);

	return 0;
}


/* Common entry-point for issuing FLOW-errors */
void flow_error(struct flow *flow, int err)
{
	struct call *call;
	struct flowmgr *fm;
	bool has_good;
	bool is_active;
	bool is_multiparty;

	if (!flow)
		return;

	call = flow->call;
	fm = call_flowmgr(call);

	flow->err = err;

	list_unlink(&flow->le);

	call_remove_conf_part(flow->call, flow);
	release_mediaflow(flow);
	
	has_good = call_has_good_flow(call);
	is_active = flow_is_active(flow);
	is_multiparty = call_is_multiparty(call);
	
	info("flowmgr(%p): call(%p): flow(%p -- %s): error: "
	     "has_good=%d multiparty=%d active=%d err=%d\n",
	     fm, call, flow, flow->flowid,
	     has_good, is_multiparty, is_active, flow->err);

	delete_flow(flow);
	
	/* Check to see if we still have any good flows,
	 * if not, or this was an active flow on a 1-1 call; 
	 * notify the sync engine
	 */	
	if (!has_good || (!is_multiparty && is_active)) {
		if (fm->errh)
			fm->errh(err, call_convid(call), fm->sarg);
	}
}


bool flow_active_handler(char *key, void *val, void *arg)
{
	struct flow *flow = val;

	return (flow->est_st & FLOWMGR_ESTAB_MEDIA) == FLOWMGR_ESTAB_MEDIA;
}


bool flow_deestablish_media(char *key, void *val, void *arg)
{
	struct flow *flow = val;

	(void)key;
	(void)arg;

	flow->est_st = FLOWMGR_ESTAB_NONE;
	
	flow_estabh(flow);

	return false;
}


int flow_debug(struct re_printf *pf, const struct flow *flow)
{
	int err = 0;

	err |= re_hprintf(pf, "ix=%2u: flowid=%s\n"
			      "           |%c%c%c%c| %s\n"
			      "           remote_user=%s"
			  ,
			  flow->ix,
			  flow->flowid,
			  flow->creator ? 'C' : ' ',
			  flow->deleted ? 'D' : ' ',
			  flow->active ? 'A' : ' ',
			  flow->estab ? 'E' : ' ',
			  flow_estab_name(flow->est_st),
			  flow->remoteid ? flow->remoteid : "unknown"
			  );

	if (flow->err)
		err |= re_hprintf(pf, " (error=%m)", flow->err);

	return err;
}


bool flow_debug_handler(char *key, void *val, void *arg)
{
	struct re_printf *pf = arg;
	struct flow *flow = val;
	int err = 0;

	err |= re_hprintf(pf, "    %H\n", flow_debug, flow);

	if (flow->userflow) {
		err |= re_hprintf(pf, "           name=\"%s\"\n",
				  flow->userflow->name);
		err |= re_hprintf(pf, "           mf=[%H]",
				  mediaflow_debug,
				  userflow_mediaflow(flow->userflow));
	}

	err |= re_hprintf(pf, "\n");

	return err != 0;
}


bool flow_stats_handler(char *key, void *val, void *arg)
{
	struct json_object *jobj = arg;
	struct flow *flow = val;
	struct mflow_stats *stats;
	uint64_t mtime;
	
	if (!flow->estab)
		return false;
	
	mtime = tmr_jiffies() - flow->estabts;

	stats = &flow->stats;
		
	json_object_object_add(jobj, "estab_time",
			       json_object_new_int(stats->estab_time));
	json_object_object_add(jobj, "local_candidate",
			       json_object_new_string(stats->ltype));
	json_object_object_add(jobj, "remote_candidate",
			       json_object_new_string(stats->rtype));
	json_object_object_add(jobj, "media_time",
			       json_object_new_int((int)mtime));
	json_object_object_add(jobj, "codec",
			       json_object_new_string(stats->codec));
	json_object_object_add(jobj, "crypto",
			       json_object_new_string(stats->crypto));

	return true;
}


int  flow_set_volh(struct flow *flow, mflow_volume_h *volh)
{
	if (!flow)
		return EINVAL;
	
	flow->volh = volh;

	return 0;
}


bool flow_has_ice(struct flow *flow)
{
	return flow ? (flow->est_st & FLOWMGR_ESTAB_ICE) == FLOWMGR_ESTAB_ICE
		    : false; 
}


struct call *flow_call(struct flow *flow)
{
	return flow ? flow->call : NULL;
}


void flow_vol_handler(struct flow *flow, bool using_voe)
{
	struct auenc_state *aes;
	struct audec_state *ads;
	struct mediaflow *mf;
	double invol = 0.0;
	double outvol = 0.0;
	int err = 0;

	if (!flow)
		return;

	if (!flow_is_active(flow))
		return;

	mf = userflow_mediaflow(flow->userflow);
	aes = mediaflow_encoder(mf);
	ads = mediaflow_decoder(mf);
	if (!aes || !ads)
		return;

	if (using_voe) {
		err = voe_invol(aes, &invol);
		err |= voe_outvol(ads, &outvol);
		if (err) {
			error("flowmgr: flow_vol_handler: volumes: (%m)\n",
			      err);
			return;
		}
		if (flow->volh) {
			flow->volh(flow, (float)invol, (float)outvol);
		}
	}
}


static void cand_destructor(void *arg)
{
	struct cand *cand = arg;

	list_unlink(&cand->le);
}


static int cand_add(struct flow *flow, struct json_object *jcand)
{
	const char *mid = jzon_str(jcand, "sdp_mid");
	const char *rcand = jzon_str(jcand, "sdp");
	int idx;
	int err;

	err = jzon_int(&idx, jcand, "sdp_mline_index");
	if (err)
		return err;

	debug("cand_add: mid=%s idx=%d sdp=%s\n", mid, idx, rcand);

	if (flow->got_sdp) {
		struct mediaflow *mf = userflow_mediaflow(flow->userflow);
		return mediaflow_add_rcand(mf, rcand, mid, idx);
	}
	else {
		struct cand *cand = mem_zalloc(sizeof(*cand), cand_destructor);
		if (!cand)
			return ENOMEM;

		str_ncpy(cand->sdp, rcand, sizeof(cand->sdp));
		str_ncpy(cand->mid, mid, sizeof(cand->mid));
		cand->idx = idx;

		list_append(&flow->pendingl, &cand->le, cand);
		return 0;
	}
}


int flow_cand_handler(struct flow *flow, struct json_object *jobj)
{
	struct json_object *jcands;
	int n;
	int i;
	int err = 0;

	if (!flow || !jobj) {
		return EINVAL;
	}

	err = jzon_array(&jcands, jobj, "candidates");
	if (err) {
		warning("flowmgr: flow_cand_handler: no 'candidates'\n");
		return err;
	}

	n = json_object_array_length(jcands);

	for (i = 0; i < n; ++i) {
		struct json_object *jcand;

		jcand = json_object_array_get_idx(jcands, i);
		if (!jcand) {
			warning("flowmgr: flow_cand_handler:"
				" cand[%d] is missing\n", i);
			continue;
		}

		err |= cand_add(flow, jcand);
	}

	return err;
}


int flow_act_handler(struct flow *flow, struct json_object *jobj)
{
	bool active;
	int err;
	struct flowmgr *fm;
	struct call *call;

	if (!flow || !jobj) {
		return EINVAL;
	}

	call = flow->call;
	fm = call->fm;
	
	err = jzon_bool(&active, jobj, "active");
	if (err) {
		warning("flowmgr: flow_act_handler: no active flag\n");
		return err;
	}

	info("flowmgr(%p): call(%p): flow(%p -- %s): active=%d\n",
	     fm, call, flow, flow->flowid, active);

	err = flow_activate(flow, active);

	return err;
}


int flow_del_handler(struct flow *flow)
{
	if (!flow)
		return EINVAL;

	flow->deleted = true;

	delete_flow(flow);

	return 0;
}


static void sdp_resp(int status, struct rr_resp *rr,
		     struct json_object *jobj, void *arg)
{
	struct flow *flow = arg;
	struct call *call = rr->call;

	debug("flowmgr: sdp_resp: status=%d call=%p\n", status, call);

	if (!call) {
		warning("flowmgr: sdp_resp: no call\n");
		return;
	}

	if (!call_has_flow(call, flow)) {
		warning("flowmgr: sdp_resp: no flow: %p\n", flow);
		return;
	}

	if (status < 200 || status >= 300) {
		warning("flowmgr: sdp_resp: sdp request failed status=%d\n",
			status);

		flow_error(flow, EPROTO);
	}
}


void flow_local_sdp_req(struct flow *flow, const char *type, const char *sdp)
{
	struct call *call = flow_call(flow);
	struct json_object *jobj = NULL;
	struct rr_resp *rr;
	char url[256];
	int err;

	if (!call) {
		warning("flowmgr: local_sdp_req: no call\n");
		return;
	}

	err = rr_alloc(&rr, call_flowmgr(call), call, sdp_resp, flow);
	if (err) {
		warning("flowmgr: local_sdp_req: rest response (%m)\n", err);
		goto out;
	}

	snprintf(url, sizeof(url),
		 CREQ_LSDP, call_convid(call), flow_flowid(flow));

	jobj = json_object_new_object();
	json_object_object_add(jobj, "type", json_object_new_string(type));
	json_object_object_add(jobj, "sdp", json_object_new_string(sdp));

	err = flowmgr_send_request(call_flowmgr(call), call, rr, url,
				   HTTP_PUT, CTYPE_JSON, jobj);
	if (err) {
		warning("flowmgr: local_sdp_req: send_request() (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(jobj);

	/* if an error happened here, we must inform the application */
	if (err) {
		flow_error(flow, err);
	}
}


int flow_sdp_handler(struct flow *flow, struct json_object *jobj, bool replayed)
{
	struct mediaflow *mf;
	const char *sdp;
	const char *state;
	bool strm_chg = false;
	bool isoffer;
	int err = 0;

	if (!flow || !jobj) {
		return EINVAL;
	}

	sdp   = jzon_str(jobj, "sdp");
	state = jzon_str(jobj, "state");
	isoffer = streq(state, "offer");

	
	mf = userflow_mediaflow(flow->userflow);
	debug("flow_sdp_handler(%p): state=%s uf=%p mf=%p\n",
	      flow, state, flow->userflow, mf);

	if (mf && mediaflow_sdp_is_complete(mf)) {
		if (replayed)
			return 0;

		strm_chg = strstr(sdp, "x-streamchange") != NULL;

		if (strm_chg) {
			info("flow_sdp_handler: x-streamchange\n");
			mediaflow_stop_media(mf);
			mediaflow_sdpstate_reset(mf);
			mediaflow_reset_media(mf);
		}
		else if (isoffer) {
			info("flow_sdp_handler: SDP re-offer detected.\n");
			flow_restart(flow);
		}
		else {
			warning("flow_sdp_handler: SDP already complete");
			return 0;
		}
	}

	if (isoffer) {
		err = userflow_accept(flow->userflow, sdp);
		if (strm_chg) {
			mediaflow_start_media(mf);
		}
	}
	else if (streq(state, "answer")) {
		err = userflow_update(flow->userflow, sdp);
	}
	else {
		warning("flowmgr: flow_sdp_handler: unknown state: %s\n",
			state);
		err = EINVAL;
	}

	flow->got_sdp = true;

	/* flush list of any pending ICE-candidates */
	if (!list_isempty(&flow->pendingl)) {

		struct le *le = list_head(&flow->pendingl);

		info("flowmgr: got SDP - adding %u pending candidates\n",
			  list_count(&flow->pendingl));

		while (le) {
			struct cand *cand = le->data;
			le = le->next;

			err |= mediaflow_add_rcand(
					   userflow_mediaflow(flow->userflow),
					   cand->sdp,
					   cand->mid, cand->idx);
			mem_deref(cand);
		}
	}

	return err;
}


const char *flow_flowid(struct flow *flow)
{
	return flow ? flow->flowid : NULL;
}


const char *flow_remoteid(struct flow *flow)
{
	return flow ? flow->remoteid : NULL;
}

bool flow_can_send_video(struct flow *flow)
{
	return mediaflow_has_video(userflow_mediaflow(flow->userflow));
}

bool flow_is_sending_video(struct flow *flow)
{
	return mediaflow_is_sending_video(userflow_mediaflow(flow->userflow));
}


void flow_set_video_send_active(struct flow *flow, bool video_active)
{
	mediaflow_set_video_send_active(userflow_mediaflow(flow->userflow),
					video_active);
}

struct userflow *flow_get_userflow(struct flow *flow)
{
	return flow ? flow->userflow : NULL;
}
