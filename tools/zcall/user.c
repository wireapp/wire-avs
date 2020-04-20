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
/* zclient-call -- command line version
 *
 * User handling
 */

#include <strings.h>
#include <re.h>
#include <avs.h>
#include "cli.h"


static void user_update_handler(struct engine_user *user,
			        enum engine_user_changes changes,
			        void *arg)
{
	(void) arg;

	if (changes & ENGINE_USER_NAME
	    && user == engine_get_self(zcall_engine))
	{
		output("You are now known as %s\n", user->name);
	}
}


static void conn_update_handler(struct engine_user *user,
				enum engine_conn_status old_status,
				void *arg)
{
	const char *name;

	(void) arg;

	if (!user->name)
		name = "unknown user";
	else
		name = user->name;

	if (old_status == ENGINE_CONN_NONE
	    && user->conn_status == ENGINE_CONN_PENDING)
	{
		output("%s asked to connect:\n%s\n",
			name, user->conn_message);
	}
	else {
		output("Connection with %s changed: %s -> %s\n",
		        name, engine_conn_status_string(old_status),
		        engine_conn_status_string(user->conn_status));
	}
}


struct engine_lsnr user_lsnr = {
	.userh = user_update_handler,
	.connh = conn_update_handler,
	.arg = NULL
};


/*** 's' ... Print own user info ("self")
 */

static bool self_key_handler(int ch)
{
	struct engine_user *self;

	(void)ch;

	self = engine_get_self(zcall_engine);
	if (!self) {
		output("Self not loaded yet.\n");
		return true;
	}

	output("name:  %s\n", self->name);
	output("email: %s\n", self->email);
	output("phone: %s\n", self->phone);
	output("id:    %s\n", self->id);

	return true;
}


struct key_stroke self_stroke = {
	.ch = 's',
	.h = self_key_handler,
	.help = "self user info"
};


/*** connections .. List connections
 */

static void connections_cmd_help(bool extended)
{
	output("usage: connections [-a]\n\n");
	if (extended) {
		output("\nPrints all pending connection requests.\n\n");
		output("\t-a        print all connections\n");
	}
}


static const char *get_user_name(struct engine_user *user)
{
	if (!user || !user->name || user->name[0] == '\0')
		return "Unknown user";
	else
		return user->name;
}


static bool all_conn_apply_handler(struct engine_user *user, void *arg)
{
	int *count = arg;

	if (user->conn_status != ENGINE_CONN_NONE) {
		output("%s: %s\n", get_user_name(user),
			engine_conn_status_string(user->conn_status));
		*count += 1;
	}

	return false;
}


static bool pending_conn_apply_handler(struct engine_user *user, void *arg)
{
	int *count = arg;

	if (user->conn_status == ENGINE_CONN_PENDING) {
		output("%s\n%s\n\n", get_user_name(user),
			user->conn_message);
		*count += 1;
	}

	return false;
}


static void connections_cmd_handler(int argc, char *argv[])
{
	bool all = false;
	int i;

	for (i = 1; i < argc; ++i) {
		if (streq(argv[i], "-a"))
			all = true;
		else if (streq(argv[i], "-h")) {
			connections_cmd_help(true);
			return;
		}
		else {
			connections_cmd_help(false);
			return;
		}
	}

	i = 0;
	engine_apply_users(zcall_engine,
			   all ? all_conn_apply_handler
			       : pending_conn_apply_handler,
			   &i);
	if (!i)
		output("No connections found.\n");
}


static struct command connections_cmd = {
	.command = "connections",
	.h = connections_cmd_handler,
	.help = "print connection requests"
};


/*** accept .. accept a connection request.
 */

static void accept_cmd_help(bool extended)
{
	output("usage: accept <user name>\n");
	if (extended) {
		output("\nAccept a connection request made by <user name>.\n");
		output("Hint: You only need to type as much of <user name>\n");
		output("      as necessary to make it unique.\n");
	}
}

static bool accept_apply_handler(struct engine_user *user, void *arg)
{
	struct pl *pl = arg;

	if (user->conn_status != ENGINE_CONN_PENDING
	    && user->conn_status != ENGINE_CONN_IGNORED)
	{
		return false;
	}

	return user->name && !strncasecmp(pl->p, user->name, pl->l);
}


static void update_conn_handler(int err, void *arg)
{
	(void) arg;

	if (err)
		output("Update failed: %m.\n", err);
}


static void accept_cmd_handler(int argc, char *argv[])
{
	struct pl pl;
	struct engine_user *user;
	int err;

	if (argc != 2) {
		accept_cmd_help(false);
		return;
	}

	pl_set_str(&pl, argv[1]);
	user = engine_apply_users(zcall_engine,
				  accept_apply_handler, &pl);

	if (!user) {
		output("No such request.\n");
		return;
	}

	err = engine_update_conn(user, ENGINE_CONN_ACCEPTED,
				 update_conn_handler, NULL);
	if (err)
		output("Update failed: %m.\n", err);
}


static struct command accept_cmd = {
	.command = "accept",
	.h = accept_cmd_handler,
	.help = "accept a connection request"
};


/*** Housekeeping.
 */


int user_init(void)
{
	register_key_stroke(&self_stroke);
	register_command(&connections_cmd);
	register_command(&accept_cmd);
	engine_lsnr_register(zcall_engine, &user_lsnr);
	return 0;
}

void user_close(void)
{
	engine_lsnr_unregister(&user_lsnr);
}
