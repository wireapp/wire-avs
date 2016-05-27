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

#include <time.h>
#include <re.h>
#include <avs.h>
#include "turn.h"


enum {
	PERM_LIFETIME = 300,
};


struct perm {
	struct le he;
	struct sa peer;
	struct trafstat ts;
	const struct allocation *al;
	time_t expires;
	time_t start;
	bool new;
};


struct createperm {
	struct list perml;
	struct allocation *al;
	bool af_mismatch;
};


static void destructor(void *arg)
{
	struct perm *perm = arg;

	hash_unlink(&perm->he);

	debug("turn: allocation %p permission %j destroyed "
	      "(%llu/%llu %llu/%llu)\n",
	      perm->al, &perm->peer,
	      perm->ts.pktc_tx, perm->ts.pktc_rx,
	      perm->ts.bytc_tx, perm->ts.bytc_rx);
}


static bool hash_cmp_handler(struct le *le, void *arg)
{
	const struct perm *perm = le->data;

	return sa_cmp(&perm->peer, arg, SA_ADDR);
}


struct perm *perm_find(const struct hash *ht, const struct sa *peer)
{
	struct perm *perm;

	if (!ht || !peer)
		return NULL;

	perm = list_ledata(hash_lookup(ht, sa_hash(peer, SA_ADDR),
				       hash_cmp_handler, (void *)peer));
	if (!perm)
		return NULL;

	if (perm->expires < time(NULL)) {
		debug("turn: allocation %p permission %j expired\n",
			      perm->al, &perm->peer);
		mem_deref(perm);
		return NULL;
	}

	return perm;
}


struct perm *perm_create(struct hash *ht, const struct sa *peer,
			 const struct allocation *al)
{
	const time_t now = time(NULL);
	struct perm *perm;

	if (!ht || !peer || !al)
		return NULL;

	perm = mem_zalloc(sizeof(*perm), destructor);
	if (!perm)
		return NULL;

	hash_append(ht, sa_hash(peer, SA_ADDR), &perm->he, perm);

	perm->peer = *peer;
	perm->al = al;
	perm->expires = now + PERM_LIFETIME;
	perm->start = now;

	debug("turn: allocation %p permission %j created\n", al, peer);

	return perm;
}


void perm_refresh(struct perm *perm)
{
	if (!perm)
		return;

	perm->expires = time(NULL) + PERM_LIFETIME;
	debug("turn: allocation %p permission %j refreshed\n",
		      perm->al, &perm->peer);
}


void perm_tx_stat(struct perm *perm, size_t bytc)
{
	if (!perm)
		return;

	perm->ts.pktc_tx++;
	perm->ts.bytc_tx += bytc;
}


void perm_rx_stat(struct perm *perm, size_t bytc)
{
	if (!perm)
		return;

	perm->ts.pktc_rx++;
	perm->ts.bytc_rx += bytc;
}


int perm_hash_alloc(struct hash **ht, uint32_t bsize)
{
	return hash_alloc(ht, bsize);
}


static bool status_handler(struct le *le, void *arg)
{
	struct perm *perm = le->data;
	struct mbuf *mb = arg;

	(void)mbuf_printf(mb, " (%j %is relay %llu/%llu)", &perm->peer,
			  perm->expires - time(NULL),
			  perm->ts.pktc_tx, perm->ts.pktc_rx);

	return false;
}


void perm_status(struct hash *ht, struct mbuf *mb)
{
	if (!ht || !mb)
		return;

	(void)mbuf_printf(mb, "    permissions:");
	(void)hash_apply(ht, status_handler, mb);
	(void)mbuf_printf(mb, "\n");
}


static bool attrib_handler(const struct stun_attr *attr, void *arg)
{
	struct createperm *cp = arg;
	struct perm *perm;

	if (attr->type != STUN_ATTR_XOR_PEER_ADDR)
		return false;

	if (sa_af(&attr->v.xor_peer_addr) != sa_af(&cp->al->rel_addr)) {
		cp->af_mismatch = true;
		return true;
	}

	perm = perm_find(cp->al->perms, &attr->v.xor_peer_addr);
	if (!perm) {
		perm = perm_create(cp->al->perms, &attr->v.xor_peer_addr,
				   cp->al);
		if (!perm)
			return true;

		perm->new = true;
	}

	hash_unlink(&perm->he);
	list_append(&cp->perml, &perm->he, perm);

	return false;
}


static bool rollback_handler(struct le *le, void *arg)
{
	struct perm *perm = le->data;
	struct allocation *al = arg;

	list_unlink(&perm->he);

	if (perm->new)
		mem_deref(perm);
	else
		hash_append(al->perms, sa_hash(&perm->peer, SA_ADDR),
			    &perm->he, perm);

	return false;
}


static bool commit_handler(struct le *le, void *arg)
{
	struct perm *perm = le->data;
	struct allocation *al = arg;

	list_unlink(&perm->he);
	hash_append(al->perms, sa_hash(&perm->peer, SA_ADDR), &perm->he, perm);

	if (perm->new)
		perm->new = false;
	else
		perm_refresh(perm);

	return false;
}


void createperm_request(struct allocation *al, struct msgctx *ctx,
			int proto, void *sock, const struct sa *src,
			const struct stun_msg *msg)
{
	int err = ENOMEM, rerr;
	struct createperm cp;
	bool hfail;

	list_init(&cp.perml);
	cp.af_mismatch = false;
	cp.al = al;

	hfail = (NULL != stun_msg_attr_apply(msg, attrib_handler, &cp));
	if (cp.af_mismatch) {
		info("turn: creatperm peer address family mismatch\n");
		rerr = stun_ereply(proto, sock, src, 0, msg,
				   443, "Peer Address Family Mismatch",
				   ctx->key, ctx->keylen, ctx->fp, 1,
				   STUN_ATTR_SOFTWARE, turn_software);
		goto out;
	}
	else if (hfail) {
		info("turn: unable to create permission\n");
		rerr = stun_ereply(proto, sock, src, 0, msg,
				   500, "Server Error",
				   ctx->key, ctx->keylen, ctx->fp, 1,
				   STUN_ATTR_SOFTWARE, turn_software);
		goto out;
	}

	if (!cp.perml.head) {
		info("turn: no peer-addr attributes\n");
		rerr = stun_ereply(proto, sock, src, 0, msg,
				   400, "No Peer Attributes",
				   ctx->key, ctx->keylen, ctx->fp, 1,
				   STUN_ATTR_SOFTWARE, turn_software);
		goto out;
	}

	err = rerr = stun_reply(proto, sock, src, 0, msg,
				ctx->key, ctx->keylen, ctx->fp, 1,
				STUN_ATTR_SOFTWARE, turn_software);
 out:
	if (rerr)
		warning("turn: createperm reply: %m\n", rerr);

	if (err)
		(void)list_apply(&cp.perml, true, rollback_handler, al);
	else
		(void)list_apply(&cp.perml, true, commit_handler, al);
}
