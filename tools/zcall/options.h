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

int options_init(void);
void options_close(void);

/* Option name is valv[0].  */
int set_option(int valc, char *valv[]);

/* Set option from one long white-space separated string. */
int set_phrase_option(const char *phrase);

typedef void(get_option_h)(void *arg);
typedef void(set_option_h)(int valc, char *valv[], void *arg);
typedef void(option_help_h)(void *arg);

struct opt {
	struct le le;
	const char *key;
	const char *help;

	get_option_h *geth;
	set_option_h *seth;
	option_help_h *helph;

	void *arg;
};
void register_option(struct opt *opt);
void unregister_option(struct opt *opt);

/* Try to get a boolean value from *val*. Returns 0 for true,
 * 1 for false, or -1 for illegal values.
 */
int option_value_to_bool(const char *val);

/* Print help for *opt*
 */
void print_option_help(const char *opt);
