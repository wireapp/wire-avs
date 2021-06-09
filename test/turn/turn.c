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

#include <string.h>
#include <re.h>
#include <avs.h>
#include "turn.h"


enum {
	ALLOC_DEFAULT_BSIZE = 512,
};


struct tuple {
	const struct sa *cli_addr;
	const struct sa *srv_addr;
	int proto;
};


const char *turn_software = "ztest v" AVS_VERSION " (" ARCH "/" OS ")";


static bool hash_cmp_handler(struct le *le, void *arg)
{
	const struct allocation *al = le->data;
	const struct tuple *tup = arg;

	if (!sa_cmp(&al->cli_addr, tup->cli_addr, SA_ALL))
		return false;

	if (!sa_cmp(&al->srv_addr, tup->srv_addr, SA_ALL))
		return false;

	if (al->proto != tup->proto)
		return false;

	return true;
}


static struct allocation *allocation_find(const struct turnd *turnd,
					  int proto, const struct sa *src,
					  const struct sa *dst)
{
	struct tuple tup;

	tup.cli_addr = src;
	tup.srv_addr = dst;
	tup.proto = proto;

	return list_ledata(hash_lookup(turnd->ht_alloc, sa_hash(src, SA_ALL),
				       hash_cmp_handler, &tup));
}


bool turn_request_handler(struct msgctx *ctx, int proto, void *sock,
		     const struct sa *src, const struct sa *dst,
		     const struct stun_msg *msg)
{
	const uint16_t met = stun_msg_method(msg);
	struct allocation *al;
	int err = 0;

	switch (met) {

	case STUN_METHOD_ALLOCATE:
	case STUN_METHOD_REFRESH:
	case STUN_METHOD_CREATEPERM:
	case STUN_METHOD_CHANBIND:
		break;

	default:
		return false;
	}

	if (ctx->turnd->sim_error != 0) {

			err = stun_ereply(proto, sock, src, 0, msg,
					  ctx->turnd->sim_error,
					  "Simulated Error",
					  ctx->key, ctx->keylen, ctx->fp, 1,
					  STUN_ATTR_SOFTWARE,turn_software);
			goto out;
	}


	if (ctx->ua.typec > 0) {
		err = stun_ereply(proto, sock, src, 0, msg,
				  420, "Unknown Attribute",
				  ctx->key, ctx->keylen, ctx->fp, 2,
				  STUN_ATTR_UNKNOWN_ATTR, &ctx->ua,
				  STUN_ATTR_SOFTWARE, turn_software);
		goto out;
	}

	al = allocation_find(ctx->turnd, proto, src, dst);

	if (!al && met != STUN_METHOD_ALLOCATE) {
		debug("turn: allocation does not exist\n");
		err = stun_ereply(proto, sock, src, 0, msg,
				  437, "Allocation Mismatch",
				  ctx->key, ctx->keylen, ctx->fp, 1,
				  STUN_ATTR_SOFTWARE, turn_software);
		goto out;
	}

	if (al && al->username && ctx->key) {

		struct stun_attr *usr = stun_msg_attr(msg, STUN_ATTR_USERNAME);

		if (!usr || strcmp(usr->v.username, al->username)) {
			debug("turn: wrong credetials\n");
			err = stun_ereply(proto, sock, src, 0, msg,
					  441, "Wrong Credentials",
					  ctx->key, ctx->keylen, ctx->fp, 1,
					  STUN_ATTR_SOFTWARE,turn_software);
			goto out;
		}
	}

	switch (met) {

	case STUN_METHOD_ALLOCATE:
		allocate_request(ctx->turnd, al, ctx, proto, sock,
				 src, dst, msg);
		break;

	case STUN_METHOD_REFRESH:
		refresh_request(ctx->turnd, al, ctx, proto, sock, src, msg);
		break;

	case STUN_METHOD_CREATEPERM:
		createperm_request(al, ctx, proto, sock, src, msg);
		break;

	case STUN_METHOD_CHANBIND:
		chanbind_request(al, ctx, proto, sock, src, msg);
		break;
	}

 out:
	if (err) {
		warning("turn reply error: %m\n", err);
	}

	return true;
}


bool turn_indication_handler(struct msgctx *ctx, int proto,
			     void *sock, const struct sa *src,
			     const struct sa *dst,
			     const struct stun_msg *msg)
{
	struct stun_attr *data, *peer;
	struct allocation *al;
	struct perm *perm;
	int err;
	(void)sock;
	(void)ctx;

	if (stun_msg_method(msg) != STUN_METHOD_SEND)
		return false;

	if (ctx->ua.typec > 0)
		return true;

	al = allocation_find(ctx->turnd, proto, src, dst);
	if (!al)
		return true;

	peer = stun_msg_attr(msg, STUN_ATTR_XOR_PEER_ADDR);
	data = stun_msg_attr(msg, STUN_ATTR_DATA);

	if (!peer || !data)
		return true;

	perm = perm_find(al->perms, &peer->v.xor_peer_addr);
	if (!perm) {
		re_printf("@@@ packet to %J dropped (no permission) @@@\n",
			  &peer->v.xor_peer_addr);
		++al->dropc_tx;
		return true;
	}

	err = udp_send(al->rel_us, &peer->v.xor_peer_addr, &data->v.data);
	if (err)
		;
	else {
		const size_t bytes = mbuf_get_left(&data->v.data);

		perm_tx_stat(perm, bytes);
	}

	return true;
}


bool turn_raw_handler(struct turnd *turnd, int proto, const struct sa *src,
		      const struct sa *dst, struct mbuf *mb)
{
	struct allocation *al;
	uint16_t numb, len;
	struct perm *perm;
	struct chan *chan;
	int err;

	al = allocation_find(turnd, proto, src, dst);
	if (!al)
		return false;

	if (mbuf_get_left(mb) < 4)
		return false;

	numb = ntohs(mbuf_read_u16(mb));
	len  = ntohs(mbuf_read_u16(mb));

	if (mbuf_get_left(mb) < len)
		return false;

	chan = chan_numb_find(al->chans, numb);
	if (!chan)
		return false;

	perm = perm_find(al->perms, chan_peer(chan));
	if (!perm) {
		++al->dropc_tx;
		return false;
	}

	err = udp_send(al->rel_us, chan_peer(chan), mb);
	if (err)
		;
	else {
		const size_t bytes = mbuf_get_left(mb);

		perm_tx_stat(perm, bytes);
	}

	return true;
}
