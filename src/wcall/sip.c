#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <avs_pstn.h>
#include "baresip.h"
#include "wcall.h"
#include "sip.h"

#define PIN_CODE_HDR "X-PIN-Code"

extern int wireaudio_set_handlers(const char *convid,
				  ausrc_read_h *rh,
				  auplay_write_h *wh,
				  void *arg);


struct {
	bool initialized;
	struct list instl;
	struct log log;
	struct sip *sip;
} g_sip = {
	.initialized = false,
	.instl = LIST_INIT,
	.log = {
		.le = LE_INIT,
		.h = wcall_ext_log,
	},

};

struct sip_instance {
	struct list wual;

	struct {
		wcall_sip_parts_h *h;
		void *arg;
	} parts;

	struct le le;
};


struct wsip_ua {
	struct calling_instance *inst;
	struct sip_instance *sip_inst;
	struct ua *ua;
	char *aor;
	bool first_reg;
	bool ready;

	/* User callbacks */
	wcall_sip_ready_h *readyh;
	wcall_sip_incoming_h *incomingh;
	wcall_sip_close_h *closeh;
	wcall_sip_err_h *errh;
	void *arg;

	struct list wsipl; /* List of calls on this UA */
	struct list convl; /* List of conv entries */

	char *userid;
	char *clientid;
	
	struct le le;
};

struct wsip_call {
	struct wsip_ua *wua;
	struct call *call;
	char *from;
	char *pin_code;
	char *convid;
	void *adm;

	struct userinfo *uinfo;
	struct conv_entry *ce;
	bool estab;

	struct le le;
};

struct conv_entry {
	char *convid;

	struct userlist *userl;

	struct le le;
};

struct pin_entry {
	char *callid;
	char *pin_code;

	struct le le;
};

static void adm_handler(const char *convid, void *adm, bool added, void *arg);


static int alloc_message(struct econn_message **msgp,
			 enum econn_msg type,
			 bool resp,
			 const char *src_userid,
			 const char *src_clientid)
{
	struct econn_message *msg = NULL;

	msg = econn_message_alloc();
	if (msg == NULL) {
		return ENOMEM;
	}

	str_ncpy(msg->src_userid, src_userid, ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, src_clientid, ECONN_ID_LEN);
	msg->msg_type = type;
	msg->resp = resp;
	msg->transient = false;

	str_ncpy(msg->dest_userid, "PSTN", ECONN_ID_LEN);
	str_ncpy(msg->dest_clientid, "PSTN", ECONN_ID_LEN);

	if (msgp)
		*msgp = msg;

	return 0;
}

static struct conv_entry *get_conv_entry(struct wsip_ua *wua,
					 const char *convid)
{
	struct conv_entry *ce;
	struct le *le;
	bool found = false;

	if (!wua)
		return NULL;

	for(le = wua->convl.head; le && !found; le = le->next) {
		ce = le->data;

		found = ce->convid == convid;
	}

	return found ? ce : NULL;
}

static int send_confpart_response(struct wsip_call *wsip)
{
	struct wsip_ua *wua;
	struct econn_message *msg = NULL;
	char *str = NULL;
	int err = 0;

	if (!wsip || !wsip->ce)
		return EINVAL;

	wua = wsip->wua;

	err = alloc_message(&msg, ECONN_CONF_PART, true,
			    wua->userid, wua->clientid);
	if (err) {
		warning("sip: send_confpart_response: failed to alloc "
			"message: %m\n", err);
		goto out;
	}

	err = userlist_get_partlist(wsip->ce->userl,
				    &msg->u.confpart.partl,
				    false,
				    true);
	if (err) {
		goto out;
	}

	wcall_send_msg(wua->inst, wsip->convid, msg, wua);
		       
 out:
	mem_deref(str);
	mem_deref(msg);

	return err;
}


static void inst_destructor(void *arg)
{
	struct sip_instance *sip_inst = arg;

	list_flush(&sip_inst->wual);
}

