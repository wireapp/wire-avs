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
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_string.h"
#include "avs_nevent.h"


struct nevent {
	struct websock_conn *conn;
	struct websock *websock;
	struct http_cli *http_cli;
	struct tmr tmr;
	char *server_uri;
	char *uri;
	bool term;
	struct list lsnrl;
	nevent_estab_h *estabh;
	nevent_recv_h *recvh;
	nevent_close_h *closeh;
	void *arg;
};


static int nevent_connect(struct nevent *ne);


struct event_data {
	struct json_object *jobj;
	const char *type;
};


static void send_to_listeners(struct nevent *ne, struct json_object *pld)
{
	int i, datac;
	struct event_data *datav, *p;
	struct le *le;

	datac = json_object_array_length(pld);
	if (datac == 0)
		return;
	datav = mem_alloc(sizeof(*datav) * datac, NULL);
	if (datav == NULL) {
		warning("nevent: Out of memory.\n");
		return;
	}
	
	/* XXX We don't really need the double loop anymore. Remove!  */

	for (i = 0, p = datav; i < datac; ++i, ++p) {
		struct json_object *item;

		item = json_object_array_get_idx(pld, i);
		if (item == NULL) {
			datac--;
			continue;
		}
		p->jobj = item;
		p->type = jzon_str(item, "type");
	}

	LIST_FOREACH(&ne->lsnrl, le) {
		struct nevent_lsnr *lsnr = le->data;

		for (i = 0, p = datav; i < datac; ++i, ++p) {
			if (lsnr->type && !streq(p->type, lsnr->type))
				continue;
			if (lsnr->eventh)
				lsnr->eventh(p->type, p->jobj, lsnr->arg);
		}
	}

	mem_deref(datav);
}


static void websock_estab_handler(void *arg)
{
	struct nevent *ne = arg;
	(void)ne;

	info("nevent: websocket established\n");

	if (ne->estabh)
		ne->estabh(ne->arg);
}


static void websock_recv_handler(const struct websock_hdr *hdr,
				 struct mbuf *mb, void *arg)
{
	struct nevent *ne = arg;
	struct json_object *jobj = NULL, *jpayload;
	const size_t len = mbuf_get_left(mb);
	int err;

	if (hdr->opcode != WEBSOCK_BIN) {
		info("nevent: ignoring websock opcode %u\n", hdr->opcode);
		return;
	}

	err = jzon_decode(&jobj, (char *)mbuf_buf(mb), len);
	if (err) {
		warning("nevent: failed to parse JSON (%zu bytes)\n", len);
		goto out;
	}

	if (!json_object_object_get_ex(jobj, "payload", &jpayload)) {
		warning("nevent: missing JSON 'payload' array\n");
		goto out;
	}

	debug("%b\n", mbuf_buf(mb), (int)len);

	if (ne->recvh)
		ne->recvh(jobj, ne->arg);

	send_to_listeners(ne, jpayload);

 out:
	mem_deref(jobj);
}


static void reconnect_handler(void *arg)
{
	struct nevent *ne = arg;
	int err;

	info("nevent: reconnecting now..\n");

	err = nevent_connect(ne);
	if (err) {
		warning("nevent: reconnect failed (%m)\n", err);
		tmr_start(&ne->tmr, 30000, reconnect_handler, ne);
	}
}


static void websock_close_handler(int err, void *arg)
{
	struct nevent *ne = arg;
	(void)ne;

	info("nevent: websock connection closed (%m)\n", err);

	if (ne->term) {

		info("nevent: we are terminated, stop it now.\n");

		if (ne->closeh)
			ne->closeh(err, ne->arg);
	}
	else {
		info("nevent: not terminated, trying to restablish Websock\n");

		tmr_start(&ne->tmr, 5000, reconnect_handler, ne);
	}
}


static void destructor(void *arg)
{
	struct nevent *ne = arg;

	ne->term = true;

	tmr_cancel(&ne->tmr);

	mem_deref(ne->uri);
	mem_deref(ne->server_uri);
	mem_deref(ne->http_cli);
	mem_deref(ne->websock);
	mem_deref(ne->conn);
}


static int nevent_connect(struct nevent *ne)
{
	if (!ne)
		return EINVAL;

	ne->conn = mem_deref(ne->conn);

	return websock_connect(&ne->conn, ne->websock, ne->http_cli, ne->uri,
			      5000,
			      websock_estab_handler, websock_recv_handler,
			      websock_close_handler, ne,
			      "Accept: application/json\r\n"
			      );
}


int nevent_alloc(struct nevent **nep, struct websock *websock,
		 struct http_cli *http_cli,
		 const char *server_uri, const char *access_token,
		 nevent_estab_h *estabh, nevent_recv_h *recvh,
		 nevent_close_h *closeh, void *arg)
{
	struct nevent *ne;
	int err = 0;

	if (!nep || !websock || !http_cli || !server_uri || !access_token)
		return EINVAL;

	ne = mem_zalloc(sizeof(*ne), destructor);
	if (!ne)
		return ENOMEM;

	ne->websock = mem_ref(websock);
	ne->http_cli = mem_ref(http_cli);

	ne->estabh = estabh;
	ne->recvh  = recvh;
	ne->closeh = closeh;
	ne->arg    = arg;

	tmr_init(&ne->tmr);

	err = str_dup(&ne->server_uri, server_uri);
	if (err) {
		warning("nevent_subscribe: copying server URI failed(%m)\n",
			err);
		goto out;
	}

	err = re_sdprintf(&ne->uri, "%s/await?access_token=%s",
		    	  server_uri, access_token);
	if (err) {
		warning("nevent_subscribe: making URI failed (%m)\n", err);
		goto out;
	}

	err = nevent_connect(ne);
	if (err) {
		warning("websock_connect() failed (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(ne);
	else
		*nep = ne;

	return err;
}


int nevent_set_access_token(struct nevent *ne, const char *access_token)
{
	if (ne->uri != NULL)
		mem_deref(ne->uri);
	return re_sdprintf(&ne->uri, "%s/await?access_token=%s",
			   ne->server_uri, access_token);
}


void nevent_register(struct nevent *ne, struct nevent_lsnr *lsnr)
{
	if (!lsnr)
		return;

	list_append(&ne->lsnrl, &lsnr->le, lsnr);
}


void nevent_unregister(struct nevent_lsnr *lsnr)
{
	if (!lsnr)
		return;

	list_unlink(&lsnr->le);
}


int nevent_restart(struct nevent *ne)
{
	return nevent_connect(ne);
}
