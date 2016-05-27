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
#define _POSIX_SOURCE 1
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <re/re.h>

#include "avs.h"

#include "flowmgr.h"


#define SDP_OFFER  "offer"
#define SDP_ANSWER "answer"
#define SDP_MAX_LEN 4096


static void mediaflow_estab_handler(const char *crypto, const char *codec,
				    const char *type, const struct sa *sa,
				    void *arg)
{
	struct userflow *uf = arg;

	if (!uf || !uf->flow)
		return;

	flow_mediaflow_estab(uf->flow, crypto, codec,
			     mediaflow_lcand_name(uf->mediaflow),
			     mediaflow_rcand_name(uf->mediaflow),
			     sa);
}


static void rtp_start_handler(bool started, bool video_started, void *arg)
{
	struct userflow *uf = arg;

	if (!uf || !uf->flow)
		return;

	flow_rtp_start(uf->flow, started, video_started);
}


static void mediaflow_close_handler(int err, void *arg)
{
	struct userflow *uf = arg;

	info("userflow(%p): mediaflow closed (%m)\n", uf, err);

	uf->mediaflow = mem_deref(uf->mediaflow);

	if (!uf->flow) {
		struct call *call;
		struct flowmgr *fm;

		warning("flowmgr: err_handler: no flow\n");

		call = uf->call;
		fm = call_flowmgr(call);

		if (fm->errh)
			fm->errh(err, call_convid(call), fm->sarg);


		return;
	}
	
	warning("flowmgr: err_handler: mediasystem failed. "
		"user=%s(%s) err='%m'\n", uf->userid, uf->name, err);

	flow_error(uf->flow, err);
}


#if 0
static void mediaflow_localcand_handler(const struct zapi_candidate *candv,
					size_t candc, void *arg)
{
	struct userflow *uf = arg;
	struct call *call = uf->call;
	struct flow *flow = uf->flow;
	struct json_object *jobj = NULL;
	struct json_object *jcands = NULL;
	struct rr_resp *rr;
	const char *flowid;
	char url[256];
	size_t i;
	int err;

	if (!call) {
		warning("flowmgr: local_ice_req: no call\n");
		return;
	}

	err = rr_alloc(&rr, call_flowmgr(call), call, flow_ice_resp, flow);
	if (err) {
		warning("flowmgr: local_ice_req: alloc rest response fail\n");
		goto out;
	}

	flowid = flow_flowid(flow);
	if (!flowid) {
		warning("flowmgr: localcand: no flowid\n");
		err = ENOENT;
		goto out;
	}

	if (re_snprintf(url, sizeof(url),
			CREQ_LCAND, call_convid(call), flowid) < 0) {
		err = ENOMEM;
		goto out;
	}

	jobj = json_object_new_object();
	jcands = json_object_new_array();
	{
		for (i = 0; i < candc; i++) {

			struct json_object *jcand;

			jcand = json_object_new_object();

			err = zapi_candidate_encode(jcand, &candv[i]);
			if (err)
				goto out;

			json_object_array_add(jcands, jcand);
		}
	}
	json_object_object_add(jobj, "candidates", jcands);

	err = flowmgr_send_request(call_flowmgr(call), call, rr,
				   url, HTTP_PUT, CTYPE_JSON, jobj);
	if (err) {
		warning("flowmgr: local_ice_req: send_request failed (%m)\n",
			err);
		goto out;
	}

 out:
	mem_deref(jobj);

	if (err) {
		warning("flowmgr: localcand_handler error (%m)\n", err);
		flow_error(uf->flow, err);
	}
}
#endif


static bool interface_handler(const char *ifname, const struct sa *sa,
			      void *arg)
{
	struct userflow *uf = arg;
	const char *bindif = msystem_get_interface();
	int err;

	/* Skip loopback and link-local addresses */
	if (sa_is_loopback(sa) || sa_is_linklocal(sa))
		return false;

	if (str_isset(bindif) && 0 != str_casecmp(bindif, ifname)) {
		info("flowmgr: interface '%s' skipped\n", ifname);
		return false;
	}

	info("flowmgr: adding local host interface to mf=%p: %s:%j\n",
	     uf->mediaflow, ifname, sa);

	err = mediaflow_add_local_host_candidate(uf->mediaflow, ifname, sa);
	if (err) {
		warning("flowmgr: userflow: failed to add local host candidate"
			" %s:%j (%m)\n", ifname, sa, err);
		return false;
	}

	++uf->num_if;

	return false;
}


