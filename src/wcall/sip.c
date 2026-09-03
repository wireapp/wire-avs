#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include "baresip.h"
#include "wcall.h"
#include "sip.h"

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

struct wsip {
	struct sip_instance *sip_inst;
	struct ua *ua;
	char *aor;

	struct le le;
};

struct instel {
	struct calling_instance *inst;
	struct le le;
};

static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm,
			     void *arg)
{
	(void)arg;
	(void)prm;
	
	/* For now this is just a place holder,
	 * here we will check for registration,
	 * incoming call, etc
	 */
	info("sip: event: %d ua: %p call=%p\n", ev, ua, call);
}


int wcall_i_sip_init(struct calling_instance *inst, const char *conf_path)
{
	struct instel *instel;
	int err = 0;

	if (g_sip.initialized)
		goto out;
	
	info("sip: initializing with conf_path=%s\n", conf_path);

	conf_path_set(conf_path);
	
	err = conf_configure();
	if (err) {
		warning("sip: failed to configure: %m\n", err);
		return err;
	}

	err = baresip_init(conf_config(), false);
	if (err) {
		warning("sip: failed to initialize baresip: %m\n", err);
		return err;
	}

	info("sip: baresip initialized\n", conf_path);

	err = conf_modules();
	if (err) {
		warning("sip: failed to load modules: %m\n", err);
		return err;
	}

	err = uag_event_register(ua_event_handler, NULL);
	if (err) {
		warning("sip: failed to register ua event handler: %m\n", err);
		return err;
	}

	info("sip: init: event handler registered\n");

	g_sip.initialized = true;

 out:
	instel = mem_zalloc(sizeof(*instel), NULL);
	if (instel)
		instel->inst = inst;

	info("sip: init: inst=%p added\n", inst);
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

		found = instel->inst == inst;
	}
	if (found) {
		list_unlink(&instel->le);
	}

	n = list_count(&g_sip.instl);
	info("sip: close: closed inst=%p n=%zu\n", inst, n);
	
	if (n == 0){
		info("sip: no active instances left, closing\n");
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
}

int wcall_i_sip_create(struct calling_instance *inst, const char *aor)
{
	struct sip_instance *sip_inst;
	struct wsip *wsip;
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
	str_dup(&wsip->aor, aor);
	err = ua_alloc(&wsip->ua, aor);
	if (err) {
		warning("sip: could not allocate ua\n");
		goto out;
	}

 out:
	if (err) {
		mem_deref(wsip);
	}
	else {
		list_append(&sip_inst->ual, &wsip->le, wsip);
	}

	return err;
}

static struct wsip *wsip_lookup(struct sip_instance *sip_inst, const char *aor)
{
	struct le *le;
	struct wsip *wsip;
	bool found = false;
	
	for(le = sip_inst->ual.head; le && !found; le = le->next) {
		wsip = le->data;
		if (!wsip)
			continue;

		found = streq(wsip->aor, aor);
	}

	return found ? wsip : NULL;
}

int wcall_i_sip_destroy(struct calling_instance *inst, const char *aor)
{
	struct sip_instance *sip_inst;
	struct wsip *wsip;

	sip_inst = wcall_get_sip_instance(inst);
	if (!sip_inst) {
		warning("sip: destroy: could not get SIP instance\n");
		return ENOSYS;
	}

	wsip = wsip_lookup(sip_inst, aor);
	if (!wsip) {
		warning("sip: destroy: could not find wsip for aor=%s\n", aor);
		return EINVAL;
	}

	info("sip(%p): destroy: aor=%s wsip=%p\n", sip_inst, aor, wsip);
	
	mem_deref(wsip);

	return 0;
}