static struct wsip_ua *wua_lookup(struct sip_instance *sip_inst,
				  const char *aor, struct ua *ua)
{
	struct le *le;
	struct wsip_ua *wua;
	bool found = false;
	
	for(le = sip_inst->wual.head; le && !found; le = le->next) {
		wua = le->data;
		if (!wua)
			continue;

		if (aor)
			found = streq(wua->aor, aor);
		else if (ua)
			found = wua->ua == ua;
	}

	return found ? wua : NULL;
}

static struct wsip_ua *ua2wua(struct ua *ua)
{
	struct wsip_ua *wua = NULL;
	bool found = false;
	struct le *le;

	for(le = g_sip.instl.head; le && !found; le = le->next) {
		struct sip_instance *sip_inst = le->data;

		if (!sip_inst) {
			warning("sip: ua2wua: no sip_instance in list\n");
			continue;
		}

		wua = wua_lookup(sip_inst, NULL, ua);
		found = wua != NULL;
	}

	return found ? wua : NULL;
}

static void ce_destructor(void *arg)
{
	struct conv_entry *ce = arg;

	list_unlink(&ce->le);
	mem_deref(ce->convid);
	mem_deref(ce->userl);
}

static int answer_call(struct wsip_call *wsip)
{
	struct wsip_ua *wua;
	struct audio *au;
	int err;

	if (!(wsip && wsip->wua))
		return EINVAL;

	wua = wsip->wua;

	au = call_audio(wsip->call);
	if (au) {
		audio_set_devicename(au, wsip->convid, wsip->convid);
	}
	
	err = ua_answer(wua->ua, wsip->call);
	if (err) {
		warning("sip(%p): answer_call=%p answer failed: %m\n",
			wua->sip_inst, wsip->call, err);
		goto out;
	}

 out:
	return err;
}

static void wsip_destructor(void *arg)
{
	struct wsip_call *wsip = arg;

	info("wsip(%p): convid=%s destructor\n", wsip, wsip->convid);

	list_unlink(&wsip->le);

	if (wsip->convid) {
		adm_handler(wsip->convid, wsip->adm, false, wsip);
	}

	mem_deref(wsip->from);
	mem_deref(wsip->pin_code);
	if (wsip->uinfo) {
		list_unlink(&wsip->uinfo->le);
		mem_deref(wsip->uinfo);
	}
	mem_deref(wsip->convid);

	/* Do we have any more users in this conversation?
	 * If not, remove the conversation
	 */
	if (wsip->ce && userlist_get_count(wsip->ce->userl) == 0) {
		mem_deref(wsip->ce);
	}
}

static struct wsip_call *lookup_call(struct wsip_ua *wua, struct call *call)
{
	struct wsip_call *wsip;
	struct le *le;
	bool found = false;

	if (!wua || !call)
		return NULL;
	
	for(le = wua->wsipl.head; le && !found; le = le->next) {
		wsip = le->data;
		if (!wsip)
			continue;

		found = call == wsip->call;
	}

	return found ? wsip : NULL;
}

static void incoming_call(struct wsip_ua *wua, struct call *call)
{
	struct wsip_call *wsip = NULL;
	struct sip_msg *msg = call_sipmsg(call);
	struct pl peer_pl = PL_INIT;
	struct uri from_uri;
	char *pin_code = NULL;
	int err = 0;

	info("sip: incoming call: %p on wua: %p msg=%p\n", call, wua, msg);
	if (msg) {
		if (pl_strcmp(&msg->met, "INVITE") == 0) {
			const struct sip_hdr *x_pinh = sip_msg_xhdr(msg, PIN_CODE_HDR);
			if (!x_pinh) {
				warning("sip: no %s in INVITE\n", PIN_CODE_HDR);
			}
			else {
				pl_strdup(&pin_code, &x_pinh->val);
			}
		}
	}

	wsip = mem_zalloc(sizeof(*wsip), wsip_destructor);
	if (!wsip) {
		warning("sip: could not allocate wsip\n");
		return;
	}

	wsip->wua = wua;
	wsip->call = call;

	/* Extract the username from the peer URI */
	pl_set_str(&peer_pl, call_peeruri(call));
	err = uri_decode(&from_uri, &peer_pl);
	if (err) {
		warning("sip: could not parse peeruri: %r\n", &peer_pl);
	}
	else {
		pl_strdup(&wsip->from, &from_uri.user);
	}
	wsip->pin_code = pin_code;

	list_append(&wua->wsipl, &wsip->le, wsip);

	info("sip(%p): incoming call on wua=%p call=%p\n",
	     wua->sip_inst, wua, call);

	if (wua->incomingh) {
		wua->incomingh(wua, wsip,
			       wsip->from,
			       wsip->pin_code,
			       wua->arg);
	}
}

