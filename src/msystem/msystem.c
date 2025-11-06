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

#include <pthread.h>

#include "re.h"
#include "avs.h"

#include "msystem.h"

#ifdef __APPLE__
#       include <TargetConditionals.h>
#endif

struct msystem {
	pthread_t tid;

	struct msystem_config config;
	
	bool inited;
	bool started;
	bool audio_activated;
	struct dnsc *dnsc;
	struct tls *dtls;
	struct mqueue *mq;
	struct tmr vol_tmr;
	char *name;
	bool using_voe;
	bool loopback;
	bool privacy;
	bool crypto_kase;
	char ifname[256];

	char version[256];
	char project[256];

	struct list mutel;
	struct list activatel;

	struct msystem_proxy *proxy;
};


struct activate_elem {
	msystem_activate_h *activateh;
	void *arg;

	struct le le;
};

struct mute_elem {
	msystem_mute_h *muteh;
	void *arg;

	struct le le;
};

static struct msystem *g_msys = NULL;

static int msys_env = 0;

void msystem_set_env(int env)
{
	msys_env = env;
}

int msystem_get_env(void)
{
	return msys_env;
}

void msystem_set_version(const char *ver)
{
	if (!g_msys)
		return;

	str_ncpy(g_msys->version, ver, sizeof(g_msys->version));
}

const char *msystem_get_version(void)
{
	if (!g_msys)
		return NULL;

	return g_msys->version[0] == '\0' ? NULL : g_msys->version;
}

void msystem_set_project(const char *proj)
{
	if (!g_msys)
		return;

	str_ncpy(g_msys->project, proj, sizeof(g_msys->project));
}

const char *msystem_get_project(void)
{
	if (!g_msys)
		return NULL;

	return g_msys->project[0] == '\0' ? NULL : g_msys->project;
}



static void msystem_destructor(void *data)
{
	struct msystem *msys = data;

	if (msys->name) {
		if (streq(msys->name, "audummy")) {
		}
	}

	tmr_cancel(&msys->vol_tmr);

	msys->mq = mem_deref(msys->mq);
	msys->dtls = mem_deref(msys->dtls);
	msys->name = mem_deref(msys->name);
	msys->dnsc = mem_deref(msys->dnsc);
	msys->proxy = mem_deref(msys->proxy);

	list_flush(&msys->mutel);
	list_flush(&msys->activatel);
	
	iflow_destroy();

	msys->inited = false;

	if (g_msys == msys)
		g_msys = NULL;
}


/* This is just a dummy handler fo waking up re_main() */
static void wakeup_handler(int id, void *data, void *arg)
{
	(void)id;
	(void)data;
	(void)arg;
}


static int msystem_init(struct msystem **msysp, const char *msysname,
			struct msystem_config *config)
{
	struct msystem *msys;
	int err;
	
	if (!msysp)
		return EINVAL;

	msys = mem_zalloc(sizeof(*msys), msystem_destructor);
	if (!msys)
		return ENOMEM;

	err = mqueue_alloc(&msys->mq, wakeup_handler, NULL);
	if (err) {
		warning("flowmgr: failed to create mqueue (%m)\n", err);
		goto out;
	}

	tmr_init(&msys->vol_tmr);

	info("msystem: initializing for msys: %s\n", msysname);
	
	err = str_dup(&msys->name, msysname);
	if (streq(msys->name, "audummy")) {
		//err = audummy_init(&msys->aucodecl);
	}
	else if (streq(msys->name, "voe")) {
		msys->using_voe = true;
	}
	else {
		warning("flowmgr: media-system not available (%s)\n",
			msys->name);
		err = EINVAL;
	}

	if (err)
		goto out;

	info("msystem: successfully initialized\n");

        msys->audio_activated = true;
	msys->inited = true;

	if (config) {
		msys->config = *config;

		if (config->data_channel) {

			//err = dce_init();
			//if (err)
			//	goto out;
		}
	}
	
	g_msys = msys;

 out:
	if (err)
		mem_deref(msys);
	else
		*msysp = msys;

	return err;
}

