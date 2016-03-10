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
#include "avs_log.h"
#include "avs_media.h"
#include "priv_mediaflow.h"


enum {
	FRESHNESS_TIMEOUT = 10,  /* seconds */
};


struct ice_lite {
	struct udp_helper *uh;
	struct udp_sock *us;
	struct tmr tmr_fresh;
	struct list rlitel;          /* remote candidates (struct lite_cand) */
	struct ice_cand_attr rcand;  /* selected candidate */
	uint64_t ts_last;
	char *ufrag;
	char *pwd;
	char *peer_software;
	ice_estab_h *estabh;
	ice_close_h *closeh;
	void *arg;
};


static int print_cand(struct re_printf *pf, const struct ice_cand_attr *cand)
{
	if (!cand)
		return 0;

	return re_hprintf(pf, "%s.%J",
			  ice_cand_type2name(cand->type), &cand->addr);
}


static void tmr_fresh_handler(void *arg)
{
	struct ice_lite *ice = arg;
	int64_t diff;

	tmr_start(&ice->tmr_fresh, 5000, tmr_fresh_handler, ice);

	diff = tmr_jiffies() - ice->ts_last;

	if (diff > (FRESHNESS_TIMEOUT*1000)) {

		info("mediaflow: no packets seen for %ld seconds - close it\n",
		     diff/1000);

		tmr_cancel(&ice->tmr_fresh);

		if (ice->closeh)
			ice->closeh(ETIMEDOUT, ice->arg);
	}
}


static void established(struct ice_lite *ice, struct ice_cand_attr *rcand)
{
	tmr_start(&ice->tmr_fresh, 5000, tmr_fresh_handler, ice);

	if (ice->estabh)
		ice->estabh(rcand, ice->arg);
}


static void handle_ice_lite(struct ice_lite *ice, struct stun_msg *msg,
			    struct sa *src)
{
	struct ice_cand_attr *rcand;
	bool update = false;

	if (!sa_cmp(&ice->rcand.addr, src, SA_ALL)) {
		info("mediaflow: lite: setting remote"
		     " address to %J"
		     " (from incoming STUN-packet) [%s]\n",
		     src, ice->peer_software);
		update = true;
	}

	ice->rcand.addr = *src;

	rcand = icelite_cand_find(ice, src);
	if (rcand) {
		info("mediaflow: lite: remote cand found (%H)\n",
		     print_cand, rcand);
		ice->rcand = *rcand;
	}
	else {
		info("mediaflow: lite: remote cand"
			" was NOT found! (%J)\n",
			src);
	}

	if (update)
		established(ice, &ice->rcand);
}


static int stunsrv_ereply(struct ice_lite *ice, const struct sa *src,
			  const struct stun_msg *req,
			  uint16_t scode, const char *reason)
{
	size_t presz = 0;

	warning("mediaflow: reply STUN error to %J (%u %s)\n",
		src, scode, reason);

	return stun_ereply(IPPROTO_UDP, ice->us, src, presz, req,
			   scode, reason,
			   (uint8_t *)ice->pwd, strlen(ice->pwd),
			   true, 0);
}


static bool udp_helper_recv_handler(struct sa *src, struct mbuf *mb,
				    void *arg)
{
	struct ice_lite *ice = arg;
	struct stun_unknown_attr ua;
	struct stun_msg *msg;
	struct stun_attr *attr;
	struct pl lu, ru;
	int err;

	/* refresh on any kinds of traffic */
	ice->ts_last = tmr_jiffies();

	if (0 != stun_msg_decode(&msg, mb, &ua))
		return false;  /* not handled */

	debug("mediaflow: recv STUN packet from %J\n", src);

	if (!mediaflow_got_sdp(ice->arg)) {
		info("mediaflow: icelite: dropping STUN %s %s from %J"
		     " (...still waiting for SDP)\n",
		     stun_method_name(stun_msg_method(msg)),
		     stun_class_name(stun_msg_class(msg)),
		     src);
		goto out;
	}

	switch (stun_msg_class(msg)) {

	case STUN_CLASS_REQUEST:

		/* check for sane STUN message */

		err = stun_msg_chk_mi(msg,
				      (uint8_t *)ice->pwd,
				      strlen(ice->pwd));
		if (err) {
			if (err == EBADMSG)
				goto unauth;
			else
				goto badmsg;
		}

		attr = stun_msg_attr(msg, STUN_ATTR_USERNAME);
		if (!attr) {
			warning("mediaflow: lite: missing USERNAME attr\n");
			goto badmsg;
		}

		err = re_regex(attr->v.username,
			       strlen(attr->v.username),
			       "[^:]+:[^]+", &lu, &ru);
		if (err) {
			warning("mediaflow: could not parse"
				" USERNAME attribute (%s)\n",
				attr->v.username);
			goto unauth;
		}
		if (pl_strcmp(&lu, ice->ufrag))
			goto unauth;

		/* NOTE: remote ufrag is not compared */

		attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
		if (attr && !ice->peer_software) {
			(void)str_dup(&ice->peer_software, attr->v.software);
		}

		err = stun_reply(IPPROTO_UDP, ice->us, src,
				 0, msg,
				 (void *)ice->pwd,
				 str_len(ice->pwd),
				 true, 2,
				 STUN_ATTR_XOR_MAPPED_ADDR, src,
				 STUN_ATTR_SOFTWARE, avs_software);
		if (err) {
			warning("mediaflow: stun_reply error: %m\n",
				err);
		}

		/* XXX: workaround for old clients in the fields that does not
		 *      check the ice-lite SDP attribute.
		 */
		if (stun_msg_attr(msg, STUN_ATTR_USE_CAND)) {
			handle_ice_lite(ice, msg, src);
		}
		else {
			info("mediaflow: handle_ice_lite (USE_CAND=no)\n");
			handle_ice_lite(ice, msg, src);
		}
		break;

	default:
		break;
	}

 out:
	mem_deref(msg);

	return true;  /* handled */

 badmsg:
	stunsrv_ereply(ice, src, msg, 400, "Bad Request");
	mem_deref(msg);
	return true;

 unauth:
	stunsrv_ereply(ice, src, msg, 401, "Unauthorized");
	mem_deref(msg);
	return true;
}


