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
	char *mod_aor;
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
	char *convid;

	/* Wildcard unregister (Contact: *) sent before the UA is
	 * allocated, enabled with regpurge=yes in the AOR
	 */
	struct {
		struct account *acc;
		struct sip_dialog *dlg;
		struct sip_auth *auth;
		struct sip_request *req;
		struct sip_loopstate ls;
	} purge;

	struct le le;
};

struct wsip_call {
	struct wsip_ua *wua;
	struct call *call;
	char *from;
	char *pin_code;
	char *convid;

	struct userinfo *uinfo;
	struct conv_entry *ce;
	bool estab;

	bool adm_registered;
	
	struct le le;
};

/* One per conversation, referenced by each wsip_call in it.
 * wua->convl only indexes them, it does not own them.
 */
struct conv_entry {
	char *convid;

	/* Short key for the conversation in wireaudio, as a (federated)
	 * convid may not fit baresip's 64 byte audio device name.
	 * The entry's address, unique while the conversation exists.
	 */
	char key[24];

	struct userlist *userl;

	uint32_t leg_id;  /* last PSTN leg id handed out */

	struct le le;
};

struct pin_entry {
	char *callid;
	char *pin_code;

	struct le le;
};

static void adm_handler(void *adm, bool added, void *arg);


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

		found = streq(ce->convid, convid);
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

/* Every call holds a reference to its conversation's entry,
 * allocated by the first call answered in the conversation.
 */
static int attach_conv_entry(struct wsip_call *wsip)
{
	struct wsip_ua *wua = wsip->wua;
	struct conv_entry *ce;
	int err;

	if (wsip->ce)
		return 0;

	ce = get_conv_entry(wua, wsip->convid);
	if (ce) {
		wsip->ce = mem_ref(ce);
		return 0;
	}

	ce = mem_zalloc(sizeof(*ce), ce_destructor);
	if (!ce)
		return ENOMEM;

	re_snprintf(ce->key, sizeof(ce->key), "%p", ce);

	err = str_dup(&ce->convid, wsip->convid);
	if (!err) {
		err = userlist_alloc(&ce->userl,
				     wua->userid,
				     wua->clientid,
				     NULL,
				     NULL,
				     NULL,
				     NULL,
				     NULL,
				     ce);
	}
	if (err) {
		warning("sip(%p): could not allocate conv entry: %m\n",
			wua->sip_inst, err);
		mem_deref(ce);
		return err;
	}

	info("sip(%p): conv entry key=%s for convid=%s\n",
	     wua->sip_inst, ce->key, ce->convid);

	/* Owned by wsip, released in its destructor */
	wsip->ce = ce;
	list_append(&wua->convl, &ce->le, ce);

	return 0;
}

