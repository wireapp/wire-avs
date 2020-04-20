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
/* avs Command Line Client
 *
 * Options Handling
 */

#ifdef HAVE_READLINE
#include <wordexp.h>
#endif
#include <re.h>
#include <avs.h>
#include "cli.h"
#include "options.h"


/*** Globals
 */

static struct {
	struct list optl;
} glob = {
	.optl = LIST_INIT,
};


/*** set_option
 */

int set_option(int valc, char *valv[])
{
	struct le *le;

	if (valc < 2 || !valv)
		return EINVAL;

	LIST_FOREACH(&glob.optl, le) {
		struct opt *opt = le->data;

		if (!strcasecmp(valv[0], opt->key)) {
			if (!opt->seth)
				output("Read-only option.\n");
			else
				opt->seth(valc, valv, opt->arg);
			return 0;
		}
	}

	return ENOENT;
}


/*** set_phrase_option
 */

int set_phrase_option(const char *phrase)
{
#ifdef HAVE_READLINE

	wordexp_t p;
	int err;

	if (!wordexp(phrase, &p, 0))
		return EPROTO; /* XXX Or what now?  */

	err = set_option((int)p.we_wordc, p.we_wordv);
	wordfree(&p);
	return err;

#else

	struct str_wordexp p;
	int err;

	if (str_wordexp(&p, phrase))
		return EPROTO; /* XXX Or what now?  */

	err = set_option((int)p.wordc, p.wordv);
	str_wordfree(&p);
	return err;

#endif

}


/*** register_option
 */

void register_option(struct opt *opt)
{
	if (!opt)
		return;

	list_append(&glob.optl, &opt->le, opt);
}


/*** unregister_option
 */

void unregister_option(struct opt *opt)
{
	if (!opt)
		return;

	list_unlink(&opt->le);
}


/*** option_value_to_bool
 */

int option_value_to_bool(const char *val)
{
	if (!val)
		return -1;

	if (!strcasecmp(val, "true"))
		return 1;
	else if (!strcasecmp(val, "on"))
		return 1;
	else if (!strcasecmp(val, "1"))
		return 1;
	else if (!strcasecmp(val, "false"))
		return 0;
	else if (!strcasecmp(val, "off"))
		return 0;
	else if (!strcasecmp(val, "0"))
		return 0;
	else
		return -1;
}


/*** print_option_help
 */

void print_option_help(const char *opt)
{
	struct le *le;

	LIST_FOREACH(&glob.optl, le) {
		struct opt *o = le->data;

		if (strcasecmp(o->key, opt))
			continue;

		if (o->helph)
			o->helph(o->arg);
		else
			output("No help available.\n");
		break;
	}
}


/*** "set" command
 */

static void set_cmd_handler(int argc, char *argv[])
{
	int err;

	if (argc < 3) {
		output("Usage: set <key> <value> [...]\n");
		return;
	}

	err = set_option(argc - 1, argv + 1);

	if (err == ENOENT) {
		output("No such option. Try \"options\" for a list.\n");
	}
	else if (err) {
		output("Error setting option: %m.\n", err);
	}
}

struct command set_cmd = {
	.command = "set",
	.h = set_cmd_handler,
	.help = "set an option value"
};


/*** "get" command
 */

static void get_cmd_handler(int argc, char *argv[])
{
	struct le *le;

	if (argc != 2) {
		output("Usage: get <key>\n");
		return;
	}

	LIST_FOREACH(&glob.optl, le) {
		struct opt *opt = le->data;

		if (!strcasecmp(argv[1], opt->key)) {
			if (!opt->geth)
				output("Write-only option.\n");
			else
				opt->geth(opt->arg);
			return;
		}
	}
	output("No such option. Try \"options\" for a list.\n");
}

struct command get_cmd = {
	.command = "get",
	.h = get_cmd_handler,
	.help = "get an option value"
};


/*** "options" command
 */

static void options_cmd_handler(int argc, char *argv[])
{
	struct le *le;

	(void) argc;
	(void) argv;

	LIST_FOREACH(&glob.optl, le) {
		struct opt *opt = le->data;

		output("%s .. %s\n", opt->key, opt->help);
	}
}

struct command options_cmd = {
	.command = "options",
	.h = options_cmd_handler,
	.help = "list available options",
};


/*** options_init
 */

int options_init(void)
{
	register_command(&set_cmd);
	register_command(&get_cmd);
	register_command(&options_cmd);

	return 0;
}


/*** options_close
 */

void options_close(void)
{
}
