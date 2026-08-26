#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include "baresip.h"
#include "wcall.h"

struct {
	struct list ual;
} g_sip;

struct wsip {
	struct ua *ua;
	char *aor;

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


int wcall_i_sip_init(const char *conf_path)
{
	int err = 0;

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
	

	err = uag_event_register(ua_event_handler, NULL);
	if (err) {
		warning("sip: failed to register ua event handler: %m\n", err);
		return err;
	}

	info("sip: event handler registered\n");
	
	return 0;
}

int wcall_i_sip_close(void)
{
	baresip_close();

	return 0;
}

static void wsip_destructor(void *arg)
{
	struct wsip *wsip = arg;

	(void)wsip;
}

int wcall_i_sip_create(const char *aor)
{
	struct wsip *wsip;
	int err;

	info("sip: create: aor=%s\n", aor);

	wsip = mem_zalloc(sizeof(*wsip), wsip_destructor);
	if (!wsip)
		return ENOMEM;

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
		list_append(&g_sip.ual, &wsip->le, wsip);
	}

	return err;
}

int wcall_i_sip_destroy(const char *aor)
{
	/* Lookup wsip from aor */

	return 0;
}

