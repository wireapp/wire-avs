/*
 * Wire
 * Copyright (C) 2018 Wire Swiss GmbH
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

#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <memory.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <pthread.h>

#include <re.h>
#include "avs_network.h"
#include "avs_string.h"
#include "avs_log.h"
#include "dns_platform.h"

#ifdef __APPLE__
#       include <TargetConditionals.h>
#endif

#ifdef ANDROID
#define TMOBILE_WORKAROUND
#endif

#define DNS_QUERY_TIMEOUT  3000



static struct {
	struct lock *lock;
	struct mqueue *mq;
	struct list lookupl;
} dns = {
	.lock = NULL,
};


static int dns_lookup_internal(const char *url,
			       dns_lookup_h *lookuph, void *arg);

static void mqueue_handler(int id, void *data, void *arg)
{
	struct dns_lookup_entry *lent = data;
	struct le *le;
	
	(void)id;
	(void)arg;

	if (lent->lookuph)
		lent->lookuph(lent->err, &lent->srv, lent->arg);
	
	lock_write_get(dns.lock);
	le = list_head(&lent->lookupl);
	lock_rel(dns.lock);
	while (le) {
		struct dns_lookup_entry *ll = le->data;
		
		if (ll->lookuph)
			ll->lookuph(lent->err, &lent->srv, ll->arg);

		lock_write_get(dns.lock);
		mem_deref(ll);
		le = list_head(&lent->lookupl);
		lock_rel(dns.lock);
	}

	lock_write_get(dns.lock);
	mem_deref(lent);
	lock_rel(dns.lock);
}


int dns_init(void *arg)
{
	int err;

	(void)arg;
	
	if (dns.lock)
		return EALREADY;

	err = lock_alloc(&dns.lock);
	if (err)
		goto out;

	list_init(&dns.lookupl);

	err = mqueue_alloc(&dns.mq, mqueue_handler, NULL);
	if (err)
		goto out;
	
	err = dns_platform_init(arg);

 out:	
	return err;
}


static struct dns_lookup_entry *find_lookup(struct list *ll, const char *host)
{
	struct dns_lookup_entry *lent = NULL;	
	bool found = false;
	struct le *le;

	le = list_head(ll);
	while (!found && le != NULL) {
		lent = le->data;
		found = strcaseeq(host, lent->host);
		le = le->next;
	}

	return found ? lent : NULL;
}

static void *lookup_thread(void *arg)
{
	struct dns_lookup_entry *lent = arg;
	int err;

	err = dns_platform_lookup(lent, &lent->srv);
	lent->err = err;

	mqueue_push(dns.mq, 0, lent);

	return NULL;
}


static void lent_destructor(void *arg)
{
	struct dns_lookup_entry *lent = arg;

	list_unlink(&lent->le);
	
	mem_deref(lent->host);
}

#ifdef TMOBILE_WORKAROUND

struct dns_query_entry {
	struct dnsc *dnsc;
	struct dns_query *q;

	struct tmr query_tmr;
	struct tmr deref_tmr;

	char *host;
	dns_lookup_h *dnsh;
	void *arg;
};

static void dnsq_destructor(void *arg)
{
	struct dns_query_entry *dnsq = arg;

	tmr_cancel(&dnsq->query_tmr);
	tmr_cancel(&dnsq->deref_tmr);

	mem_deref(dnsq->host);
	mem_deref(dnsq->q);
	mem_deref(dnsq->dnsc);
	
}

static void dnsq_destructor_handler(void *arg)
{
	struct dns_query_entry *dnsq = arg;

	mem_deref(dnsq);
}


static void query_timeout_handler(void *arg)
{
	struct dns_query_entry *dnsq = arg;

	dns_lookup_internal(dnsq->host, dnsq->dnsh, dnsq->arg);
}


static void raw_lookup_handler(int err, const struct dnshdr *hdr,
			       struct list *ansl, struct list *authl,
			       struct list *addl, void *arg)
{
	struct dns_query_entry *dnsq = arg;
        struct dnsrr *rr;
	struct sa srv;

        (void)hdr;
        (void)authl;
        (void)addl;

	tmr_cancel(&dnsq->query_tmr);
	
	/* Find A answers */
        rr = dns_rrlist_find(ansl, NULL, DNS_TYPE_A, DNS_CLASS_IN, false);
        if (!rr) {
                err = err ? err : EDESTADDRREQ;
                goto out;
        }
	
        sa_set_in(&srv, rr->rdata.a.addr, 3478);
	if (dnsq->dnsh)
		dnsq->dnsh(err, &srv, dnsq->arg);

 out:
	tmr_start(&dnsq->deref_tmr, 1, dnsq_destructor_handler, dnsq);
}