static void me_destructor(void *arg)
{
	struct mute_elem *me = arg;

	list_unlink(&me->le);
}

struct msystem *msystem_instance(void)
{
	return g_msys;
}

int msystem_get(struct msystem **msysp, const char *msysname,
		struct msystem_config *config,
		msystem_mute_h *muteh,
		void *arg)
{
	int err = 0;
	
	if (!msysp)
		return EINVAL;
	
	if (g_msys)
		*msysp = mem_ref(g_msys);
	else {
		err = msystem_init(msysp, msysname, config);
		if (err)
			return err;
	}

	if (muteh) {
		struct mute_elem *me;
		
		me = mem_zalloc(sizeof(*me), me_destructor);
		me->muteh = muteh;
		me->arg = arg;
		list_append(&g_msys->mutel, &me->le, me);
	}
	
	return err;
}

void msystem_unregister_listener(void *arg)
{
	struct le *le;

	LIST_FOREACH(&g_msys->mutel, le) {
		struct mute_elem *me = le->data;

		if (arg == me->arg) {
			mem_deref(me);
			break;
		}
	}
}

static void ae_destructor(void *arg)
{
	struct activate_elem *ae;

	(void)ae;
}

void *msystem_add_activate_handler(msystem_activate_h *activateh, void *arg)
{
	struct activate_elem *ae;

	if (!activateh)
		return NULL;

	ae = mem_zalloc(sizeof(*ae), ae_destructor);
	if (!ae)
		return NULL;

	ae->activateh = activateh;
	ae->arg = arg;
	list_append(&g_msys->activatel, &ae->le, ae);

	return (void *)ae;
}

void msystem_activate(bool active)
{
	struct le *le;

	LIST_FOREACH(&g_msys->activatel, le) {
		struct activate_elem *ae = le->data;

		if (!ae)
			continue;

		if (ae->activateh) {
			ae->activateh(active, ae->arg);
		}
	}
}

void msystem_set_tid(struct msystem *msys, pthread_t tid)
{
	info("msystem: setting tid to: %p\n", tid);
	
	if (msys)
		msys->tid = tid;
}

static bool tid_isset(struct msystem *msys)
{
	pthread_t tid;

	memset(&tid, 0, sizeof(tid));

	return !pthread_equal(tid, msys->tid);
}

void msystem_enter(struct msystem *msys)
{
	if (!msys)
		return;

	if (!tid_isset(msys)) {
		warning("msystem: enter: tid not set!\n");
		return;
	}
	
	if (!pthread_equal(pthread_self(), msys->tid))
		re_thread_enter();
}


void msystem_leave(struct msystem *msys)
{
	if (!msys)
		return;

	if (!tid_isset(msys)) {
		warning("msystem: leave: tid not set!\n");
		return;
	}
	
	if (!pthread_equal(pthread_self(), msys->tid))
		re_thread_leave();
}
	

struct tls *msystem_dtls(struct msystem *msys)
{
	return msys ? msys->dtls : NULL;
}


bool msystem_get_loopback(struct msystem *msys)
{
	return msys ? msys->loopback : false;
}


bool msystem_get_privacy(struct msystem *msys)
{
	return msys ? msys->privacy : false;
}


const char *msystem_get_interface(struct msystem *msys)
{
	return msys ? msys->ifname : NULL;
}


void msystem_start(struct msystem *msys)
{
	if (!msys)
		return;

	msys->started = true;
}


void msystem_stop(struct msystem *msys)
{
	if (!msys)
		return;

	msys->started = false;
}


bool msystem_is_started(struct msystem *msys)
{
	return msys ? msys->started : false;
}


int msystem_push(struct msystem *msys, int op, void *arg)
{
	if (!msys->mq)
		return ENOSYS;

	return mqueue_push(msys->mq, op, arg);	
}


bool msystem_is_using_voe(struct msystem *msys)
{
	return msys ? msys->using_voe : false;
}


void msystem_enable_loopback(struct msystem *msys, bool enable)
{
	if (!msys)
		return;

	msys->loopback = enable;	
}


