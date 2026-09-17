#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include <avs_pstn.h>
//#include <avs_wireaudio.h>
#include "baresip.h"
#include "wcall.h"
#include "sip.h"

extern int wireaudio_set_handlers(const char *convid,
				  ausrc_read_h *rh,
				  auplay_write_h *wh,
				  void *arg);


struct {
	bool initialized;
	struct list instl;
	struct log log;
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

	struct le le;
};


struct wsip_ua {
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

	struct le le;
};

struct wsip_call {
	struct wsip_ua *wua;
	struct call *call;
	char *convid;
	void *adm;

	struct le le;
};

static void adm_handler(const char *convid, void *adm, bool added, void *arg);


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

static void close_call(struct wsip_ua *wua, struct call *call)
{
	struct wsip_call *wsip;
	bool found = false;
	struct le *le;

	for(le = wua->wsipl.head; le && !found; le = le->next) {
		wsip = le->data;
		if (!wsip)
			continue;

		found = call == wsip->call;
	}

	if (found && wua->closeh) {
		wua->closeh(wsip, wua->arg);
	}

	if (found) {
		mem_deref(wsip);
	}
}

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

static void wsip_destructor(void *arg)
{
	struct wsip_call *wsip = arg;

	info("wsip(%p): destructor\n", wsip);

	list_unlink(&wsip->le);

	adm_handler(wsip->convid, wsip->adm, false, wsip);

	mem_deref(wsip->convid);
}

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm,
			     void *arg)
{
	struct wsip_ua *wua = NULL;
	struct wsip_call *wsip = NULL;
	int err = 0;
	
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
		wsip = mem_zalloc(sizeof(*wsip), wsip_destructor);
		if (!wsip) {
			err = ENOMEM;
			break;
		}

		wsip->wua = wua;
		wsip->call = call;
		list_append(&wua->wsipl, &wsip->le, wsip);

		info("sip(%p): incoming call on wua=%p call=%p\n",
		     wua->sip_inst, wua, call);
		if (wua->incomingh) {
			const char *from = call_peeruri(call);
			char *pin;

			err = parse_pin(&pin, call_localuri(call));
			if (err) {
				wua->incomingh(wua, wsip, from, NULL, wua->arg);
			}
			else {
				wua->incomingh(wua, wsip, from, pin, wua->arg);
				mem_deref(pin);
			}
		}
		break;

	case UA_EVENT_CALL_RINGING:
		break;

	case UA_EVENT_CALL_PROGRESS:
		break;

	case UA_EVENT_CALL_ESTABLISHED:
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

	info("sip(%p): adm_handler: adm=%p %s on wsip=%p\n",
	     wsip->wua->sip_inst, adm, added ? "ADDED" : "REMOVED", wsip);

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
	err = ua_init("jbp", true, true, false, false);
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
	
	mem_deref(wua->ua);

	list_flush(&wua->wsipl);
}


int wcall_i_sip_create(struct calling_instance *inst,
		       const char *aor,
		       wcall_sip_ready_h *readyh,
		       wcall_sip_incoming_h *incomingh,
		       wcall_sip_close_h *closeh,
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
	
	wua = mem_zalloc(sizeof(*wua), wua_destructor);
	if (!wua)
		return ENOMEM;
	
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

	info("sip(%p): create: allocating UA with aor=%s\n",
	     sip_inst, wua->aor);
	err = ua_alloc(&wua->ua, wua->aor);
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
		       struct wsip_call *wsip, const char *convid)
{
	int err = 0;
	void *adm;

	if (!wsip || !convid) {
		warning("sip(%p): answer: invalid wsip=%p convid=%p\n",
			inst, wsip, convid);
		return EINVAL;
	}

	info("sip(%p): answer: on wsip=%p convid=%s\n", inst, wsip, convid);

	/* Assign convid to this call */
	str_dup(&wsip->convid, convid);

	err = pstn_adm_handler_register(adm_handler, wsip);
	if (err) {
		warning("sip: could not register handler\n");
		goto out;
	}

	adm = pstn_adm_find(wsip->convid);
	/* Do we already have an adm?
	 * If so, then register audio directly,
	 * otherwise wait for the adm_handler for an adm
	 */
	if (adm) {
		adm_handler(convid, adm, true, wsip);
	}

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