static void userflow_set_signal_state(struct userflow *uf,
				      enum userflow_signal_state state)
{
	if (!uf)
		return;
	
	info("flowmgr: userflow(%p): %s->%s\n",
	     uf,
	     userflow_signal_state_name(uf->signal_state),
	     userflow_signal_state_name(state));

	uf->signal_state = state;
}


static void mediaflow_gather_handler(void *arg)
{
	struct userflow *uf = arg;
	int err;

	info("flowmgr: gather complete (async_offer=%d, async_answer=%d)\n",
	     uf->sdp.async_offer, uf->sdp.async_answer);

	if (uf->sdp.async_offer) {

		err = userflow_generate_offer(uf);
		if (err) {
			warning("flowmgr: gather: userflow_generate_offer"
				" failed (%m)\n", err);
		}

		uf->sdp.async_offer = false;
	}

	if (uf->sdp.async_answer) {

		err = mediaflow_generate_answer(uf->mediaflow,
						uf->sdp.sdp, uf->sdp.len);
		if (err) {
			warning("flowmgr: create: generate offer"
				" failed (%m)\n", err);
			err = EPROTO;
			goto out;
		}
		uf->sdp.ready = true;
		userflow_set_signal_state(uf, USERFLOW_SIGNAL_STATE_STABLE);

		switch (uf->state) {
		case USERFLOW_STATE_POST:
			call_check_and_post(uf->call);
			break;

		case USERFLOW_STATE_ANSWER:			
		case USERFLOW_STATE_RESTART:
			flow_local_sdp_req(uf->flow, "answer", uf->sdp.sdp);
			userflow_set_state(uf, USERFLOW_STATE_IDLE);
			break;

		default:
			break;
		}

		uf->sdp.async_answer = false;
	}

 out:
	return;
}


static int userflow_add_iceserver(struct userflow *uf,
				  const char *uristr,
				  const char *username, const char *password)
{
	struct stun_uri stun_uri;
	int err;

	if (!uf || !uristr)
		return EINVAL;

	err = stun_uri_decode(&stun_uri, uristr);
	if (err) {
		warning("flowmgr: add_iceserver: cannot decode URI (%s)\n",
			uristr);
		return err;
	}

	switch (stun_uri.scheme) {

	case STUN_SCHEME_STUN:
		err = mediaflow_gather_stun(uf->mediaflow, &stun_uri.addr);
		if (err)
			return err;
		break;

	case STUN_SCHEME_TURN:
		if (!username || !password) {
			warning("flowmgr: add_iceserver: username/password"
				" is required for TURN uri\n");
			return EINVAL;
		}

		switch (stun_uri.proto) {

		case IPPROTO_UDP:
			err = mediaflow_gather_turn(uf->mediaflow,
						    &stun_uri.addr,
						    username, password);
			if (err)
				return err;
			break;

		case IPPROTO_TCP:
			err = mediaflow_gather_turn_tcp(uf->mediaflow,
							&stun_uri.addr,
							username, password,
							stun_uri.secure);
			if (err)
				return err;
			break;

		default:
			warning("flowmgr: add_iceserver: "
				" protocol %d not supported\n", stun_uri.proto);
			return ENOTSUP;
		}

		break;

	default:
		warning("flowmgr: add_iceserver: unknown uri scheme (%s)\n",
			uristr);
		return EINVAL;
	}

	return 0;
}


int userflow_update_config(struct userflow *uf)
{
	struct call_config *call_config;
	int err = 0;
	
	/* optional iceservers */
	call_config = uf->call ? &uf->call->fm->config.cfg : NULL;
	if (call_config && call_config->iceserverc) {

		struct zapi_ice_server *srv;
		size_t i;

		for (i=0; i<call_config->iceserverc; i++) {

			srv = &call_config->iceserverv[i];

			err = userflow_add_iceserver(uf, srv->url,
						     srv->username,
						     srv->credential);
			if (err) {
				warning("flowmgr: failed to add iceserver"
					" (%m)\n", err);
				/* ignore error, keep going */
			}
		}

	}
	else {
		info("flowmgr: userflow_alloc_mediaflow: no iceservers\n");
	}

	return 0;
}


