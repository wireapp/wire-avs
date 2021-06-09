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

#include <re.h>
#include <avs.h>
#include <avs_devpair.h>
#include "cli.h"


static struct {
	struct tmr tmr_get;
	bool started;
	char pairid[256];
	char username[256];
} pairing;


static const char *verify_uri = "http://127.0.0.1:8080";  // TODO config


static void get_publish(const char *pairid);
static void get_accept(const char *pairid);


static void publish_tmr_handler(void *arg)
{
	const char *pairid = arg;

	get_publish(pairid);
}


static void accept_tmr_handler(void *arg)
{
	const char *pairid = arg;

	get_accept(pairid);
}


static void devpair_estab_handler(const char *pairid, void *arg)
{
	(void)arg;

	output("pairing established with pairid %s\n", pairid);
}


static void devpair_close_handler(int err, const char *pairid, void *arg)
{
	(void)arg;

	output("pairing closed with pairid %s (%m)\n", pairid, err);

	pairing.pairid[0] = '\0';
	tmr_cancel(&pairing.tmr_get);
}


static void accept_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	if (err) {
		warning("accept: request failed: %m\n", err);
		return;
	}

	re_printf("accept: reply: scode=%d reason=%r \n",
		  msg->scode, &msg->reason);
}


static void accept_send_handler(const char *pairid, const uint8_t *data,
				size_t len, void *arg)
{
	char uri[512];
	int err = 0;

	re_printf("pairing[%s]: accept: send %zu bytes\n", pairid, len);

	re_snprintf(uri, sizeof(uri),
		    "%s/accept?pairid=%s",
		    verify_uri, pairid);

	err = http_request(NULL, engine_get_httpc(zcall_engine),
			   "PUT", uri,
			   accept_resp_handler,
			   NULL,
			   NULL,
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: %zu\r\n"
			   "\r\n"
			   "%s\r\n\r\n",
			   str_len((const char *)data), data);
	if (err) {
		warning("pairing: http_request failed (%m)\n", err);
		return;
	}
}


static void devpair_data_handler(const char *pairid,
				 const uint8_t *data, size_t len, void *arg)
{
	re_printf("pairing[%s]: recieved data: %s \n", pairing.username, data);
}