void msystem_enable_privacy(struct msystem *msys, bool enable)
{
	if (!msys)
		return;

	msys->privacy = enable;	
}


void msystem_enable_kase(struct msystem *msys, bool enable)
{
	if (!msys)
		return;

	msys->crypto_kase = enable;
}


bool msystem_have_kase(const struct msystem *msys)
{
	return msys ? msys->crypto_kase : false;
}


void msystem_set_ifname(struct msystem *msys, const char *ifname)
{
	if (!msys)
		return;

	str_ncpy(msys->ifname, ifname, sizeof(msys->ifname));
}
			  

bool msystem_is_initialized(struct msystem *msys)
{
	return msys ? msys->inited : false;
}


int msystem_enable_datachannel(struct msystem *msys, bool enable)
{
	//int err;

	if (!msys)
		return EINVAL;
/*
	msys->config.data_channel = enable;

	if (enable) {

		err = dce_init();
		if (err)
			return err;
	}
	else {
		dce_close();
	}
*/
	return 0;
}


bool msystem_have_datachannel(const struct msystem *msys)
{
//	return msys ? msys->config.data_channel : false;
	return false;
}


int msystem_update_conf_parts(struct list *partl)
{
#if 0
	const struct audec_state **adsv;
	struct le *le;
	size_t i, adsc = list_count(partl);

	adsv = mem_zalloc(sizeof(*adsv) * adsc, NULL);
	if (!adsv)
		return ENOMEM;

	/* convert the participant list to a vector */
	for (le = list_head(partl), i = 0; le; le = le->next, ++i) {
		struct conf_part *part = le->data;
		struct mediaflow *mf = part->data;
		struct audec_state *ads = mediaflow_decoder(mf);

		adsv[i] = ads;
	}

	mem_deref(adsv);
#endif
	return 0;
}

void msystem_set_auplay(const char *dev)
{
	if (!g_msys)
		return;
}

void msystem_stop_silencing(void)
{
	if (!g_msys)
		return;
}

bool msystem_get_muted(void)
{
	bool muted = false;

	if (!g_msys)
		return false;

	if (streq(g_msys->name, "voe")) {
			muted = iflow_get_mute();
	}

	return muted;
}

static void call_muteh(bool muted)
{
	struct le *le;

	LIST_FOREACH(&g_msys->mutel, le) {
		struct mute_elem *me = le->data;

		if (me->muteh)
			me->muteh(muted, me->arg);
	}
}

void msystem_set_muted(bool muted)
{
	bool mute_state = false;

	info("msys(%p): set_muted: %d\n", g_msys, muted);
	
	if (g_msys) {
		if (streq(g_msys->name, "voe")) {
			mute_state = iflow_get_mute();
			info("msys(%p): muted_state=%d->%d\n", g_msys, mute_state, muted);
			if (muted != mute_state) {
				iflow_set_mute(muted);
			}
			call_muteh(iflow_get_mute());
		}
	}
}


struct dnsc *msystem_dnsc(void)
{
	return g_msys ? g_msys->dnsc : NULL;
}


bool msystem_audio_is_activated(void)
{
	if (!g_msys)
		return false;
	
	return g_msys->audio_activated;
}

void msystem_audio_set_activated(bool activated)
{
	if (!g_msys) {
		warning("msystem: activate: no msystem available\n");
		return;
	}

	g_msys->audio_activated = activated;
}

static void proxy_destructor(void *arg)
{
	struct msystem_proxy *proxy = arg;

	mem_deref(proxy->host);
}

int msystem_set_proxy(const char *host, int port)
{
	struct msystem_proxy *proxy;
	
	if (!g_msys)
		return ENOSYS;

	proxy = mem_zalloc(sizeof(*proxy), proxy_destructor);
	if (!proxy)
		return ENOMEM;
	
	str_dup(&proxy->host, host);
	proxy->port = port;
	
	mem_deref(g_msys->proxy);
	g_msys->proxy = proxy;

	return 0;
}

struct msystem_proxy *msystem_get_proxy(void)
{
	return g_msys ? g_msys->proxy : NULL;
}

