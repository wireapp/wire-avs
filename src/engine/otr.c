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

#include <strings.h>
#include <re.h>
#include <avs.h>
#include "message.h"

#define MAX_CLIENTS 8
#define MAX_ID_LEN 64

struct context {
	struct engine *engine;
#ifdef HAVE_CRYPTOBOX
	struct cryptobox *cb;
#endif
	struct engine_conv *conv;    /* pointer */
	struct list userl;           /* list of users */
	struct engine_user *self;    /* pointer */

	char target_userid[MAX_ID_LEN];
	char target_clientid[MAX_ID_LEN];
	char local_clientid[MAX_ID_LEN];

	bool waiting_for_otr;
	size_t missing_prekeys;
	int32_t retries;

	struct list msgl;
	uint8_t *data;
	size_t data_len;
	bool transient;
	bool ignore_missing;
	otr_resp_h *resph;
	void *arg;
};

struct ctx_user {
	char userid[MAX_ID_LEN];
	char clientid[MAX_CLIENTS][MAX_ID_LEN];
	size_t client_num;
	struct le le;		/* element in context userl */
	struct context *ctx;
	struct prekey_handler pkh;
};

static int send_otr_if_ready(struct context *ctx, bool ignore_missing);


static void context_destructor(void *data)
{
	struct context *ctx = data;

	list_flush(&ctx->userl);
	list_flush(&ctx->msgl);
	mem_deref(ctx->data);
}

#ifdef HAVE_CRYPTOBOX
static void prekey_handler(const char *userid,
			   const uint8_t *key, size_t key_len,
			   uint16_t id, const char *clientid,
			   bool last, void *arg)
{
	struct ctx_user *cu = arg;
	struct context *ctx = cu->ctx;
	int err;

	userid = cu->userid;
	info("OTR(%p) prekey_handler: %zu bytes, user:%s[%u] -> %s\n",
	       ctx, key_len, userid, id, clientid);

	struct session *sess;

	sess = cryptobox_session_find(ctx->cb, userid, clientid, ctx->local_clientid);
	if (sess) {
		info("prekey: session found\n");
	}
	else {
		info("conv: adding key to cryptobox for clientid=%s\n",
		     clientid);

		err = cryptobox_session_add_send(ctx->cb, userid, clientid, ctx->local_clientid,
						 key, key_len);
		if (err) {
			warning("cryptobox_session_add_send failed (%m)\n",
				err);
		}
		ctx->missing_prekeys--;
	}
	send_otr_if_ready(ctx, ctx->ignore_missing);
}
#endif

static void otr_missing_handler(const char *userid, const char *clientid, void *arg)
{
	struct context *ctx = arg;
	struct ctx_user *cu;
	struct le *ule;

	// Dont add clients for non targeted users
	if (str_isset(ctx->target_userid) && 0 != str_casecmp(userid, ctx->target_userid)) {
		return;
	}

	// Dont add non targeted clients
	if (str_isset(ctx->target_clientid) && 0 != str_casecmp(clientid, ctx->target_clientid)) {
		return;
	}

	ule = list_head(&ctx->userl);
	while (ule) {
		cu = ule->data;

		if (0 == str_casecmp(userid, cu->userid)) {
			break;
		}
		ule = ule->next;
	}

	if (!ule) {
		cu = mem_zalloc(sizeof(*cu), NULL);
		if (!cu) {
			return;
		}
		cu->ctx = ctx;
		str_ncpy(cu->userid, userid, MAX_ID_LEN);
		list_append(&ctx->userl, &cu->le, cu);
	}

	if (cu->client_num < MAX_CLIENTS) {
		str_ncpy(cu->clientid[cu->client_num], clientid, MAX_ID_LEN);
		cu->client_num++;
	}

#ifdef HAVE_CRYPTOBOX
	if (!cryptobox_session_find(ctx->cb, userid, clientid, ctx->local_clientid)) {
		ctx->missing_prekeys++;
		cu->pkh.prekeyh = prekey_handler;
		cu->pkh.arg = cu;

		info("OTR(%p) getting prekeys for %s.%s\n", ctx, userid, clientid);
		int err = engine_get_client_prekeys(ctx->engine, userid, clientid, &cu->pkh);
		if (err) {
			warning("engine_get_client_prekeys failed (%m)\n", err);
		}
	}
#endif
}

static void otr_response_handler(int err, void *arg)
{
	struct context *ctx = arg;

	debug("OTR(%p) response: (%m)\n", ctx, err);
	ctx->waiting_for_otr = false;
	if (err == EAGAIN) {
		send_otr_if_ready(ctx, ctx->ignore_missing);
		return;
	}

	if (ctx->resph)
		ctx->resph(err, ctx->arg);

	mem_deref(ctx);
}


static int encrypt_msg(struct list *msgl,
		       const char *userid,
		       const char *clientidv[], size_t clientidc,
		       struct context *ctx)
{
	struct recipient_msg *rmsg = NULL;
	struct session *sess;
	size_t i;
	int err = 0;

	if (!msgl)
		return EINVAL;

	err = engine_recipient_msg_alloc(&rmsg);
	if (err)
		goto out;

	str_ncpy(rmsg->userid, userid, sizeof(rmsg->userid));