int userflow_alloc_mediaflow(struct userflow *uf)
{
	enum mediaflow_nat nat = MEDIAFLOW_TRICKLEICE_DUALSTACK;
	struct sa laddr;	
	int err;

	debug("userflow_alloc_mediaflow: uf=%p\n", uf);
	
	/*
	 * NOTE: v4 has presedence over v6 for now
	 */
	if (0 == net_default_source_addr_get(AF_INET, &laddr)) {
		info("flowmgr: local IPv4 addr %j\n", &laddr);
	}
	else if (0 == net_default_source_addr_get(AF_INET6, &laddr)) {
		info("flowmgr: local IPv6 addr %j\n", &laddr);
	}
	else if (msystem_get_loopback()) {

		sa_set_str(&laddr, "127.0.0.1", 0);
	}
	else {
		warning("flowmgr: no local addresses\n");
		err = EAFNOSUPPORT;
		goto out;
	}

	err = mediaflow_alloc(&uf->mediaflow,
			      msystem_dtls(),
			      msystem_aucodecl(),
			      &laddr, nat, CRYPTO_DTLS_SRTP, true,
			      NULL,
			      mediaflow_estab_handler, 
			      mediaflow_close_handler, uf);
	if (err) {
		warning("flowmgr: failed to alloc mediaflow (%m)\n", err);
		goto out;
	}

#if 1
	if (msystem_get_privacy()) {
		info("flowmgr: enable mediaflow privacy\n");
		mediaflow_enable_privacy(uf->mediaflow, true);
	}
#endif

	mediaflow_set_gather_handler(uf->mediaflow,
				     mediaflow_gather_handler);

	mediaflow_set_rtpstate_handler(uf->mediaflow, rtp_start_handler);

	info("flowmgr: adding video\n");

	// TODO: add a run-time option for video-call or not ?
	err = mediaflow_add_video(uf->mediaflow, msystem_vidcodecl());
	if (err) {
		warning("flowmgr: mediaflow add video failed (%m)\n", err);
		goto out;
	}

	/* populate all network interfaces */
	uf->num_if = 0;
	net_if_apply(interface_handler, uf);

	info("flowmgr: num interfaces added: %u\n", uf->num_if);

	if (uf->num_if == 0) {

		if (msystem_get_loopback()) {

			struct sa lo;

			sa_set_str(&lo, "127.0.0.1", 0);

			err = mediaflow_add_local_host_candidate(uf->mediaflow,
								 "lo0", &lo);
			if (err) {
				warning("flowmgr: userflow: failed "
					"to add local host candidate"
					" lo0:%j (%m)\n", &lo, err);
			}
		}
		else {
			warning("flowmgr: No interfaces added!\n");
		}
	}

	err = userflow_update_config(uf);
	if (err) {
		warning("userflow(%p): failed to update config\n", uf);
		goto out;
	}

 out:
	if (err)
		uf->mediaflow = mem_deref(uf->mediaflow);

	return err;
}


static void destructor(void *arg)
{
	struct userflow *uf = arg;

	debug("userflow(%p): dtor: flow=%p\n",
	      uf, uf->flow);
	
	if (uf->call && uf->userid)
		dict_remove(uf->call->users, uf->userid);

	if (uf->flow)
		uf->flow->userflow = NULL;

	mem_deref(uf->sdp.sdp);
	mem_deref(uf->mediaflow);
	mem_deref(uf->userid);
	mem_deref(uf->name);
}


int userflow_alloc(struct userflow **ufp,
		   struct call *call, const char *userid, const char *name)
{
	struct userflow *uf;
	int err;

	if (!ufp || !call || !userid)
		return EINVAL;

	uf = mem_zalloc(sizeof(*uf), destructor);
	if (!uf)
		return ENOMEM;

	debug("userflow_alloc(%p): userid:%s name:%s\n", uf, userid, name);

	uf->call = call;
	uf->sdp.len = SDP_MAX_LEN;
	uf->sdp.sdp = mem_zalloc(uf->sdp.len, NULL);
	err = str_dup(&uf->userid, userid);
	if (err)
		goto out;

	if (name) {
		err = str_dup(&uf->name, name);
		if (err)
			goto out;
	}

	err = userflow_alloc_mediaflow(uf);
	if (err)
		goto out;
	
	err = dict_add(call->users, userid, uf);	
	if (err) {
		warning("flowmgr: userflow: dict_add(%p) (%m)\n",
			call->users, err);
		goto out;
	}
	
	/* userflow now owned by dictionary */
	mem_deref(uf);

 out:
	if (err)
		mem_deref(uf);
	else
		*ufp = uf;

	return err;
}


