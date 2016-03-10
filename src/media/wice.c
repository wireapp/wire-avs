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
#include <pthread.h>
#include <re.h>
#include "avs_log.h"
#include "avs_media.h"
#include "priv_mediaflow.h"


#define ICE_DEFAULT_Tr 15


static int stunsrv_ereply(struct wice *ice, const struct sa *src,
			  size_t presz, const struct stun_msg *req,
			  uint16_t scode, const char *reason)
{
	if (!ice)
		return EINVAL;

	info("mediaflow: reply STUN error to %J (%u %s)\n",
	     src, scode, reason);

	return stun_ereply(IPPROTO_UDP, ice->us, src, presz, req,
			   scode, reason,
			   (uint8_t *)ice->pwd, strlen(ice->pwd),
			   true, 0);
}


static bool udp_helper_recv_handler_ice(struct sa *src, struct mbuf *mb,
					void *arg)
{
	struct wice *ice = arg;
	struct stun_unknown_attr ua;
	struct stun_msg *msg;
	struct stun_attr *attr;
	size_t presz = mb->pos;  /* Offset for TURN Channel/Header */
	const char *transp = "strange transport?";
	int err;

	if      (mb->pos == 0)  transp = "socket";
	else if (mb->pos == 4)  transp = "TURN Channel";
	else if (mb->pos == 36) transp = "STUN Indication";

	if (0 == stun_msg_decode(&msg, mb, &ua)) {

		struct pl lu, ru;
		int rcontrolling = -1;

#if 1
		debug("mediaflow: helper recv %zu bytes (%zu:%zu) from %J"
		      " via '%s'\n",
		      mbuf_get_left(mb), mb->pos, mb->end, src, transp);
#endif

		debug("mediaflow: recv STUN packet from %J\n", src);

		switch (stun_msg_class(msg)) {

		case STUN_CLASS_REQUEST:

			/* check for sane STUN message */

			err = stun_msg_chk_mi(msg,
					      (uint8_t *)ice->pwd,
					      strlen(ice->pwd));
			if (err) {
				info("mediaflow: stun: MI mismatch"
				     " from %J (%m)\n",
				     src, err);

				if (err == EBADMSG)
					goto unauth;
				else
					goto badmsg;
			}

			attr = stun_msg_attr(msg, STUN_ATTR_USERNAME);
			if (!attr)
				goto badmsg;

			err = re_regex(attr->v.username,
				       strlen(attr->v.username),
				       "[^:]+:[^]+", &lu, &ru);
			if (err) {
				warning("mediaflow: could not parse"
					" USERNAME attribute (%s)\n",
					attr->v.username);
				goto unauth;
			}
			if (pl_strcmp(&lu, ice->ufrag)) {
				info("mediaflow: stun: ufrag mismatch"
				     " from %J (local=%s, them=%r)\n",
				     src, ice->ufrag, &lu);
				goto unauth;
			}

#if 0
			rufrag = sdp_rattr(mf->sdp, mf->sdpm, "ice-ufrag");
			if (rufrag && pl_strcmp(&ru, rufrag)) {
				warning("mediaflow: remote ufrag mismatch"
					" (sdp=%s, stun=%r)\n",
					rufrag, &ru);
				goto unauth;
			}
#endif


			if (stun_msg_attr(msg, STUN_ATTR_CONTROLLED)) {
				rcontrolling = false;
			}

			if (stun_msg_attr(msg, STUN_ATTR_CONTROLLING)) {
				rcontrolling = true;
			}

			if (ice->lcontrolling == rcontrolling) {
				warning("mediaflow: wice: both local"
					" and remote"
					" is Controll%s\n",
					rcontrolling ? "ing" : "ed");
			}

			err = stun_reply(IPPROTO_UDP, ice->us, src,
					 presz, msg,
					 (void *)ice->pwd,
					 str_len(ice->pwd),
					 true, 2,
					 STUN_ATTR_XOR_MAPPED_ADDR, src,
					 STUN_ATTR_SOFTWARE, avs_software);
			if (err) {
				warning("mediaflow: stun_reply error: %m\n",
					err);
			}

			if (ice->reqh)
				ice->reqh(msg, src, presz>0, ice->arg);
			break;

		default:
			(void)stun_ctrans_recv(ice->stun, msg, &ua);
			break;
		}

		mem_deref(msg);

		return true;
	}

	return false;

 badmsg:
	stunsrv_ereply(ice, src, presz, msg, 400, "Bad Request");
	mem_deref(msg);
	return true;

 unauth:
	stunsrv_ereply(ice, src, presz, msg, 401, "Unauthorized");
	mem_deref(msg);
	return true;
}


static void cand_destructor(void *arg)
{
	struct media_candidate *cand = arg;

	tmr_cancel(&cand->tmr);
	list_unlink(&cand->le);
	mem_deref(cand->ct);
}