static void uinfo_destructor(void *arg)
{
	struct userinfo *uinfo = arg;

	list_unlink(&uinfo->le);
	
	mem_deref(uinfo->userid_real);
	mem_deref(uinfo->clientid_real);
	mem_deref(uinfo->userid_hash);
	mem_deref(uinfo->clientid_hash);
}

static int parts_json(char **jsonp,
		      const char *convid,
		      struct list *userl)
{
	struct json_object *tparts;
	struct json_object *jparts;
	struct le *le;
	int err = 0;
	
	tparts = jzon_alloc_object();
	if (!tparts)
		return ENOMEM;
	
	jzon_add_str(tparts, "convid", "%s", convid);

	/* Array of participants, may be empty */
	jparts = json_object_new_array();
	if (!jparts) {
		err = ENOMEM;
		goto out;
	}

	LIST_FOREACH(userl, le) {
		struct userinfo *uinfo = le->data;
		struct json_object *jpart;

		jpart = jzon_alloc_object();
		if (!jpart)
			continue;

		jzon_add_str(jpart, "userid", "%s", uinfo->userid_real);

		json_object_array_add(jparts, jpart);
	}

	json_object_object_add(tparts, "participants", jparts);
	jzon_encode(jsonp, tparts);

 out:
	mem_deref(tparts);

	return err;
}

static void update_parts(struct wsip_ua *wua, struct wsip_call *wsip,
			 bool remove)
{
	struct conv_entry *ce;
	int err = 0;

	ce = get_conv_entry(wua, wsip->convid);
	if (remove) {
		/* Has this call been added to the userlist,
		 * if it has it will be we need to remove it.
		 */
		if (wsip->uinfo) {
			list_unlink(&wsip->uinfo->le);
			wsip->uinfo = mem_deref(wsip->uinfo);
		}
	}
	else {
		struct userinfo *uinfo;

		if (!ce) {
			ce = mem_zalloc(sizeof(*ce), ce_destructor);
			if (!ce) {
				warning("sip: could nou allocate conv entry\n");
				return;
			}
			str_dup(&ce->convid, wsip->convid);
			err = userlist_alloc(&ce->userl,
					     wua->userid,
					     wua->clientid,
					     NULL,
					     NULL,
					     NULL,
					     NULL,
					     NULL,
					     ce);
			if (err) {
				warning("wua(%p): could not allocate "
					"userlist: %m\n",
					wua, err);
				return;
			}
		
			list_append(&wua->convl, &ce->le, ce);
		}
		wsip->ce = ce;
	
		uinfo = mem_zalloc(sizeof(*uinfo), uinfo_destructor);
		if (!uinfo) {
			warning("sip: establ_call: could not "
				"allocate userinfo\n");
			return;
		}

		str_dup(&uinfo->userid_real, wsip->from);
		str_dup(&uinfo->userid_hash, wsip->from);
		str_dup(&uinfo->clientid_real, "_");
		str_dup(&uinfo->clientid_hash, "_");
		uinfo->pstn = true;

		wsip->uinfo = uinfo;

		list_append(&wsip->ce->userl->users, &uinfo->le, uinfo);
	}

	send_confpart_response(wsip);

	if (ce && wua->sip_inst->parts.h) {
		char *pjson = NULL;

		err = parts_json(&pjson, wsip->convid, &ce->userl->users);
		if (!err) {
			wua->sip_inst->parts.h(wsip->convid,
				pjson,
				wua->sip_inst->parts.arg);
		}
	}
}