int userflow_generate_offer(struct userflow *uf)
{
	int err;

	if (!uf)
		return EINVAL;

	info("flowmgr: userflow(%p): generate_offer: "
	     "signal_state: %s gathered=%d\n",
	     uf, userflow_signal_state_name(uf->signal_state),
	     mediaflow_is_gathered(uf->mediaflow));

	if (uf->signal_state != USERFLOW_SIGNAL_STATE_STABLE)
		return 0;

	/* ICE Gathering not ready ... wait ... */
	if (!mediaflow_is_gathered(uf->mediaflow)) {
		info("flowmgr: userflow(%p): offer: mediaflow not gathered "
		     ".. wait ..\n", uf);
		uf->sdp.async_offer = true;
		return 0;
	}
	
	uf->sdp.type = SDP_OFFER;
	err = mediaflow_generate_offer(uf->mediaflow,
				       uf->sdp.sdp, uf->sdp.len);
	if (err) {
		warning("flowmgr: userflow_generate_offer(%p):"
			" failed (%m)\n", uf, err);
		err = EPROTO;
		goto out;
	}
	userflow_set_signal_state(uf, USERFLOW_SIGNAL_STATE_HAVE_LOCAL_OFFER);
	uf->sdp.ready = true;
	
	switch (uf->state) {
	case USERFLOW_STATE_POST:
		call_check_and_post(uf->call);
		break;

	case USERFLOW_STATE_OFFER:
	case USERFLOW_STATE_RESTART:
		if (uf->flow) {
			flow_local_sdp_req(uf->flow,
					   uf->sdp.type, uf->sdp.sdp);
			userflow_set_state(uf, USERFLOW_STATE_IDLE);
		}
		break;

	default:
		break;
	}


 out:
	return err;
}


bool userflow_check_sdp_handler(char *key, void *val, void *arg)
{
	struct userflow *uf = val;
	struct call *call = arg;
	
	(void)key;
	(void)call;

	debug("%s: call=%p uf=%p(s:%s ss:%s) ready=%d\n",
	      __func__, call, uf,
	      userflow_state_name(uf->state),
	      userflow_signal_state_name(uf->signal_state),
	      uf->sdp.ready);

	if (uf->state != USERFLOW_STATE_POST)
		return false;
	
	return !uf->sdp.ready;
}


void userflow_release_mediaflow(struct userflow *uf)
{
	if (!uf)
		return;

	uf->mediaflow = mem_deref(uf->mediaflow);
}


struct mediaflow *userflow_mediaflow(struct userflow *uf)
{
	return uf ? uf->mediaflow : NULL;
}


void userflow_set_state(struct userflow *uf, enum userflow_state state)
{
	if (!uf)
		return;

	info("userflow: %p state: %s->%s\n",
	     uf, userflow_state_name(uf->state), userflow_state_name(state));

	if (uf->state == state)
		return;
	
	uf->state = state;

	switch(uf->state) {
	case USERFLOW_STATE_POST:
		call_check_and_post(uf->call);
		break;

	case USERFLOW_STATE_IDLE:
		uf->sdp.ready = false;
		break;

	case USERFLOW_STATE_ANSWER:
		uf->sdp.async_answer = true;
		break;

	default:
		break;
	}
}


bool userflow_debug_handler(char *key, void *val, void *arg)
{
	struct re_printf *pf = arg;
	struct userflow *uf = val;
	int err = 0;

	err |= re_hprintf(pf, "    <%p>  signal=(%s)  state=%8s  sdp='%6s'"
			  "  userid=%s  username=\"%s\"\n",
			  uf,
			  userflow_signal_state_name(uf->signal_state),
			  userflow_state_name(uf->state), uf->sdp.type,
			  uf->userid, uf->name);

	return err != 0;
}


const char *userflow_state_name(enum userflow_state st)
{
	switch (st) {

	case USERFLOW_STATE_IDLE:    return "Idle";
	case USERFLOW_STATE_POST:    return "Post";
	case USERFLOW_STATE_RESTART: return "Restart";
	case USERFLOW_STATE_ANSWER:  return "Answer";
	case USERFLOW_STATE_OFFER:   return "Offer";
	default: return "???";
	}
}

const char *userflow_signal_state_name(enum userflow_signal_state st)
{
	switch (st) {

	case USERFLOW_SIGNAL_STATE_STABLE:            return "Stable";
	case USERFLOW_SIGNAL_STATE_HAVE_LOCAL_OFFER:  return "Local offer";
	case USERFLOW_SIGNAL_STATE_HAVE_REMOTE_OFFER: return "Remote offer";
	default: return "???";
	}
}