static int raw_query(const char *host, dns_lookup_h *lookuph, void *arg)
{
	struct dns_query_entry *dnsq;
	struct sa goog;
	int err;

	dnsq = mem_zalloc(sizeof(*dnsq), dnsq_destructor);
	if (!dnsq)
		return ENOMEM;	
	
	sa_init(&goog, AF_INET);
	sa_set_str(&goog, "8.8.8.8", DNS_PORT);
	err = dnsc_alloc(&dnsq->dnsc, NULL, &goog, 1);
	if (err) {
		warning("dns: dnsc_alloc failed: %m\n", err);
		goto out;
	}

	tmr_init(&dnsq->deref_tmr);
	tmr_init(&dnsq->query_tmr);
	str_dup(&dnsq->host, host);
	dnsq->dnsh = lookuph;
	dnsq->arg = arg;
	tmr_start(&dnsq->query_tmr, DNS_QUERY_TIMEOUT,
		  query_timeout_handler, dnsq);
	err = dnsc_query(&dnsq->q, dnsq->dnsc, host, DNS_TYPE_A, DNS_CLASS_IN,
			 true, raw_lookup_handler, dnsq);
	if (err) {
		warning("dns: dnsc_query failed: %m\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(dnsq);

	return err;
}

struct if_lookup_entry {
	bool has_wifi;
};

static bool check_if_handler(const char *ifname, const struct sa *sa,
			     void *arg)
{
	struct if_lookup_entry *ile = arg;

	info("dns: if[%s]\n", ifname);
	
	if (strstr(ifname, "wlan")) {
		ile->has_wifi = true;
		info("dns: has wifi\n");
		return true;
	}
	
	info("dns: has NO wifi\n");

	return false;
}

#endif


static int dns_lookup_internal(const char *url,
			       dns_lookup_h *lookuph, void *arg)
{
	struct dns_lookup_entry *lent;
	struct dns_lookup_entry *pend_lent;
	struct list *ll;
	int err = 0;

	info("dns: lookup internal: %s\n", url);

	lent = mem_zalloc(sizeof(*lent), lent_destructor);
	if (!lent)
		return ENOMEM;

	lock_write_get(dns.lock);
	
	err = str_dup(&lent->host, url);
	if (err)
		goto out;

	list_init(&lent->lookupl);
	lent->lookuph = lookuph;
	lent->arg = arg;

	pend_lent = find_lookup(&dns.lookupl, url);

	/* The lookup is either appended to an already running lookup,
	 * or to the list of pending lookups.
	 */
	ll = pend_lent ? &pend_lent->lookupl : &dns.lookupl;
	list_append(ll, &lent->le, lent);

	/* If there were no pending lookup requests, we need to create one */
	if (pend_lent) {
		lent->tid = pend_lent->tid;
	}
	else {
		err = pthread_create(&lent->tid, NULL, lookup_thread, lent);
		if (err) {
			warning("dns_iosx: lookup thread failed: %m\n", err);
			goto out;
		}
	}

 out:
	if (err)
		mem_deref(lent);
	lock_rel(dns.lock);

	return err;
}

int dns_lookup(const char *url, dns_lookup_h *lookuph, void *arg)
{
#ifdef TMOBILE_WORKAROUND
	struct sa laddr;
	int err = 0;
	
	/* Apply this workaround only on IPv4 networks */
	if (0 != net_default_source_addr_get(AF_INET6, &laddr)) {
		struct if_lookup_entry ile = {
		      .has_wifi = false
		};
		
		/* If we don't have a wlan interface,
		 * try a raw DNS query instead
		 */
		err = net_if_apply(check_if_handler, &ile);
		if (err)
			warning("dns: net_if_apply failed: %m\n", err);
		else {
			if (!ile.has_wifi) {
				info("dns: no WIFI, attempting raw query\n");
				return raw_query(url, lookuph, arg);
			}
		}
	}
#endif

	return dns_lookup_internal(url, lookuph, arg);
}



int dns_get_servers(char *domain, size_t dsize, struct sa *nsv, uint32_t *n)
{
#if TARGET_OS_IPHONE
	struct __res_state state;
	uint32_t i, cnt;
	int ret, err=0;
	union res_9_sockaddr_union *addrs;
	int k;

	memset(&state, 0, sizeof(state));
	ret = res_ninit(&state);
	if (0 != ret)
		return ENOENT;

	if (!state.nscount) {
		err = ENOENT;
		goto out;
	}

	cnt = min(*n, (uint32_t)state.nscount);

	addrs = mem_zalloc(cnt * sizeof(*addrs), NULL);
	if (!addrs) {
		err = ENOMEM;
		goto out;
	}

	cnt = res_getservers(&state, addrs, cnt);
	k = 0;
	for (i=0; i<cnt; i++) {
		switch (addrs[i].sin.sin_family) {
		case AF_INET:
			sa_set_in(&nsv[k],
				  addrs[i].sin.sin_addr.s_addr,
				  addrs[i].sin.sin_port);
			break;

		case AF_INET6:
			sa_set_in6(&nsv[k],
				   addrs[i].sin6.sin6_addr.s6_addr,
				   addrs[i].sin6.sin6_port);
			break;

		default:
			break;
		}		
		if (!sa_port(&nsv[k]))
			sa_set_port(&nsv[k], DNS_PORT);
		++k;
		    
	}
	mem_deref(addrs);

	*n = k;
 out:
	res_nclose(&state);

	return err;
#else
	return ENOSYS;
#endif
	
}


void dns_close(void)
{
	struct le *le;

	if (!dns.lock)
		return;
	
	/* Wait for all pending lookups to complete */
	lock_write_get(dns.lock);
	le = list_head(&dns.lookupl);
	while(le) {
		struct dns_lookup_entry *lent = le->data;

		lock_rel(dns.lock);
		pthread_join(lent->tid, NULL);
		lock_write_get(dns.lock);
		le = list_head(&dns.lookupl);
	}	
	lock_rel(dns.lock);

	dns.mq = mem_deref(dns.mq);
	dns.lock = mem_deref(dns.lock);

	dns_platform_close();
}
