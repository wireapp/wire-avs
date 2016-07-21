/**
 * @file icem.c  ICE Media stream
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re_types.h>
#include <re_fmt.h>
#include <re_mem.h>
#include <re_mbuf.h>
#include <re_list.h>
#include <re_tmr.h>
#include <re_sa.h>
#include <re_stun.h>
#include <re_ice.h>
#include <re_sys.h>
#include <re_trice.h>
#include "trice.h"


#define DEBUG_MODULE "icem"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static const struct trice_conf conf_default = {
	false,
	false,
	false,
	true
};


static void trice_destructor(void *data)
{
	struct trice *icem = data;

	mem_deref(icem->checklist);

	list_flush(&icem->validl);
	list_flush(&icem->checkl);
	list_flush(&icem->lcandl);
	list_flush(&icem->rcandl);

	list_flush(&icem->connl);

	mem_deref(icem->rufrag);
	mem_deref(icem->rpwd);
	mem_deref(icem->lufrag);
	mem_deref(icem->lpwd);
	mem_deref(icem->sw);
}


/**
 * Allocate a new ICE Media object
 *
 * @param icemp       Pointer to allocated ICE Media object
 * @param conf        ICE configuration
 * @param controlling True for controlling role, false for controlled
 * @param lufrag      Local username fragment
 * @param lpwd        Local password
 * @param estabh      Candidate pair established handler
 * @param closeh      Close / error handler
 * @param arg         Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */

int trice_alloc(struct trice **icemp, const struct trice_conf *conf,
	       bool controlling,
	       const char *lufrag, const char *lpwd)
{
	struct trice *icem;
	int err = 0;

	if (!icemp || !lufrag || !lpwd)
		return EINVAL;

	if (str_len(lufrag) < 4 || str_len(lpwd) < 22) {
		DEBUG_WARNING("alloc: lufrag/lpwd is too short\n");
		return EINVAL;
	}

	icem = mem_zalloc(sizeof(*icem), trice_destructor);
	if (!icem)
		return ENOMEM;

	icem->conf = conf ? *conf : conf_default;
	list_init(&icem->lcandl);
	list_init(&icem->rcandl);
	list_init(&icem->checkl);
	list_init(&icem->validl);

	icem->controlling = controlling;
	icem->tiebrk = rand_u64();


	err |= str_dup(&icem->lufrag, lufrag);
	err |= str_dup(&icem->lpwd, lpwd);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(icem);
	else
		*icemp = icem;

	return err;
}


int trice_set_remote_ufrag(struct trice *icem, const char *rufrag)
{
	if (!icem || !rufrag)
		return EINVAL;

	icem->rufrag = mem_deref(icem->rufrag);
	return str_dup(&icem->rufrag, rufrag);
}


int trice_set_remote_pwd(struct trice *icem, const char *rpwd)
{
	if (!icem || !rpwd)
		return EINVAL;

	icem->rpwd = mem_deref(icem->rpwd);

	return str_dup(&icem->rpwd, rpwd);
}


int trice_set_software(struct trice *icem, const char *sw)
{
	if (!icem)
		return EINVAL;

	icem->sw = mem_deref(icem->sw);

	if (sw)
		return str_dup(&icem->sw, sw);

	return 0;
}


struct trice_conf *trice_conf(struct trice *icem)
{
	return icem ? &icem->conf : NULL;
}


void trice_set_controlling(struct trice *trice, bool controlling)
{
	if (!trice)
		return;

	trice->controlling = controlling;
}


bool trice_is_controlling(const struct trice *icem)
{
	return icem ? icem->controlling : false;
}


/**
 * Print debug information for the ICE Media
 *
 * @param pf   Print function for debug output
 * @param icem ICE Media object
 *
 * @return 0 if success, otherwise errorcode
 */