bool userflow_sdp_isready(struct userflow *uf)
{
	return uf ? uf->sdp.ready : false;
}

void userflow_set_flow(struct userflow *uf, struct flow *flow)
{
	if (!uf)
		return;
	
	uf->flow = flow;
}


int userflow_accept(struct userflow *uf, const char *sdp)
{
	struct call *call;
	char *sdpa;
	size_t sdpa_len = 4096;
	struct mediaflow *mf;
	uint32_t nrefs;
	int err;

	if (!uf || !uf->flow)
		return EINVAL;

	mf = uf->mediaflow;
	call = flow_call(uf->flow);
			
	if (sdp) {
		/* Check signalling state here? */
		switch (uf->signal_state) {
		case USERFLOW_SIGNAL_STATE_HAVE_LOCAL_OFFER:		
			if (str_cmp(flowmgr_get_self_userid(call->fm),
				    uf->userid) > 0) {
				info("flowmgr: userflow: SDP conflict "
				     "detected. Winning! (remote=%s)\n",
				     uf->userid);
				
				return 0;
			}
			else {
				info("flowmgr: userflow: SDP conflict "
				     "detected. Losing! (remote=%s)\n",
				     uf->userid);
			
				mediaflow_sdpstate_reset(uf->mediaflow);
			}
			break;

		case USERFLOW_SIGNAL_STATE_HAVE_REMOTE_OFFER:
			warning("flowmgr: userflow: signal_state "
				"HAVE_REMOTE_OFFER\n");
			return 0;

		default:
			break;
		}
		
		userflow_set_signal_state(uf,
				  USERFLOW_SIGNAL_STATE_HAVE_REMOTE_OFFER);
		err = mediaflow_handle_offer(mf, sdp);
		if (err)
			goto out;
	}

	
	nrefs = mem_nrefs(call);

	info("flowmgr: userflow_accept: uf=%p flow=%p call=%p(nrefs=%d active=%d)\n",
	     uf, uf->flow, call, nrefs, call->active);
	
	if (!call)
		return 0;
	if (!call->active)
		return 0;

	if (mediaflow_is_gathered(mf)) {
		sdpa = mem_zalloc(sdpa_len, NULL);
		if (!sdpa)
			return ENOMEM;

		mediaflow_generate_answer(mf, sdpa, sdpa_len);
		uf->sdp.ready = true;
		
		flow_local_sdp_req(uf->flow, SDP_ANSWER, sdpa);
		
		if (uf->state == USERFLOW_STATE_POST) {
			call_check_and_post(uf->call);
			userflow_set_state(uf, USERFLOW_STATE_IDLE);
		}
				
		userflow_set_signal_state(uf, USERFLOW_SIGNAL_STATE_STABLE);
		
		mem_deref(sdpa);
	}
	else {
		info("flowmgr: userflow_accept(%p): mediaflow "
			  "not gathered .. wait ..\n", uf);
		uf->sdp.async_answer = true;
		userflow_set_state(uf, USERFLOW_STATE_ANSWER);
	}
	mediaflow_start_ice(mf);

 out:
	
	return err;
}


/* rsdp = SDP Answer */
int userflow_update(struct userflow *uf, const char *sdp)
{
	struct call *call;
	struct mediaflow *mf;	
	int err;

	if (!uf || !uf->flow || !sdp)
		return EINVAL;
	
	call = flow_call(uf->flow);
	if (!call || !call->active)
		return 0;

	if (uf->signal_state != USERFLOW_SIGNAL_STATE_HAVE_LOCAL_OFFER) {
		warning("userflow: update: %p wrong signal state: %d(%s)\n",
			uf, uf->signal_state,
			userflow_signal_state_name(uf->signal_state));
		return EALREADY;
	}
	
	mf = uf->mediaflow;
	err = mediaflow_handle_answer(mf, sdp);
	if (err) {
		warning("flowmgr: update: handle_answer failed (%m)\n", err);
		return err;
	}	

	mediaflow_start_ice(mf);
	
	userflow_set_signal_state(uf, USERFLOW_SIGNAL_STATE_STABLE);

	return 0;
}


enum userflow_signal_state userflow_signal_state(struct userflow *uf)
{
	return uf ? uf->signal_state : USERFLOW_SIGNAL_STATE_UNKNOWN;
}