	for (i=0; i<clientidc; ++i) {

		struct client_msg *msg;
		const char *clientid = clientidv[i];

		err = engine_client_msg_alloc(&msg, &rmsg->msgl);
		if (err)
			goto out;

		str_ncpy(msg->clientid, clientid, sizeof(msg->clientid));

		msg->cipher_len = 4096;
		msg->cipher = mem_alloc(msg->cipher_len, NULL);

#ifdef HAVE_CRYPTOBOX
		sess = cryptobox_session_find(ctx->cb, userid, clientid, ctx->local_clientid);
		if (!sess) {
			warning("otr: no crypto session found for %s.%s"
				" (index=%zu)\n",
				userid, clientid, i);
			re_printf("(You need to fetch prekeys first!)\n");
			err = ENOENT;
			goto out;
		}

		err = cryptobox_session_encrypt(ctx->cb, sess,
						msg->cipher,
						&msg->cipher_len,
						ctx->data, ctx->data_len);
		if (err) {
			warning("otr: cryptobox_session_encrypto"
				" failed (%m)\n", err);
			goto out;
		}
#else
		warning("otr: compiled without HAVE_CRYPTOBOX\n");
		sess = NULL;
		err = ENOSYS;
		goto out;
#endif
	}
	list_append(msgl, &rmsg->le, rmsg);

 out:
	if (err)
		mem_deref(rmsg);

	return err;
}

static int send_otr_if_ready(struct context *ctx, bool ignore_missing)
{
	int err = 0;
	struct ctx_user *cu;
	size_t i;

	//info("send_otr_if_ready w:%s mp:%u rt:%d\n", ctx->waiting_for_otr ? "true" : "false",
	//	ctx->missing_prekeys, ctx->retries);

	if (!ctx->waiting_for_otr && ctx->missing_prekeys == 0) {
		if (--ctx->retries < 0) {
			if (ctx->resph)
				ctx->resph(EPROTO, ctx->arg);
			return EPROTO;
		}

		/* If userlist is empty, set ignore_missing=false to get the userlist */
		if (list_count(&ctx->userl) < 1) {
			ignore_missing = false;
		}

		while (ctx->userl.head != NULL) {
			cu = list_ledata(ctx->userl.head);
			list_unlink(&cu->le);

			const char *cids[MAX_CLIENTS];

			for (i = 0; i < cu->client_num; i++) {
				cids[i] = cu->clientid[i];
			}

			err = encrypt_msg(&ctx->msgl, cu->userid, cids, cu->client_num, ctx);
			if (err) {
				warning("OTR(%p) error encrypting message to %s failed (%m)\n",
					ctx, cu->userid, err);
			}

			mem_deref(cu);
		}

		ctx->waiting_for_otr = true;
		err = engine_send_message(ctx->conv,
				      ctx->local_clientid,
				      &ctx->msgl,
				      ctx->transient,
				      ignore_missing,
				      otr_response_handler,
				      otr_missing_handler,
				      ctx);
		if (err) {
			warning("OTR(%p) Send message Failed: %m.\n", ctx, err);
		}
	}

	return err;
}


/*
 * Send one message to users, encrypted for each of his clients
 *
 *  1. foreach user resolve how many clients user has
 *  2. foreach client in clients:
 *         encrypt payload
 *  3. send otr message with all encrypted payloads
 */
int engine_send_otr_message(struct engine *engine,
			    void *cb,
			    struct engine_conv *conv,
			    const char *target_userid,
			    const char *target_clientid,
			    const char *local_clientid,
			    const uint8_t *data, size_t data_len,
			    bool transient,
			    bool ignore_missing,
			    otr_resp_h *resph, void *arg)
{
	struct context *ctx;
	struct engine_user *self;
	int err;

	if (!conv || !data || !data_len)
		return EINVAL;

	ctx = mem_zalloc(sizeof(*ctx), context_destructor);	
	if (!ctx) {
		err = ENOMEM;
		warning("otr: could not allocate context\n");
		goto out;
	}
	list_init(&ctx->msgl);

	if (!local_clientid) {
		err = EINVAL;
		warning("otr: no local client id specified\n");
		goto out;
	}

	strncpy(ctx->local_clientid, local_clientid, MAX_ID_LEN - 1);

	ctx->engine = engine;
	self = engine_get_self(ctx->engine);
	if (!self) {
		err = ENOSYS;
		warning("otr: could not get self user\n");
		goto out;
	}
	
#ifdef HAVE_CRYPTOBOX
	ctx->cb = (struct cryptobox*)cb;
#endif
	ctx->conv = conv;
	ctx->self = self;
	ctx->data = mem_alloc(data_len, 0);
	ctx->data_len = data_len;
	memcpy(ctx->data, data, data_len);
	ctx->transient = transient;
	ctx->ignore_missing = ignore_missing;
	ctx->resph = resph;
	ctx->arg = arg;
	ctx->missing_prekeys = 0;
	ctx->retries = 3;

	if (target_userid) {
		strncpy(ctx->target_userid, target_userid, MAX_ID_LEN - 1);
	}

	if (target_clientid) {
		strncpy(ctx->target_clientid, target_clientid, MAX_ID_LEN - 1);
	}

	// First time will fail with 412 and fill the user-client list
	err = send_otr_if_ready(ctx, false);

 out:
	if (err)
		mem_deref(ctx);

	return err;
}
