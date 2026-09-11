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
	struct list wsipl;
};


struct wsip {
	struct sip_instance *sip_inst;
	struct ua *ua;
	char *aor;
	char *convid;
	void *adm;

	struct list calll; /* List of calls on this UA */

	struct le le;
};

struct instel {
	struct sip_instance *inst;

	struct le le;
};

struct sip_call {
	struct call *call;

	struct le le;
};

static void inst_destructor(void *arg)
{
	struct sip_instance *sip_inst = arg;

	list_flush(&sip_inst->wsipl);
}

static struct wsip *wsip_lookup(struct sip_instance *sip_inst,
				const char *convid, struct ua *ua)
{
	struct le *le;
	struct wsip *wsip;
	bool found = false;
	
	for(le = sip_inst->wsipl.head; le && !found; le = le->next) {
		wsip = le->data;
		if (!wsip)
			continue;

		if (convid)
			found = streq(wsip->convid, convid);
		else if (ua)
			found = wsip->ua == ua;
	}

	return found ? wsip : NULL;
}

static struct wsip *ua2wsip(struct ua *ua)
{
	struct wsip *wsip = NULL;
	bool found = false;
	struct le *le;

	for(le = g_sip.instl.head; le && !found; le = le->next) {
		struct sip_instance *sip_inst = le->data;

		wsip = wsip_lookup(sip_inst, NULL, ua);
		found = wsip != NULL;
	}

	return found ? wsip : NULL;
}

static void scall_destructor(void *arg)
{
	struct sip_call *scall = arg;

	list_unlink(&scall->le);
}

static void answer_call(struct wsip *wsip, struct ua *ua, struct call *call)
{
	struct audio *au;
	struct sip_call *scall;
	int err;

	au = call_audio(call);
	if (au) {
		audio_set_devicename(au, wsip->convid, wsip->convid);
	}
	
	err = ua_answer(ua, call);
	if (err) {
		warning("sip(%p): answer_call=%p answer failed: %m\n",
			wsip->sip_inst, call, err);
		return;
	}
	scall = mem_zalloc(sizeof(*scall), scall_destructor);
	if (!scall) {
		warning("sip(%p): failled to alloc scall\n", wsip->sip_inst);
		return;
	}
	scall->call = call;
	list_append(&wsip->calll, &scall->le, scall);
}

