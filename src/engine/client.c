#include <re.h>
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_rest.h"
#include "avs_uuid.h"
#include "avs_string.h"
#include "avs_zapi.h"
#include "avs_engine.h"
#include "module.h"
#include "engine.h"
#include "event.h"
#include "user.h"
#include "conv.h"
#include "utils.h"

#define MAX_CLIENTS 8

static void get_clients_handler(int err, const struct http_msg *msg,
				struct mbuf *mb, struct json_object *jobj,
				void *arg)
{
	const struct client_handler *clih = arg;
	struct odict *dict;
	size_t i, count;

	if (err) {
		warning("engine: get clients failed (%m)\n", err);
		goto out;
	}

	if (msg && msg->scode >= 300) {
		warning("engine: get clients failed (%u %r)\n",
			msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

#if 0
	re_printf("- - - - - - - - - - - - - - -\n");
	re_printf("Response:\n");
	re_printf("%b\n", mb->buf, mb->end);
	re_printf("- - - - - - - - - - - - - - -\n");
#endif

	dict = jzon_get_odict(jobj);

	/*
	 * Print the client info in a compact format
	 */
	count = odict_count(dict, false);

	for (i=0; i<count; i++) {
		const struct odict_entry *ae, *e;
		const char *clientid, *model;
		char key[16];

		re_snprintf(key, sizeof(key), "%zu", i);

		ae = odict_lookup(dict, key);
		if (!ae)
			continue;

		e = odict_lookup(ae->u.odict, "id");
		if (!e) {
			warning("engine: client \"id\" missing\n");
			continue;
		}
		clientid = e->u.str;

		e = odict_lookup(ae->u.odict, "time");

		e = odict_lookup(ae->u.odict, "address");

		e = odict_lookup(ae->u.odict, "model");
		model = e ? e->u.str : "?";

		if (clih && clih->clienth)
			clih->clienth(clientid, model, clih->arg);
	}

 out:
	if (clih && clih->getclih)
		clih->getclih(err, clih->arg);
}


int engine_get_clients(struct engine *eng, const struct client_handler *clih)
{
	int priority = 0;

	if (!eng)
		return EINVAL;

	return rest_get(NULL, eng->rest, priority,
			get_clients_handler, (void *)clih, "/clients");
}


static void get_client_prekeys_handler(int err, const struct http_msg *msg,
				struct mbuf *mb, struct json_object *jobj,
				void *arg)
{
	const struct prekey_handler *pkh = arg;
	struct odict *dict;
	const struct odict_entry *prekey, *clientid, *key, *id;
	uint8_t *buf = NULL;
	size_t buf_len;

	if (err) {
		warning("engine: get prekeys failed (%m)\n", err);
		return;
	}

	if (msg->scode >= 300) {
		warning("engine: get prekeys failed (%u %r)\n",
			msg->scode, &msg->reason);
		return;
	}

#if 0
	re_printf("get prekeys response:\n");
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

	dict = jzon_get_odict(jobj);

	prekey   = odict_lookup(dict, "prekey");
	clientid = odict_lookup(dict, "client");
	if (!prekey || !clientid) {
		warning("engine: get prekeys missing fields"
			" \"prekey\" or \"client\"\n");
		goto out;
	}

	key = odict_lookup(prekey->u.odict, "key");
	id  = odict_lookup(prekey->u.odict, "id");
	if (!key || !id) {
		warning("engine: get prekeys missing fields"
			" \"key\" or \"id\"\n");
		goto out;
	}
	buf_len = str_len(key->u.str) * 3 / 4;
	buf = mem_alloc(buf_len, NULL);

	err = base64_decode(key->u.str, str_len(key->u.str),
			    buf, &buf_len);
	if (err) {
		warning("engine: get prekeys: base64_decode failed\n");
		goto out;
	}

	if (pkh && pkh->prekeyh) {

		pkh->prekeyh(NULL, buf, buf_len, id->u.integer,
			     clientid->u.str, true,
			     pkh->arg);
	}

out:
	mem_deref(buf);
}


static void get_prekeys_handler(int err, const struct http_msg *msg,
				struct mbuf *mb, struct json_object *jobj,
				void *arg)
{
	const struct prekey_handler *pkh = arg;
	struct odict *dict;
	const struct odict_entry *user, *clients;
	size_t i, count;

	if (err) {
		warning("engine: get prekeys failed (%m)\n", err);
		return;
	}

	if (msg->scode >= 300) {
		warning("engine: get prekeys failed (%u %r)\n",
			msg->scode, &msg->reason);
		return;
	}

#if 0
	re_printf("get prekeys response:\n");
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

	dict = jzon_get_odict(jobj);

	user = odict_lookup(dict, "user");
	clients = odict_lookup(dict, "clients");
	if (!user || !clients) {
		warning("engine: get prekeys missing fields"
			" \"user\" or \"clients\"\n");
		return;
	}

	count = odict_count(clients->u.odict, false);
	if (count == 0) {
		warning("engine: no prekeys for user %s\n", user->u.str);
	}

	for (i=0; i<count; i++) {
		const struct odict_entry *ae;
		const struct odict_entry *prekey, *clientid, *key, *id;
		uint8_t *buf = NULL;
		size_t buf_len;
		char num[16];

		re_snprintf(num, sizeof(num), "%zu", i);

		ae = odict_lookup(clients->u.odict, num);
		if (!ae)
			continue;

		prekey   = odict_lookup(ae->u.odict, "prekey");
		clientid = odict_lookup(ae->u.odict, "client");
		if (!prekey || !clientid) {
			warning("engine: get prekeys missing fields"
				" \"prekey\" or \"client\"\n");
			continue;
		}

		key = odict_lookup(prekey->u.odict, "key");
		id  = odict_lookup(prekey->u.odict, "id");
		if (!key || !id) {
			warning("engine: get prekeys missing fields"
				" \"key\" or \"id\"\n");
			continue;
		}
		buf_len = str_len(key->u.str) * 3 / 4;
		buf = mem_alloc(buf_len, NULL);

		err = base64_decode(key->u.str, str_len(key->u.str),
				    buf, &buf_len);
		if (err) {
			warning("engine: get prekeys: base64_decode failed\n");
			goto next;
		}

		if (pkh && pkh->prekeyh) {

			pkh->prekeyh(user->u.str, buf, buf_len, id->u.integer,
				     clientid->u.str, i==(count-1),
				     pkh->arg);
		}

	next:
		mem_deref(buf);
	}
}


int engine_get_prekeys(struct engine *eng, const char *userid,
		       const struct prekey_handler *pkh)
{
	int priority = 0;

	if (!eng || !str_isset(userid))
		return EINVAL;

	return rest_get(NULL, eng->rest, priority,
			get_prekeys_handler, (void *)pkh,
			"/users/%s/prekeys", userid);
}


int engine_get_client_prekeys(struct engine *eng, const char *userid, const char *clientid,
		       const struct prekey_handler *pkh)
{
	int priority = 0;

	if (!eng || !str_isset(userid))
		return EINVAL;

	return rest_get(NULL, eng->rest, priority,
			get_client_prekeys_handler, (void *)pkh,
			"/users/%s/prekeys/%s", userid, clientid);
}


struct uc_ctx {
	engine_user_clients_h *uch;
	void *arg;
};



static void get_user_clients_handler(int err, const struct http_msg *msg,
				     struct mbuf *mb,
				     struct json_object *jobj,
				     void *arg)
{
	struct uc_ctx *ctx = arg;
	const char *clientidv[MAX_CLIENTS];
	size_t i, clientidc = 0;

	if (err) {
		if (err != ECONNABORTED) {
			warning("engine: get user clients failed (%m)\n", err);
		}
		goto out;
	}

	if (msg->scode >= 300) {
		warning("engine: get user clients failed (%u %r)\n",
			msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

#if 0
	re_printf("get user clients response:\n");
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

	clientidc = json_object_array_length(jobj);
	if (clientidc > ARRAY_SIZE(clientidv))
		clientidc = ARRAY_SIZE(clientidv);
	
	for (i=0; i<clientidc; i++) {
		struct json_object *jent;

		jent = json_object_array_get_idx(jobj, i);

		clientidv[i] = jzon_str(jent, "id");
	}

 out:
	ctx->uch(err, clientidv, clientidc, ctx->arg);

	mem_deref(ctx);
}


/**
 * Get all of a user's clients
 */
int engine_get_user_clients(struct engine *eng, const char *userid,
			    engine_user_clients_h *uch, void *arg)
{
	struct uc_ctx *ctx;
	int priority = 0;
	int err;

	if (!eng || !str_isset(userid))
		return EINVAL;

	ctx = mem_zalloc(sizeof(*ctx), NULL);

	ctx->uch = uch;
	ctx->arg = arg;

	err = rest_get(NULL, eng->rest, priority,
		       get_user_clients_handler, ctx,
		       "/users/%s/clients", userid);
	if (err)
		mem_deref(ctx);

	return err;
}


static void reg_client_resp_handler(int err, const struct http_msg *msg,
				    struct mbuf *mb, struct json_object *jobj,
				    void *arg)
{
	const struct client_handler *clih = arg;

	if (err) {
		warning("engine: reg client failed (%m)\n", err);
		goto out;
	}

	if (msg->scode >= 300) {
		warning("engine: reg client failed (%u %r)\n",
			msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

	info("engine: Register client: %u %r\n",
	     msg ? msg->scode : 0,
	     msg ? &msg->reason : 0);

#if 0
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

 out:
	if (clih && clih->clientregh) {
		clih->clientregh(err, jzon_str(jobj, "id"),
				 clih->arg);
	}
}


/*
 * Example JSON request:
 *
 * {
 *  "cookie": "",
 *  "lastkey": {
 *    "key": "",
 *    "id": 0
 *  },
 *  "sigkeys": {
 *    "enckey": "",
 *    "mackey": ""
 *  },
 *  "password": "",
 *  "model": "",
 *  "type": "",
 *  "prekeys": [
 *    {
 *      "key": "",
 *      "id": 0
 *    }
 *  ],
 *  "class": "",
 *  "label": ""
 * }
 */
int engine_register_client(struct engine *eng,
			   const struct zapi_prekey *lastkey,
			   const struct zapi_prekey *prekeyv, size_t prekeyc,
			   const struct client_handler *clih)
{
	struct json_object *jobj, *jlastkey, *jsigkeys, *jarr;
	const char *cookie = "123";  /* XXX: dummy cookie */
	static const uint8_t dummy_key[32] = {0};
	size_t i;
	int priority = 0;
	int err = 0;

	if (!eng || !lastkey || !prekeyv || !prekeyc)
		return EINVAL;

	jobj       = json_object_new_object();
	jlastkey   = json_object_new_object();
	jsigkeys   = json_object_new_object();

	err = jzon_add_str(jobj, "cookie", cookie);
	if (err)
		goto out;

	err = zapi_prekey_encode(jlastkey, lastkey);
	if (err)
		goto out;

	json_object_object_add(jobj, "lastkey", jlastkey);

	/* The signaling keys to use for encryption and signing of OTR
	 * native push notifications (APNS, GCM).
	 *
	 * zcall does not use APNS/GCM to use dummy keys
	 */
	err  = jzon_add_base64(jsigkeys, "enckey",
			       dummy_key, sizeof(dummy_key));
	err |= jzon_add_base64(jsigkeys, "mackey",
			       dummy_key, sizeof(dummy_key));
	if (err)
		goto out;

	json_object_object_add(jobj, "sigkeys", jsigkeys);

	err |= jzon_add_str(jobj, "password", eng->password);
	err |= jzon_add_str(jobj, "model", eng->user_agent);
	err |= jzon_add_str(jobj, "type", "permanent");
	if (err)
		goto out;

	/* add an array of prekeys */
	jarr = json_object_new_array();

	for (i=0; i<prekeyc; i++) {

		struct json_object *ae = json_object_new_object();

		err = zapi_prekey_encode(ae, &prekeyv[i]);
		if (err)
			goto out;

		err = json_object_array_add(jarr, ae);
		if (err)
			goto out;
	}

	json_object_object_add(jobj, "prekeys", jarr);

	err = jzon_add_str(jobj, "class", "desktop");
	if (err)
		goto out;

#if 0
	re_printf("POST /clients request:\n");
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

	err = rest_request_jobj(NULL, eng->rest, priority, "POST",
				reg_client_resp_handler, (void *)clih,
				jobj,
				"/clients");
	if (err)
		goto out;

 out:
	mem_deref(jobj);

	return err;
}


static void delete_client_handler(int err, const struct http_msg *msg,
				  struct mbuf *mb, struct json_object *jobj,
				  void *arg)
{
	if (err) {
		warning("engine: delete client failed (%m)\n", err);
		return;
	}

	if (msg->scode >= 300) {
		warning("engine: delete client failed (%u %r)\n",
			msg->scode, &msg->reason);
		return;
	}

	re_printf("Delete client: %u %r\n", msg->scode, &msg->reason);

#if 1
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif
}


int engine_delete_client(struct engine *eng, const char *clientid)
{
	struct json_object *jobj;
	int priority = 0;
	int err;

	if (!eng || !clientid)
		return EINVAL;

	err = jzon_creatf(&jobj, "s", "password", eng->password);
	if (err)
		return err;

	err = rest_request_jobj(NULL, eng->rest, priority, "DELETE",
				delete_client_handler, NULL,
				jobj,
				"/clients/%s", clientid);
	if (err)
		goto out;

 out:
	mem_deref(jobj);

	return err;
}