static void destructor(void *arg)
{
	struct wice *ice = arg;

	list_flush(&ice->rcandl);
	mem_deref(ice->uh);
	mem_deref(ice->us);
	mem_deref(ice->ufrag);
	mem_deref(ice->pwd);
	mem_deref(ice->stun);
}


int wice_alloc(struct wice **wicep, struct udp_sock *us,
	       const char *ufrag, const char *pwd,
	       ice_request_h *reqh, void *arg)
{
	struct wice *ice;
	int err = 0;

	if (!wicep || !us)
		return EINVAL;

	ice = mem_zalloc(sizeof(*ice), destructor);
	if (!ice)
		return ENOMEM;

	err = stun_alloc(&ice->stun, NULL, NULL, NULL);
	if (err)
		goto out;

	/*
	 * tuning the STUN transaction values
	 *
	 * RTO=160 and RC=8 gives around 22 seconds timeout
	 */
	stun_conf(ice->stun)->rto = 160;  /* milliseconds */
	stun_conf(ice->stun)->rc =    8;  /* number of retransmits */

	err  = str_dup(&ice->ufrag, ufrag);
	err |= str_dup(&ice->pwd, pwd);
	if (err)
		goto out;

	ice->us = mem_ref(us);
	err = udp_register_helper(&ice->uh, us, LAYER_ICE, NULL,
				  udp_helper_recv_handler_ice, ice);
	if (err)
		goto out;

	ice->reqh = reqh;
	ice->arg = arg;

 out:
	if (err)
		mem_deref(ice);
	else
		*wicep = ice;

	return err;
}


/* Keepalive is needed to keep PIN-holes in NATs open */
static void timeout(void *arg)
{
	struct media_candidate *mc = arg;

	tmr_start(&mc->tmr, ICE_DEFAULT_Tr * 1000 + rand_u16() % 1000,
                  timeout, mc);

	if (mc->err == 0) {

		size_t presz;

		presz = mc->local_relay ? 36 : 0;

		stun_indication(mc->rcand.proto, mc->ice->us,
				&mc->rcand.addr, presz,
				STUN_METHOD_BINDING, NULL, 0, true, 0);
	}
}


int wice_rcand_add(struct media_candidate **candp, struct wice *ice,
		   const struct ice_cand_attr *rcand, bool local_relay)
{
	struct media_candidate *cand;

	if (!ice || !rcand)
		return EINVAL;

	cand = mem_zalloc(sizeof(*cand), cand_destructor);
	if (!cand)
		return ENOMEM;

	list_append(&ice->rcandl, &cand->le, cand);

	cand->ice   = ice;
	cand->err   = -1;
	cand->rcand = *rcand;
	cand->local_relay = local_relay;

	tmr_start(&cand->tmr, ICE_DEFAULT_Tr * 1000, timeout, cand);

	if (candp)
		*candp = cand;

	return 0;
}


/* lookup "addr" in list of remote candidates */
struct media_candidate *wice_rcand_find(const struct wice *ice,
					const struct sa *addr)
{
	struct le *le;

	if (!ice || !addr)
		return NULL;

	for (le = ice->rcandl.head; le; le = le->next) {

		struct media_candidate *mc = le->data;

		if (sa_cmp(&mc->rcand.addr, addr, SA_ALL))
			return mc;
	}

	return NULL;
}


bool wice_remote_cands_has_type(const struct wice *ice,
				enum ice_cand_type type)
{
	struct le *le;

	if (!ice)
		return false;

	for (le = ice->rcandl.head; le; le = le->next) {

		struct media_candidate *mc = le->data;

		if (mc->rcand.type == type)
			return true;
	}

	return false;
}


static int print_cand(struct re_printf *pf, const struct ice_cand_attr *cand)
{
	if (!cand)
		return 0;

	return re_hprintf(pf, "%s.%J",
			  ice_cand_type2name(cand->type), &cand->addr);
}


static int print_errno(struct re_printf *pf, int err)
{
	if (err == -1)
		return re_hprintf(pf, "Progress..");
	else if (err)
		return re_hprintf(pf, "%m", err);
	else
		return re_hprintf(pf, "Success");
}


int wice_debug(struct re_printf *pf, const struct wice *ice)
{
	struct le *le;
	int err = 0;

	if (!ice)
		return 0;

	for (le = ice->rcandl.head; le; le = le->next) {
		struct media_candidate *cand = le->data;
		err |= re_hprintf(pf, "...[%c%c%c%c] remote=%H (%H)\n",
				  cand->triggered   ? 'T' : ' ',
				  cand->local_relay ? 'R' : ' ',
				  cand->incoming    ? 'I' : ' ',
				  cand->check_sent  ? 'C' : ' ',
				  print_cand, &cand->rcand,
				  print_errno, cand->err);
	}

	return err;
}