static void destructor(void *arg)
{
	struct ice_lite *ice = arg;

	list_flush(&ice->rlitel);
	tmr_cancel(&ice->tmr_fresh);
	mem_deref(ice->uh);
	mem_deref(ice->us);
	mem_deref(ice->ufrag);
	mem_deref(ice->pwd);
	mem_deref(ice->peer_software);
}


int icelite_alloc(struct ice_lite **icep, struct udp_sock *us,
		  const char *ufrag, const char *pwd,
		  ice_estab_h *estabh, ice_close_h *closeh, void *arg)
{
	struct ice_lite *ice;
	int err = 0;

	if (!icep || !us)
		return EINVAL;

	ice = mem_zalloc(sizeof(*ice), destructor);
	if (!ice)
		return ENOMEM;

	tmr_init(&ice->tmr_fresh);
	ice->rcand.type = (enum ice_cand_type)-1;

	err  = str_dup(&ice->ufrag, ufrag);
	err |= str_dup(&ice->pwd, pwd);
	if (err)
		goto out;

	ice->us = mem_ref(us);
	err = udp_register_helper(&ice->uh, us, LAYER_ICE, NULL,
				  udp_helper_recv_handler, ice);
	if (err)
		goto out;

	ice->estabh = estabh;
	ice->closeh = closeh;
	ice->arg    = arg;

 out:
	if (err)
		mem_deref(ice);
	else
		*icep = ice;

	return err;
}


static void litecand_destructor(void *arg)
{
	struct lite_cand *lc = arg;

	list_unlink(&lc->le);
}


int icelite_cand_add(struct ice_lite *ice, const struct ice_cand_attr *cand)
{
	struct lite_cand *lc;

	if (!ice || !cand)
		return EINVAL;

	// todo: lookup in list, update duplicates

	lc = mem_zalloc(sizeof(*lc), litecand_destructor);
	if (!lc)
		return ENOMEM;

	lc->cand = *cand;
	list_append(&ice->rlitel, &lc->le, lc);

	return 0;
}


/* lookup "addr" in list of remote Lite-candidates */
struct ice_cand_attr *icelite_cand_find(const struct ice_lite *ice,
					const struct sa *addr)
{
	struct le *le;

	if (!ice || !addr)
		return NULL;

	for (le = ice->rlitel.head; le; le = le->next) {
		struct lite_cand *lc = le->data;

		if (sa_cmp(&lc->cand.addr, addr, SA_ALL)) {
			return &lc->cand;
		}
	}

	return NULL;
}


struct list *icelite_rcandl(struct ice_lite *ice)
{
	return ice ? &ice->rlitel : NULL;
}


int icelite_debug(struct re_printf *pf, const struct ice_lite *ice)
{
	struct le *le;
	int err = 0;

	if (!ice)
		return 0;

	err |= re_hprintf(pf, "peer_software:       %s\n", ice->peer_software);

	for (le = ice->rlitel.head; le; le = le->next) {
		struct lite_cand *cand = le->data;
		err |= re_hprintf(pf, "...remote=%H\n",
				  print_cand, &cand->cand);
	}

	return err;
}