static int answer_call(struct wsip_call *wsip)
{
	struct wsip_ua *wua;
	struct audio *au;
	int err;

	if (!(wsip && wsip->wua && wsip->ce))
		return EINVAL;

	wua = wsip->wua;

	au = call_audio(wsip->call);
	if (au) {
		char devname[64];

		++wsip->ce->leg_id;

		/* "<conv key>/<leg>": wireaudio uses the conv key to find
		 * the conversation's mixer, and the full name to pair this
		 * call's audio source and player into one mix-minus leg.
		 * Must fit baresip's 64 byte device name, which the
		 * convid itself may not.
		 */
		if (re_snprintf(devname, sizeof(devname), "%s/%x",
				wsip->ce->key, wsip->ce->leg_id) < 0) {
			err = EOVERFLOW;
			goto out;
		}
		audio_set_devicename(au, devname, devname);
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

	/* The pstn handler is registered on answer, regardless of
	 * whether an adm ever showed up, so always unregister it
	 * to avoid a dangling wsip pointer.
	 */
	pstn_adm_handler_unregister(adm_handler, wsip);

	/* Release this call's reference on the wireaudio handlers */
	adm_handler(NULL, false, wsip);

	mem_deref(wsip->from);
	mem_deref(wsip->pin_code);
	if (wsip->uinfo) {
		list_unlink(&wsip->uinfo->le);
		mem_deref(wsip->uinfo);
	}
	mem_deref(wsip->convid);

	/* The conversation goes away with its last call */
	mem_deref(wsip->ce);
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
		mem_deref(pin_code);
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

		/* Set in attach_conv_entry, which every established call
		 * went through
		 */
		if (!wsip->ce) {
			warning("sip: update_parts: wsip=%p has no "
				"conv entry\n", wsip);
			return;
		}

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

	ce = wsip->ce;

	send_confpart_response(wsip);

	if (ce && wua->sip_inst->parts.h) {
		char *pjson = NULL;

		err = parts_json(&pjson, wsip->convid, &ce->userl->users);
		if (!err) {
			wua->sip_inst->parts.h(wsip->convid,
				pjson,
				wua->sip_inst->parts.arg);
		}
		mem_deref(pjson);
	}
}

static void estab_call(struct wsip_ua *wua, struct call *call)
{
	struct wsip_call *wsip = lookup_call(wua, call);

	if (!wsip) {
		warning("sip: estab_call: no wsip for call=%p\n", call);
		return;
	}

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

/* The handler arg is the adm of the conversation the wireaudio
 * device belongs to, so each conversation uses its own adm.
 */
static void adm_rec_handler(void *sampv, size_t sampc, void *arg)
{
	pstn_play_read(arg, (int16_t *)sampv, sampc);
}

static void adm_play_handler(const void *sampv, size_t sampc, void *arg)
{
	pstn_rec_write(arg, (const int16_t *)sampv, sampc);
}

static void adm_handler(void *adm, bool added, void *arg)
{
	struct wsip_ua *wua;
	struct wsip_call *wsip = arg;

	/* wireaudio knows the conversation by its conv entry key */
	if (!wsip || !wsip->wua || !wsip->ce)
		return;

	wua = wsip->wua;
	info("sip(%p): adm_handler: convid=%s key=%s adm=%p %s on wsip=%p\n",
	     wua->sip_inst, wsip->convid, wsip->ce->key, adm,
	     added ? "ADDED" : "REMOVED", wsip);

	if (added) {
		if (!wsip->adm_registered) {
			wireaudio_set_handlers(wsip->ce->key,
					       adm_play_handler, adm_rec_handler,
					       adm);
			wsip->adm_registered = true;
		}
	}
	else if (wsip->adm_registered) {
		wireaudio_set_handlers(wsip->ce->key, NULL, NULL, NULL);
		wsip->adm_registered = false;
	}
}

int wcall_i_sip_init(struct calling_instance *inst, const char *conf_path)
{
	struct sip_instance *sip_inst;
	int err = 0;

	if (wcall_get_sip_instance(inst)) {
		warning("sip: init: inst=%p already has a SIP instance\n",
			inst);
		return EALREADY;
	}

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
		warning("sip(%p): init: registering SIP instance failed: %m\n",
			sip_inst, err);
		mem_deref(sip_inst);
		return err;
	}
	
	info("sip(%p): init: added to inst=%p\n", sip_inst, inst);
	list_append(&g_sip.instl, &sip_inst->le, sip_inst);
	
	return 0;
}

int wcall_i_sip_close(struct calling_instance *inst)
{
	struct sip_instance *sip_inst;
	size_t n;

	info("sip: close: inst=%p\n", inst);
	
	if (!g_sip.initialized) {
		warning("sip: close: not initialized\n");
		return ENOSYS;
	}

	sip_inst = wcall_get_sip_instance(inst);
	if (sip_inst) {
		wcall_unregister_sip_instance(inst, sip_inst);
		list_unlink(&sip_inst->le);

		/* Destroys its UAs and their calls, which must happen
		 * before baresip is closed
		 */
		mem_deref(sip_inst);
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
	mem_deref(wua->mod_aor);

	mem_deref(wua->purge.req);
	mem_deref(wua->purge.auth);
	mem_deref(wua->purge.dlg);
	mem_deref(wua->purge.acc);

	list_unlink(&wua->le);

	mem_deref(wua->userid);
	mem_deref(wua->clientid);

	mem_deref(wua->ua);

	/* The wsips own the conv entries; convl is only an index.
	 * Clear it so entries outliving the wua don't unlink from it.
	 */
	list_flush(&wua->wsipl);
	list_clear(&wua->convl);

	/* Freed last, wsip destructors use it to detach audio */
	mem_deref(wua->convid);
}


static int start_ua(struct wsip_ua *wua)
{
	int err;

	info("sip(%p): create: allocating UA with aor=%s\n",
	     wua->sip_inst, wua->mod_aor);
	err = ua_alloc(&wua->ua, wua->mod_aor);
	if (err) {
		warning("sip(%p): create: could not allocate ua\n",
			wua->sip_inst);
	}

	return err;
}


static void purge_done(struct wsip_ua *wua)
{
	int err;

	wua->purge.req = mem_deref(wua->purge.req);
	wua->purge.auth = mem_deref(wua->purge.auth);
	wua->purge.dlg = mem_deref(wua->purge.dlg);
	wua->purge.acc = mem_deref(wua->purge.acc);

	err = start_ua(wua);
	if (err && wua->errh)
		wua->errh(wua, "could not allocate ua", wua->arg);
}


static int purge_auth_handler(char **username, char **password,
			      const char *realm, void *arg)
{
	struct wsip_ua *wua = arg;

	return account_auth(wua->purge.acc, username, password, realm);
}


static void purge_resp_handler(int err, const struct sip_msg *msg, void *arg);

static int purge_request(struct wsip_ua *wua)
{
	return sip_drequestf(&wua->purge.req, uag_sip(), true, "REGISTER",
			     wua->purge.dlg, 0, wua->purge.auth,
			     NULL, purge_resp_handler, wua,
			     "Contact: *\r\n"
			     "Expires: 0\r\n"
			     "Content-Length: 0\r\n"
			     "\r\n");
}


static void purge_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct wsip_ua *wua = arg;

	if (err) {
		warning("sip(%p): purge: request failed: %m\n",
			wua->sip_inst, err);
		goto out;
	}

	if (msg->scode < 200)
		return;

	if (sip_request_loops(&wua->purge.ls, msg->scode)) {
		warning("sip(%p): purge: request loops: %u %r\n",
			wua->sip_inst, msg->scode, &msg->reason);
		goto out;
	}

	if (msg->scode < 300) {
		info("sip(%p): purge: removed all bindings: %u %r\n",
		     wua->sip_inst, msg->scode, &msg->reason);
		goto out;
	}

	if (msg->scode == 401 || msg->scode == 407) {
		err = sip_auth_authenticate(wua->purge.auth, msg);
		if (!err)
			err = purge_request(wua);
		if (!err)
			return;

		warning("sip(%p): purge: authentication failed: %m\n",
			wua->sip_inst, err);
		goto out;
	}

	warning("sip(%p): purge: failed: %u %r\n",
		wua->sip_inst, msg->scode, &msg->reason);

 out:
	/* Register normally whatever the outcome of the purge */
	purge_done(wua);
}


/* Remove all bindings of the AOR from the registrar with a wildcard
 * unregister (RFC 3261 10.2.2), so that stale contacts left behind by
 * earlier instances do not get INVITEs forked to them.
 * Returns ENOENT if regpurge is not enabled for this AOR.
 */
static int purge_bindings(struct wsip_ua *wua)
{
	struct sip_addr *laddr;
	struct uri uri;
	struct pl pl;
	const char *routev[1];
	char *reg_uri = NULL;
	int err;

	err = account_alloc(&wua->purge.acc, wua->mod_aor);
	if (err)
		return err;

	laddr = account_laddr(wua->purge.acc);
	if (msg_param_decode(&laddr->params, "regpurge", &pl)
	    || pl_strcasecmp(&pl, "yes")) {
		err = ENOENT;
		goto out;
	}

	/* Same Request-URI and route as the UA's own REGISTER */
	uri = laddr->uri;
	uri.user = uri.password = pl_null;
	err = re_sdprintf(&reg_uri, "%H", uri_encode, &uri);
	if (err)
		goto out;

	routev[0] = account_outbound(wua->purge.acc, 0);

	err = sip_dialog_alloc(&wua->purge.dlg, reg_uri,
			       account_aor(wua->purge.acc), NULL,
			       account_aor(wua->purge.acc),
			       routev[0] ? routev : NULL,
			       routev[0] ? 1 : 0);
	if (err)
		goto out;

	err = sip_auth_alloc(&wua->purge.auth, purge_auth_handler,
			     wua, false);
	if (err)
		goto out;

	info("sip(%p): purge: removing all bindings for %s\n",
	     wua->sip_inst, account_aor(wua->purge.acc));

	err = purge_request(wua);

 out:
	mem_deref(reg_uri);
	if (err) {
		wua->purge.auth = mem_deref(wua->purge.auth);
		wua->purge.dlg = mem_deref(wua->purge.dlg);
		wua->purge.acc = mem_deref(wua->purge.acc);
	}

	return err;
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
	err = re_sdprintf(&wua->mod_aor, "%s;", aor);
	if (err)
		goto out;

	/* With regpurge=yes the UA is allocated once the purge is done */
	err = purge_bindings(wua);
	if (err == ENOENT)
		err = start_ua(wua);
	else if (err) {
		warning("sip(%p): create: purge failed: %m\n",
			sip_inst, err);
		err = start_ua(wua);
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

	/* No UA yet while the purge is pending */
	if (wua->ua)
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

	/* A call is answered once: a second answer would register
	 * a second pstn handler, which the destructor doesn't remove,
	 * and leak the convid.
	 */
	if (wsip->convid) {
		warning("sip(%p): answer: wsip=%p already answered "
			"in convid=%s\n", inst, wsip, wsip->convid);
		return EALREADY;
	}

	/* Assign convid to this call */
	err = str_dup(&wsip->convid, convid);
	if (err)
		goto out;

	/* Before the adm handler, which needs the conv entry key */
	err = attach_conv_entry(wsip);
	if (err)
		goto out;

	err = pstn_adm_handler_register(wsip->convid, adm_handler, wsip);
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