static void close_call(struct wsip *wsip, struct call *call)
{
	struct sip_call *scall;
	bool found = false;
	struct le *le;

	for(le = wsip->calll.head; le && !found; le = le->next) {
		scall = le->data;
		if (!scall)
			continue;

		found = call == scall->call;
	}

	if (found) {
		mem_deref(scall);
	}
}

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm,
			     void *arg)
{
	struct wsip *wsip = ua2wsip(ua);
	
	(void)arg;
	(void)prm;

	if (!wsip) {
		warning("sip: ua_event: no instance for ua=%p\n", ua);
		return;
	}

#if 0
	info("sip: event: %d(%s) ua: %p call=%p\n",
	     ev, uag_event_str(ev), ua, call);
#endif

	switch(ev) {
	case UA_EVENT_CALL_INCOMING:
		info("sip(%p): incoming call on wsip=%p call=%p\n",
		     wsip->sip_inst, wsip, call);
		answer_call(wsip, ua, call);
		break;

	case UA_EVENT_CALL_RINGING:
		break;

	case UA_EVENT_CALL_PROGRESS:
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		break;

	case UA_EVENT_CALL_CLOSED:
		info("sip(%p): call=%p closed\n", wsip->sip_inst);
		close_call(wsip, call);
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
	struct wsip *wsip = arg;
	
	pstn_play_read(wsip->adm, (int16_t *)sampv, sampc);
}

static void adm_play_handler(const void *sampv, size_t sampc, void *arg)
{
	struct wsip *wsip = arg;
	
	pstn_rec_write(wsip->adm, (const int16_t *)sampv, sampc);
}


static void adm_handler(const char *convid, void *adm, bool added, void *arg)
{
	struct wsip *wsip = arg;

	info("sip(%p): adm_handler: adm=%p %s on wsip=%p\n",
	     wsip->sip_inst, adm, added ? "ADDED" : "REMOVED", wsip);

	wsip->adm = adm;
	
	wireaudio_set_handlers(convid, adm_play_handler, adm_rec_handler,
			       wsip);
}


int wcall_i_sip_init(struct calling_instance *inst, const char *conf_path)
{
	struct sip_instance *sip_inst;
	struct instel *instel;
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
	
	instel = mem_zalloc(sizeof(*instel), NULL);
	if (instel)
		instel->inst = sip_inst;

	info("sip(%p): init: added to inst=%p\n", sip_inst, inst);
	list_append(&g_sip.instl, &instel->le, instel);
	
	return 0;
}

int wcall_i_sip_close(struct calling_instance *inst)
{
	struct le *le;
	struct instel *instel;
	bool found = false;
	size_t n;

	info("sip: close: inst=%p\n", inst);
	
	if (!g_sip.initialized) {
		warning("sip: close: not initialized\n");
		return ENOSYS;
	}

	for(le = g_sip.instl.head; le && !found; le = le->next) {
		instel = le->data;
		if (!instel)
			continue;

		found = instel->inst == wcall_get_sip_instance(inst);
	}
	if (found) {
		list_unlink(&instel->le);
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

static void wsip_destructor(void *arg)
{
	struct wsip *wsip = arg;

	info("wsip(%p): destructor\n", wsip);
	
	list_unlink(&wsip->le);	
	mem_deref(wsip->aor);
	mem_deref(wsip->ua);
	mem_deref(wsip->convid);
}

int wcall_i_sip_create(struct calling_instance *inst,
		       const char *convid,
		       const char *aor)
{
	struct sip_instance *sip_inst;
	struct wsip *wsip;
	void *adm;
	char mod_aor[1024];
	int err;

	info("sip: create: aor=%s\n", aor);

	sip_inst = wcall_get_sip_instance(inst);
	if (!sip_inst) {
		warning("sip: create: no SIP instance for: %p\n", inst);
		return ENOSYS;
	}
	
	wsip = mem_zalloc(sizeof(*wsip), wsip_destructor);
	if (!wsip)
		return ENOMEM;
	
	wsip->sip_inst = sip_inst;

	re_snprintf(mod_aor, sizeof(mod_aor),
		    "%s;"
		    "audio_source=wireaudio;"
		    "audio_player=wireaudio",
		    aor);
	
	err = str_dup(&wsip->aor, mod_aor);
	if (err) {
		warning("sip: could not allocate aor string\n");
		goto out;
	}

	info("sip(%p): create: allocating ua with aor=%s\n",
	     sip_inst, wsip->aor);
	err = ua_alloc(&wsip->ua, wsip->aor);
	if (err) {
		warning("sip(%p): create: could not allocate ua\n", sip_inst);
		goto out;
	}

	err = pstn_adm_handler_register(adm_handler, wsip);
	if (err) {
		warning("sip: could not register handler\n");
		goto out;
	}

	str_dup(&wsip->convid, convid);

	adm = pstn_adm_find(wsip->convid);
	/* Do we already have an adm?
	 * If so, then register audio directly,
	 * otherwise wait for the adm_handler for an adm
	 */
	if (adm) {
		//wireaudio_set_adm(wsip->convid, adm);
	}

 out:
	if (err) {
		mem_deref(wsip);
	}
	else {
		list_append(&sip_inst->wsipl, &wsip->le, wsip);
	}

	return err;
}


int wcall_i_sip_destroy(struct calling_instance *inst,
			const char *convid,
			const char *aor)
{
	struct sip_instance *sip_inst;
	struct wsip *wsip;

	sip_inst = wcall_get_sip_instance(inst);
	if (!sip_inst) {
		warning("sip: destroy: could not get SIP instance\n");
		return ENOSYS;
	}

	wsip = wsip_lookup(sip_inst, convid, NULL);
	if (!wsip) {
		warning("sip(%p): destroy: could not find wsip for aor=%s\n",
			sip_inst, aor);
		return EINVAL;
	}

	info("sip(%p): destroy: unregistering adm handler for wsip=%p\n",
	     sip_inst, wsip);
	pstn_adm_handler_unregister(adm_handler, wsip);
	
	info("sip(%p): destroy: aor=%s wsip=%p\n", sip_inst, aor, wsip);
	
	mem_deref(wsip);

	return 0;
}

