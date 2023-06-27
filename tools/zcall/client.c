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
#include "cli.h"
#include "options.h"


#define ID "clientid"


int client_id_load(char *buf, size_t sz)
{
	struct sobject *so = NULL;
	char *clientid = NULL;
	int err;

	if (!zcall_store)
		return ENOENT;

	err = store_user_open(&so, zcall_store, "zcall", ID, "rb");
	if (err)
		return err;

	err = sobject_read_lenstr(&clientid, so);
	if (err)
		goto out;

	if (strlen(clientid) > sz) {
		err = ENOSPC;
		goto out;
	}

	str_ncpy(buf, clientid, sz);

 out:
	mem_deref(clientid);
	mem_deref(so);

	return err;
}


int client_id_save(const char *clientid)
{
	struct sobject *so = NULL;
	int err;

	if (!str_isset(clientid))
		return EINVAL;

	if (!zcall_store)
		return ENOENT;

	err = store_user_open(&so, zcall_store, "zcall", ID, "wb");
	if (err)
		goto out;

	err = sobject_write_lenstr(so, clientid);
	if (err)
		goto out;

 out:
	sobject_close(so);
	mem_deref(so);

	return err;
}


void client_id_delete(void)
{
	store_user_unlink(zcall_store, "zcall", ID);
}


static void client_reg_handler(int err, const char *clientid, void *arg)
{

	output("Client registered: %s\n", clientid);

	err = client_id_save(clientid);
	if (err) {
		output("could not save clientid (%m)\n", err);
	}
	else {
		output("Clientid saved successfully.\n");
	}
}


static void client_handler(const char *clientid, const char *model, void *arg)
{
	char my_clientid[64] = "";
	(void)arg;

	client_id_load(my_clientid, sizeof(my_clientid));

	output("%c Client:     %s    (%s)\n",
	       0 == str_casecmp(clientid, my_clientid) ? '*' : ' ',
	       clientid, model);
}


static const struct client_handler clienth = {
	.clientregh = client_reg_handler,
	.clienth    = client_handler
};


/*** get_clients. List the registered clients.
 */


static void get_clients_cmd_handler(int argc, char *argv[])
{
	int err;
	(void) argc;

	err = engine_get_clients(zcall_engine, &clienth);
	if (err)
		output("Failed: %m.\n", err);
	else
		output("OK\n");
}


static struct command get_clients_command = {
	.command = "get_clients",
	.h = get_clients_cmd_handler,
	.help = "List your registered clients.",
	.verbatim = true
};


#ifdef HAVE_CRYPTOBOX


/*** reg_client. Register the running client.
 */

#define NUM_PREKEYS 1  // TODO: should be 100

static void reg_client_cmd_handler(int argc, char *argv[])
{
	struct zapi_prekey lastkey;
	struct zapi_prekey prekeyv[NUM_PREKEYS];
	size_t i;
	int err;
	(void) argc;

	lastkey.key_len = sizeof(lastkey.key);
	lastkey.id = 65535;

	err = cryptobox_generate_prekey(g_cryptobox,
					lastkey.key, &lastkey.key_len,
					lastkey.id);
	if (err) {
		warning("failed to generate prekey (%m)\n", err);
		return;
	}

	for (i=0; i<NUM_PREKEYS; i++) {
		struct zapi_prekey *pk = &prekeyv[i];
		uint16_t id = i + 1;

		pk->key_len = sizeof(pk->key);
		pk->id = id;

		err = cryptobox_generate_prekey(g_cryptobox,
						pk->key, &pk->key_len, id);
		if (err) {
			warning("failed to generate prekey (%m)\n", err);
			return;
		}
	}

	err = engine_register_client(zcall_engine, &lastkey,
				     prekeyv, ARRAY_SIZE(prekeyv), &clienth);
	if (err)
		output("Failed: %m.\n", err);
	else
		output("OK\n");
}


static struct command reg_client_command = {
	.command = "reg_client",
	.h = reg_client_cmd_handler,
	.help = "Register your running client.",
	.verbatim = true
};
#endif


static void delete_all_client_handler(const char *clientid,
				      const char *model, void *arg)
{
	int err;

	output("deleting client with clientid=%s (%s)\n", clientid, model);

	err = engine_delete_client(zcall_engine, clientid);
	if (err)
		output("Failed: %m.\n", err);
	else
		output("OK\n");
}


static const struct client_handler delete_allh = {
	.clienth    = delete_all_client_handler
};


/*** delete_client. Delete the running client.
 */

static void delete_client_cmd_handler(int argc, char *argv[])
{
	char my_clientid[64] = "";
	const char *clientid;
	bool delete_all = false;
	int err;

	if (argc != 2) {
		output("usage: delete_client <CLIENTID>\n");
		return;
	}

	clientid = argv[1];

	if (0 == str_cmp(clientid, "*")) {
		delete_all = true;
		output("DELETING ALL CLIENTS!!!\n");
	}
	else {
		output("DELETING CLIENT WITH CLIENTID \"%s\"\n", clientid);
	}

	if (delete_all) {

		err = engine_get_clients(zcall_engine, &delete_allh);
		if (err)
			output("Failed: %m.\n", err);
		else
			output("OK\n");
	}
	else {
		err = engine_delete_client(zcall_engine, clientid);
		if (err)
			output("Failed: %m.\n", err);
		else
			output("OK\n");

		err = client_id_load(my_clientid, sizeof(my_clientid));
		if (err) {
			output("could not load my clientid (%m)\n",
			       err);
			return;
		}
	}

	if (0 == str_casecmp(clientid, my_clientid) || delete_all) {
		output("NOTE: also deleting my cryptobox directory!\n");
		client_id_delete();
	}
}


static struct command delete_client_command = {
	.command = "delete_client",
	.h = delete_client_cmd_handler,
	.help = "Delete a client.",
	.verbatim = true
};


int client_init(void)
{
	register_command(&get_clients_command);
	register_command(&delete_client_command);

#ifdef HAVE_CRYPTOBOX
	register_command(&reg_client_command);
#endif

	return 0;
}


void client_close(void)
{
}
