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
	CONSENT_FIRST_INTERVAL = 50,    /* milliseconds */
	CONSENT_INTERVAL = 5000,    /* milliseconds */
	MAX_RETRIES = 4,
};


struct consent {
	struct tmr tmr;
	struct stun *stun;
	struct stun_ctrans *ct;
	struct udp_sock *us;
	struct sa raddr;
	size_t presz;
	char *lufrag;
	char *rufrag;
	char *rpwd;
	uint64_t ts_sent;
	unsigned n_tries;
	bool controlling;
	bool closed;
	consent_close_h *closeh;
	void *arg;
};


static void tmr_consent_handler(void *arg);


static void consent_stun_resp_handler(int err,
				      uint16_t scode, const char *reason,
				      const struct stun_msg *msg, void *arg)
{
	struct consent *con = arg;
	int diff = tmr_jiffies() - con->ts_sent;
	(void)reason;

	debug("mediaflow: recv consent response (%d %u) [%d milliseconds].\n",
	      err, scode, diff);

	if (con->closed)
		return;

	con->n_tries = 0;

	if (err) {
		warning("mediaflow: consent request failed (%m)\n", err);
		goto out;
	}

	switch (scode) {

	case 0: /* Success case */
		tmr_start(&con->tmr, CONSENT_INTERVAL,
			  tmr_consent_handler, con);
		break;

	default:
		warning("mediaflow: consent request failed --"
			" STUN Response: %u %s\n",
			scode, reason);
		err = EPROTO;
		break;
	}

 out:
	if (err) {
		con->closed = true;

		if (con->closeh)
			con->closeh(err, con->arg);
	}
}


/* Send consent STUN Binding Request to Peer */
static int send_consent(struct consent *con)
{
	char username[256];
	uint64_t tiebrk = 42ULL;
	uint32_t prio;
	uint16_t ctrl_attr;
	bool use_cand = con->controlling;
	int err = 0;

	if (re_snprintf(username, sizeof(username),
			"%s:%s", con->rufrag, con->lufrag) < 0) {
		err = ENOMEM;
		goto out;
	}

	prio = ice_cand_calc_prio(ICE_CAND_TYPE_PRFLX, 0, 1);

	if (con->controlling)
		ctrl_attr = STUN_ATTR_CONTROLLING;
	else
		ctrl_attr = STUN_ATTR_CONTROLLED;

	err = stun_request(&con->ct, con->stun, IPPROTO_UDP,
			   con->us, &con->raddr, con->presz,
			   STUN_METHOD_BINDING,
			   (uint8_t *)con->rpwd, str_len(con->rpwd),
			   true, consent_stun_resp_handler, con,
			   4,
			   STUN_ATTR_USERNAME, username,
			   STUN_ATTR_PRIORITY, &prio,
			   ctrl_attr, &tiebrk,
			   STUN_ATTR_USE_CAND, use_cand ? &use_cand : NULL);
	if (err) {
		warning("mediaflow: stun_request(%J) failed (%m)\n",
			&con->raddr, err);
		goto out;
	}

	con->ts_sent = tmr_jiffies();

 out:
	return err;
}


static void tmr_consent_handler(void *arg)
{
	struct consent *con = arg;
	int err;

	debug("mediaflow: [presz=%zu] send consent to %J..\n",
	      con->presz, &con->raddr);

	++con->n_tries;

	err = send_consent(con);
	if (err) {

		/* Too many tries */
		if (con->n_tries > MAX_RETRIES) {
			con->closed = true;

			if (con->closeh)
				con->closeh(err, con->arg);
		}
		/* Try again */
		else {
			info("mediaflow: consent timer, trying again"
			     " (n_tries=%u)\n", con->n_tries);
			tmr_start(&con->tmr, CONSENT_INTERVAL,
				  tmr_consent_handler, con);
		}
	}
}


static void destructor(void *arg)
{
	struct consent *con = arg;

	tmr_cancel(&con->tmr);
	mem_deref(con->ct);    /* NOTE: must be deref'd before 'stun' */
	mem_deref(con->stun);
	mem_deref(con->us);
	mem_deref(con->lufrag);
	mem_deref(con->rufrag);
	mem_deref(con->rpwd);
}


int consent_start(struct consent **conp, struct stun *stun,
		  struct udp_sock *us, bool controlling,
		  const char *lufrag,
		  const char *rufrag, const char *rpwd,
		  size_t presz, const struct sa *raddr,
		  consent_close_h *closeh, void *arg)
{
	struct consent *con;
	int err = 0;

	if (!conp || !us || !sa_isset(raddr, SA_ALL))
		return EINVAL;

	con = mem_zalloc(sizeof(*con), destructor);
	if (!con)
		return ENOMEM;

	tmr_init(&con->tmr);

	con->stun = mem_ref(stun);
	con->us = mem_ref(us);
	con->controlling = controlling;
	err |= str_dup(&con->lufrag, lufrag);
	err |= str_dup(&con->rufrag, rufrag);
	err |= str_dup(&con->rpwd, rpwd);
	if (err)
		goto out;

	con->presz = presz;
	con->raddr = *raddr;

	con->closeh = closeh;
	con->arg = arg;

	tmr_start(&con->tmr, CONSENT_FIRST_INTERVAL, tmr_consent_handler, con);

 out:
	if (err)
		mem_deref(con);
	else
		*conp = con;

	return err;
}