int trice_debug(struct re_printf *pf, const struct trice *icem)
{
	struct le *le;
	int err = 0;

	if (!icem)
		return 0;

	err |= re_hprintf(pf, "----- ICE Media <%p> -----\n", icem);

	err |= re_hprintf(pf, " local_role=Controll%s\n",
			  icem->controlling ? "ing" : "ed");
	err |= re_hprintf(pf, " local_ufrag=\"%s\" local_pwd=\"%s\"\n",
			  icem->lufrag, icem->lpwd);

	err |= re_hprintf(pf, " Local Candidates: %H",
			  trice_lcands_debug, &icem->lcandl);
	err |= re_hprintf(pf, " Remote Candidates: %H",
			  trice_rcands_debug, &icem->rcandl);
	err |= re_hprintf(pf, " Check list: ");
	err |= trice_candpairs_debug(pf, icem->conf.ansi, &icem->checkl);

	err |= re_hprintf(pf, " Valid list: ");
	err |= trice_candpairs_debug(pf, icem->conf.ansi, &icem->validl);

	if (icem->checklist)
		err |= trice_checklist_debug(pf, icem->checklist);

	err |= re_hprintf(pf, " TCP Connections: (%u)\n",
			  list_count(&icem->connl));

	for (le = list_head(&icem->connl); le; le = le->next) {
		struct ice_tcpconn *conn = le->data;

		err |= re_hprintf(pf, "      %H\n",
				  trice_conn_debug, conn);
	}

	return err;
}


/**
 * Get the list of Local Candidates (struct cand)
 *
 * @param icem ICE Media object
 *
 * @return List of Local Candidates
 */
struct list *trice_lcandl(const struct trice *icem)
{
	return icem ? (struct list *)&icem->lcandl : NULL;
}


/**
 * Get the list of Remote Candidates (struct cand)
 *
 * @param icem ICE Media object
 *
 * @return List of Remote Candidates
 */
struct list *trice_rcandl(const struct trice *icem)
{
	return icem ? (struct list *)&icem->rcandl : NULL;
}


/**
 * Get the checklist of Candidate Pairs
 *
 * @param icem ICE Media object
 *
 * @return Checklist (struct ice_candpair)
 */
struct list *trice_checkl(const struct trice *icem)
{
	return icem ? (struct list *)&icem->checkl : NULL;
}


/**
 * Get the list of valid Candidate Pairs
 *
 * @param icem ICE Media object
 *
 * @return Validlist (struct ice_candpair)
 */
struct list *trice_validl(const struct trice *icem)
{
	return icem ? (struct list *)&icem->validl : NULL;
}


void trice_printf(struct trice *icem, const char *fmt, ...)
{
	va_list ap;

	if (!icem || !icem->conf.debug)
		return;

	va_start(ap, fmt);
	(void)re_printf("%v", fmt, &ap);
	va_end(ap);
}


void trice_tracef(struct trice *icem, int color, const char *fmt, ...)
{
	va_list ap;

	if (!icem || !icem->conf.trace)
		return;

	if (icem->conf.ansi && color) {
		re_printf("\x1b[%dm", color);
	}

	va_start(ap, fmt);
	(void)re_printf("%v", fmt, &ap);
	va_end(ap);

	if (icem->conf.ansi && color) {
		re_printf("\x1b[;m");
	}
}


void trice_switch_local_role(struct trice *ice)
{
	if (!ice)
		return;

	ice->controlling = !ice->controlling;

	/* recompute pair priorities for all media streams */
	trice_candpair_prio_order(&ice->checkl, ice->controlling);
}


/* sock = [ struct udp_sock | struct tcp_conn ] */
bool trice_stun_process(struct trice *icem, struct ice_lcand *lcand,
		       int proto, void *sock, const struct sa *src,
		       struct mbuf *mb)
{
	struct stun_msg *msg = NULL;
	struct stun_unknown_attr ua;
	size_t start = mb->pos;
	(void)proto;

	if (stun_msg_decode(&msg, mb, &ua)) {
		return false;  /* continue recv-processing */
	}

	if (STUN_METHOD_BINDING == stun_msg_method(msg)) {

		switch (stun_msg_class(msg)) {

		case STUN_CLASS_REQUEST:
			(void)trice_stund_recv(icem, lcand, sock,
					      src, msg, start);
			break;

		default:
			if (icem->checklist) {
				(void)stun_ctrans_recv(icem->checklist->stun,
						       msg, &ua);
			}
			else {
				DEBUG_NOTICE("STUN resp from %J dropped"
					     " (no checklist)\n",
					     src);
			}
			break;
		}
	}

	mem_deref(msg);

	return true;
}