static void estab_call(struct wsip_ua *wua, struct call *call)
{
	struct wsip_call *wsip = lookup_call(wua, call);

	if (wsip->estab)
		return;

	wsip->estab = true;

	update_parts(wua, wsip, false);
}

static void close_call(struct wsip_ua *wua, struct call *call)
{
	struct wsip_call *wsip;

	wsip = lookup_call(wua, call);
	if (!wsip)
		return;

	wsip->estab = false;

	update_parts(wua, wsip, true);

	if (wua->closeh) {
		wua->closeh(wsip, wua->arg);
	}

	mem_deref(wsip);
}

#if 0
static int parse_pin(char **pin, const char *local_uri)
{
	static struct pl x_pin_code = PL("X-PIN-Code");
	struct uri parsed_uri;
	struct pl local_pl;
	struct pl pin_val;
	int err;

	pl_set_str(&local_pl, local_uri);
	err = uri_decode(&parsed_uri, &local_pl);
	if (err)
		return err;

#if 1
	info("sip: parsing URI-params=%r\n", &parsed_uri.params);
#endif

	err = uri_param_get(&parsed_uri.params, &x_pin_code, &pin_val);
	if (err)
		return err;

	return pl_strdup(pin, &pin_val);
}
#endif

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm,
			     void *arg)
{
	struct wsip_ua *wua = NULL;
	
	(void)prm;

#if 1
	info("sip: event: %d(%s) ua: %p call=%p prm=%s\n",
	     ev, uag_event_str(ev), ua, call, prm);
#endif

	wua = ua2wua(ua);
	if (!wua) {
		warning("sip: ua_event: no instance for ua=%p\n", ua);
		/* There is a quirk in baresip where ua_register is called in
		 * context of ua_alloc, so wua might not be ready yet,
		 * ensure to register again
		 */
		if (ev == UA_EVENT_REGISTER_OK) {
			ua_register(ua);
		}
		return;
	}

	switch(ev) {
	case UA_EVENT_REGISTER_OK:
		if (wua->ready)
			break;
		else {
			wua->ready = true;
			wua->first_reg = false;
			if (wua->readyh) {
				wua->readyh(wua, wua->arg);
			}
		}
		break;

	case UA_EVENT_REGISTER_FAIL:
		info("ua(%p): register failed ready=%d\n", wua, wua->ready);
		if (!wua->first_reg && !wua->ready)
			break;
		else {
			wua->ready = false;
			if (wua->errh) {
				wua->errh(wua, prm, wua->arg);
			}
		}
		break;

	case UA_EVENT_CALL_INCOMING:
		info("sip(%p): call=%p incoming\n", wua->sip_inst, call);
		incoming_call(wua, call);
		break;

	case UA_EVENT_CALL_RINGING:
		break;

	case UA_EVENT_CALL_PROGRESS:
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		info("sip(%p): call=%p established\n", wua->sip_inst, call);
		estab_call(wua, call);
		break;

	case UA_EVENT_CALL_CLOSED:
		info("sip(%p): call=%p closed\n", wua->sip_inst, call);
		close_call(wua, call);
		break;

	case UA_EVENT_CALL_DTMF_START:
		break;

	case UA_EVENT_CALL_DTMF_END:
		break;
		
	case UA_EVENT_CALL_RTCP:
		break;

	default:
		break;
	}
}

static void adm_rec_handler(void *sampv, size_t sampc, void *arg)
{
	struct wsip_call *wsip = arg;
	
	pstn_play_read(wsip->adm, (int16_t *)sampv, sampc);
}

static void adm_play_handler(const void *sampv, size_t sampc, void *arg)
{
	struct wsip_call *wsip = arg;
	
	pstn_rec_write(wsip->adm, (const int16_t *)sampv, sampc);
}


