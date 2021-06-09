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
#include "avs_string.h"


int str_wordexp(struct str_wordexp *we, const char *str)
{
#define MAX_WORDS 32
	struct pl plv[MAX_WORDS];
	struct pl input;
	size_t i = 0;
	int err = 0;

	if (!we || !str)
		return EINVAL;

	input.p = str;
	input.l = str_len(str);

	while (input.l > 0) {
		struct pl word, ws;

		if (i >= ARRAY_SIZE(plv))
			return EOVERFLOW;

		err = re_regex(input.p, input.l,
			       "[^ \t\r\n]+[ \t\r\n]*", &word, &ws);
		if (err)
			return err;

		plv[i] = word;
		++i;

		pl_advance(&input, word.l + ws.l);
	}

	we->wordc = i;
	we->wordv = mem_alloc(we->wordc * sizeof(char *), NULL);
	if (!we->wordv)
		return ENOMEM;

	for (i=0; i<we->wordc; i++) {

		err = pl_strdup(&we->wordv[i], &plv[i]);
		if (err)
			break;
	}

	return err;
}


void str_wordfree(struct str_wordexp *we)
{
	size_t i;

	if (!we)
		return;

	for (i=0; i<we->wordc && we->wordv; i++) {

		we->wordv[i] = mem_deref(we->wordv[i]);
	}

	we->wordv = mem_deref(we->wordv);
	we->wordc = 0;
}