static void get_publish_resp_handler(int err, const struct http_msg *msg,
				     void *arg)
{
	const char *pairid = arg;
	const char *username;

	if (err) {
		warning("get_publish: request failed: %m\n", err);
		goto out;
	}

	re_printf("pairing: publish response (%u %r)\n",
		  msg->scode, &msg->reason);

	if (msg->scode >= 300) {
		warning("pairing: request failed (%u %r)\n",
			msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

	username = devpair_create(pairid, mbuf_buf(msg->mb),
				  mbuf_get_left(msg->mb));

	re_printf("pairing: accepted from device with username \"%s\"\n",
		  username);

	err = devpair_accept(pairid, accept_send_handler,
		       devpair_estab_handler,
		       devpair_data_handler,
		       devpair_close_handler,
		       NULL);
	if (err) {
		warning("pairing: devpair_accept failed (%m)\n", err);
		goto out;
	}

	str_ncpy(pairing.username, username, sizeof(pairing.username));

 out:
	if (err) {
		tmr_start(&pairing.tmr_get, 5000,
			  publish_tmr_handler, (void *)pairid);
	}
}


static void get_accept_resp_handler(int err, const struct http_msg *msg,
				     void *arg)
{
	const char *pairid = arg;

	if (err) {
		warning("get_accept: request failed: %m\n", err);
		goto out;
	}

	re_printf("pairing: accept response (%u %r)\n",
		  msg->scode, &msg->reason);

	if (msg->scode >= 300) {
		warning("pairing: request failed (%u %r)\n",
			msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

	err = devpair_ack(pairid, mbuf_buf(msg->mb), mbuf_get_left(msg->mb),
			  devpair_estab_handler,
			  devpair_data_handler, devpair_close_handler);
	if (err) {
		warning("pairing: devpair_ack failed (%m)\n", err);
		goto out;
	}

 out:
	if (err) {
		tmr_start(&pairing.tmr_get, 5000,
			  accept_tmr_handler, (void *)pairid);
	}
}


static void get_publish(const char *pairid)
{
	char uri[512];
	int err = 0;

	re_printf("pairing: get_publish (pairid=%s)\n", pairid);

	re_snprintf(uri, sizeof(uri),
		    "%s/publish?pairid=%s",
		    verify_uri, pairid);

	err = http_request(NULL, engine_get_httpc(zcall_engine),
			   "GET", uri,
			   get_publish_resp_handler,
			   NULL,
			   (void *)pairid,
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: 0\r\n"
			   "\r\n");
	if (err) {
		warning("pairing: http_request failed (%m)\n", err);
		return;
	}
}


static void get_accept(const char *pairid)
{
	char uri[512];
	int err = 0;

	re_printf("pairing: get_publish (pairid=%s)\n", pairid);

	re_snprintf(uri, sizeof(uri),
		    "%s/accept?pairid=%s",
		    verify_uri, pairid);

	err = http_request(NULL, engine_get_httpc(zcall_engine),
			   "GET", uri,
			   get_accept_resp_handler,
			   NULL,
			   (void *)pairid,
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: 0\r\n"
			   "\r\n");
	if (err) {
		warning("pairing: http_request failed (%m)\n", err);
		return;
	}
}


static void create_resp_handler(int err, const struct http_msg *msg,
				void *arg)
{
	struct json_object *jobj = NULL;
	const char *pairid;

	if (err) {
		warning("create_resp_handler: request failed: %m\n", err);
		return;
	}
	if (msg->scode >= 300) {
		warning("pairing: request failed (%u %r)\n",
			msg->scode, &msg->reason);
		return;
	}

	re_printf("pairing_response: reply: scode=%d reason=%r \n",
		  msg->scode, &msg->reason);

	err = jzon_decode(&jobj, (char *)mbuf_buf(msg->mb),
			  mbuf_get_left(msg->mb));
	if (err) {
		warning("could not decode JSON\n");
		goto out;
	}

	pairid = jzon_str(jobj, "pairid");
	if (!pairid) {
		warning("pairing: create_resp: missing 'pairid'\n");
		goto out;
	}

	str_ncpy(pairing.pairid, pairid, sizeof(pairing.pairid));

	output("got pairing-ID from server:    %s\n", pairid);

	get_publish(pairing.pairid);

 out:
	mem_deref(jobj);
}


static void publish_resp_handler(int err, const struct http_msg *msg,
				 void *arg)
{
	if (err) {
		warning("publish: request failed: %m\n", err);
		return;
	}

	re_printf("publish: reply: scode=%d reason=%r \n",
		  msg->scode, &msg->reason);

	get_accept(pairing.pairid);
}


static void pairing_create_command_handler(int argc, char *argv[])
{
	struct http_cli *httpc;
	char uri[512];
	int err = 0;

	output("Start pairing..\n");

	httpc = engine_get_httpc(zcall_engine);

	re_snprintf(uri, sizeof(uri),
		    "%s/create",
		    verify_uri);

	err = http_request(NULL, httpc, "POST", uri,
			   create_resp_handler,
			   NULL,
			   NULL,
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: 0\r\n"
			   "\r\n");
	if (err) {
		warning("pairing: create: http request failed (%m)\n", err);
		return;
	}
}


static void publish_send_handler(const char *pairid, const uint8_t *data,
				 size_t len, void *arg)
{
	char uri[512];
	int err = 0;

	re_printf("pairing[%s]: send %zu bytes\n", pairid, len);

	re_snprintf(uri, sizeof(uri),
		    "%s/publish?pairid=%s",
		    verify_uri, pairid);

	err = http_request(NULL, engine_get_httpc(zcall_engine),
			   "PUT", uri,
			   publish_resp_handler,
			   NULL,
			   NULL,
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: %zu\r\n"
			   "\r\n"
			   "%s\r\n\r\n",
			   str_len((const char *)data), data);
	if (err) {
		warning("pairing: http_request failed (%m)\n", err);
		return;
	}
}


static void pairing_publish_command_handler(int argc, char *argv[])
{
	struct engine_user *self;
	const char *pairid;
	const char *username = NULL;
	int err = 0;

	if (argc < 2) {
		output("usage: pairing_accept <pairid>\n");
		return;
	}

	pairid = argv[1];

	output("Publish pairing request with PAIRID=%s ..\n", pairid);

	self = engine_get_self(zcall_engine);
	if (!self) {
		output("Self not loaded yet.\n");
		return;
	}

	username = self->name;

	str_ncpy(pairing.pairid, pairid, sizeof(pairing.pairid));
	err = devpair_publish(pairid,
			      username,
			      publish_send_handler,
			      NULL);
	if (err) {
		warning("pairing: devpair_publish failed (%m)\n", err);
		return;
	}

	/* Poll for response */
}


static void pairing_xfer_command_handler(int argc, char *argv[])
{
	const char *pairid;
	const char *data;
	int err = 0;

	if (argc < 3) {
		output("usage: pairing_xfer <pairid> data\n");
		return;
	}

	pairid = argv[1];
	data = argv[2];

	output("Transfer data with PAIRID=%s ..\n", pairid);

	err = devpair_xfer(pairid, (uint8_t*)data, strlen(data), NULL, NULL);

	if (err) {
		warning("pairing: devpair_xfer failed (%m)\n", err);
		return;
	}
}


static void file_rcv_handler(const char *pairid,
			     const char *location, void *arg)
{
	re_printf("pairing[%s]: recieved file: %s \n", pairid, location);
}


static void file_snd_handler(const char *pairid, const char *name,
			     bool success, void *arg)
{
	if (success) {
		re_printf("pairing[%s]: succeded sending file: %s \n",
			  pairid, name);
	}
	else {
		re_printf("pairing[%s]: failed sending file: %s \n",
			  pairid, name);
	}
}


static void pairing_enable_file_xfer_command_handler(int argc, char *argv[])
{
	const char *pairid;
	int err = 0;

	if (argc < 2) {
		output("usage: pairing_enable_file_xfer <pairid>\n");
		return;
	}

	pairid = argv[1];

	output("Enable file transfer for PAIRID=%s ..\n", pairid);

	err = devpair_register_ft_handlers( pairid, "", file_rcv_handler,
					    file_snd_handler);
	if (err) {
		warning("pairing: devpair_register_ft_handlers failed (%m)\n",
			err);
		return;
	}
}


static void pairing_xfer_file_command_handler(int argc, char *argv[])
{
	const char *pairid;
	const char *file;
	const char *name;
	int err = 0;

	if (argc < 4) {
		output("usage: pairing_xfer_file <pairid> file name \n");
		return;
	}

	pairid = argv[1];
	file = argv[2];
	name = argv[3];

	output("Sending file %s with PAIRID=%s ..\n", file, pairid);

	err = devpair_xfer_file( pairid, file, name, -1);
	if (err) {
		warning("pairing: devpair_xfer_file failed (%m)\n", err);
		return;
	}
}


static struct command pairing_start_command = {
	.command = "pairing_start",
	.h = pairing_create_command_handler,
	.help = "start a pairing process (new)"
};

static struct command pairing_publish_command = {
	.command = "pairing_publish",
	.h = pairing_publish_command_handler,
	.help = "publish a pairing request (old)"
};

static struct command pairing_xfer_command = {
	.command = "pairing_xfer",
	.h = pairing_xfer_command_handler,
	.help = "Transfering a data message"
};

static struct command pairing_enable_file_xfer_command = {
	.command = "pairing_enable_file_xfer",
	.h = pairing_enable_file_xfer_command_handler,
	.help = "Enable file transfering"
};

static struct command pairing_xfer_file_command = {
	.command = "pairing_xfer_file",
	.h = pairing_xfer_file_command_handler,
	.help = "Transfering a file "
};


int pairing_init(void)
{
	int err;

	err = devpair_init();
	if (err)
		return err;

	register_command(&pairing_start_command);
	register_command(&pairing_publish_command);
	register_command(&pairing_xfer_command);
	register_command(&pairing_enable_file_xfer_command);
	register_command(&pairing_xfer_file_command);

	return 0;
}


void pairing_close(void)
{
	tmr_cancel(&pairing.tmr_get);

	devpair_close();
}