static void adm_handler(const char *convid, void *adm, bool added, void *arg)
{
	struct wsip_call *wsip = arg;

	info("sip(%p): adm_handler: convid=%s adm=%p %s on wsip=%p\n",
	     wsip->wua->sip_inst, convid, adm,
	     added ? "ADDED" : "REMOVED", wsip);

	wsip->adm = adm;

	if (added) {
		wireaudio_set_handlers(convid,
				       adm_play_handler, adm_rec_handler,
				       wsip);
	}
	else {
		wireaudio_set_handlers(convid, NULL, NULL, wsip);
	}
}

int wcall_i_sip_init(struct calling_instance *inst, const char *conf_path)
{
	struct sip_instance *sip_inst;
	int err = 0;

	if (g_sip.initialized)
		goto newinst;

	log_enable_debug(true);
	log_enable_info(true);
	log_enable_stdout(true);
	
	info("sip: initializing with conf_path=%s\n", conf_path);

	conf_path_set(conf_path);

	err = conf_configure();
	if (err) {
		warning("sip: failed to configure: %m\n", err);
		return err;
	}

	err = baresip_init(conf_config(), false);
	if (err) {
		warning("sip: init: failed to initialize baresip: %m\n", err);
		return err;
	}

	info("sip: baresip initialized\n", conf_path);

	err = conf_modules();
	if (err) {
		warning("sip: init: failed to load modules: %m\n", err);
		return err;
	}
	
	err = uag_event_register(ua_event_handler, NULL);
	if (err) {
		warning("sip: init: failed to register ua event handler: %m\n", err);
		return err;
	}
	info("sip: init: event handler registered\n");


	info("sip: init: initializing UA\n");
	err = ua_init("wire-jbp", true, true, false, false);
	if (err) {
		warning("sip: failed to init UA\n");
		return err;
	}

	g_sip.initialized = true;

 newinst:
	sip_inst = mem_zalloc(sizeof(*sip_inst), inst_destructor);
	if (!sip_inst) {
		warning("sip: init: failed to allocate instance: %m\n", err);
		return ENOMEM; 
	}

	err = wcall_register_sip_instance(inst, sip_inst);
	if (err) {
		warning("sip(%p): init: registering SIP instance failed: %m",
			sip_inst, err);
	}
	
	info("sip(%p): init: added to inst=%p\n", sip_inst, inst);
	list_append(&g_sip.instl, &sip_inst->le, sip_inst);
	
	return 0;
}

int wcall_i_sip_close(struct calling_instance *inst)
{
	struct le *le;
	struct sip_instance *sip_inst;
	bool found = false;
	size_t n;

	info("sip: close: inst=%p\n", inst);
	
	if (!g_sip.initialized) {
		warning("sip: close: not initialized\n");
		return ENOSYS;
	}

	for(le = g_sip.instl.head; le && !found; le = le->next) {
		sip_inst = le->data;
		if (!sip_inst)
			continue;

		found = sip_inst == wcall_get_sip_instance(inst);
	}
	if (found) {
		list_unlink(&sip_inst->le);
	}

	n = list_count(&g_sip.instl);
	info("sip: close: closed inst=%p n=%zu\n", inst, n);
	
	if (n == 0) {
		info("sip: close: no active instances left, closing\n");
		baresip_close();
		g_sip.initialized = false;
	}

	return 0;
}

static void wua_destructor(void *arg)
{
	struct wsip_ua *wua = arg;

	mem_deref(wua->aor);

	list_unlink(&wua->le);

	mem_deref(wua->userid);
	mem_deref(wua->clientid);

	mem_deref(wua->ua);

	list_flush(&wua->convl);
	list_flush(&wua->wsipl);
}

int wcall_i_sip_create(struct calling_instance *inst,
		       const char *aor,
		       wcall_sip_ready_h *readyh,
		       wcall_sip_incoming_h *incomingh,
		       wcall_sip_close_h *closeh,
		       wcall_sip_parts_h *partsh,
		       wcall_sip_err_h *errh,
		       void *arg)
{
	struct sip_instance *sip_inst;
	struct wsip_ua *wua;
	char mod_aor[1024];
	int err;

	info("sip: create: aor=%s\n", aor);

	sip_inst = wcall_get_sip_instance(inst);
	if (!sip_inst) {
		warning("sip: create: no SIP instance for: %p\n", inst);
		return ENOSYS;
	}

	sip_inst->parts.h = partsh;
	sip_inst->parts.arg = arg;
	
	wua = mem_zalloc(sizeof(*wua), wua_destructor);
	if (!wua)
		return ENOMEM;

	wua->inst = inst;
	wua->sip_inst = sip_inst;
	wua->first_reg = true;
	wua->readyh = readyh;
	wua->incomingh = incomingh;
	wua->closeh = closeh;
	wua->errh = errh;
	wua->arg = arg;

	err = str_dup(&wua->aor, aor);
	if (err) {
		warning("sip: could not allocate aor string\n");
		goto out;
	}

	str_dup(&wua->userid, wcall_get_userid(inst));
	str_dup(&wua->clientid, wcall_get_clientid(inst));

	//re_snprintf(mod_aor, sizeof(mod_aor), "%s;natpinhole=yes;", aor);
	re_snprintf(mod_aor, sizeof(mod_aor), "%s;", aor);
	info("sip(%p): create: allocating UA with aor=%s\n",
	     sip_inst, mod_aor);
	err = ua_alloc(&wua->ua, mod_aor);
	if (err) {
		warning("sip(%p): create: could not allocate ua\n", sip_inst);
		goto out;
	}

 out:
	if (err) {
		mem_deref(wua);
	}
	else {
		list_append(&sip_inst->wual, &wua->le, wua);
	}

	return err;
}


int wcall_i_sip_destroy(struct calling_instance *inst,
			const char *aor)
{
	struct sip_instance *sip_inst;
	struct wsip_ua *wua;

	sip_inst = wcall_get_sip_instance(inst);
	if (!sip_inst) {
		warning("sip: destroy: could not get SIP instance\n");
		return ENOSYS;
	}

	wua = wua_lookup(sip_inst, aor, NULL);
	if (!wua) {
		warning("sip(%p): destroy: could not find wsip for aor=%s\n",
			sip_inst, aor);
		return EINVAL;
	}

	info("sip(%p): destroy: aor=%s wua=%p ua=%[\n", sip_inst, aor, wua, wua->ua);

	ua_unregister(wua->ua);
	
	mem_deref(wua);

	return 0;
}

int wcall_i_sip_answer(struct calling_instance *inst,
		       struct wsip_call *wsip,
		       const char *convid)
{
	struct wsip_ua *wua;
	int err = 0;

	if (!wsip || !convid) {
		warning("sip(%p): answer: invalid wsip=%p convid=%p\n",
			inst, wsip, convid);
		return EINVAL;
	}

	wua = wsip->wua;
	info("sip(%p): answer: on wua=%p wsip=%p convid=%s\n",
	     inst, wua, wsip, convid);

	/* Assign convid to this call */
	str_dup(&wsip->convid, convid);

	err = pstn_adm_handler_register(adm_handler, wsip);
	if (err) {
		warning("sip(%p): answer: could not register handler\n", inst);
		goto out;
	}
	/* If any adms already exist, the adm_handler will be called by
	 * the register function.
	 */

	err = answer_call(wsip);

 out:
	return err;
}

void wcall_i_sip_hangup(struct calling_instance *inst,
			struct wsip_call *wsip, int code, const char *status)
{
	(void)inst;

	info("wcall(%p): sip_hangup: wsip=%p code=%d status=%s\n", inst, wsip, code, status);

	if (!(wsip && wsip->wua))
		return;

	ua_hangup(wsip->wua->ua, wsip->call, code, status);
}
