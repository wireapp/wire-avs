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
/**
 * @file aucodec.c Audio Codec
 */

#include <stdlib.h>
#include <re.h>
#include "avs_log.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_aucodec.h"


/* note: shadow struct */
struct auenc_state {
	const struct aucodec *ac;  /* base clase */
};

/* note: shadow struct */
struct audec_state {
	const struct aucodec *ac;  /* base clase */
};


void aucodec_register(struct list *aucodecl, struct aucodec *ac)
{
	if (!aucodecl || !ac)
		return;

	if (ac->pt) {
		int pt = atoi(ac->pt);

		if (pt >= MEDIA_PT_DYNAMIC_START) {
#if 0
			if (pt < MEDIA_PT_AUDIO_START ||
			    pt > MEDIA_PT_AUDIO_END) {
				warning("aucodec: pt outside range\n");
				return;
			}
#endif
		}
	}

	list_append(aucodecl, &ac->le, ac);

	info("aucodec: %s/%u/%d (pt=%s)\n", ac->name, ac->srate, ac->ch,
	     ac->pt);
}


void aucodec_unregister(struct aucodec *ac)
{
	if (!ac)
		return;

	list_unlink(&ac->le);
}


const struct aucodec *aucodec_find(struct list *aucodecl,
				   const char *name, uint32_t srate,
				   uint8_t ch)
{
	struct le *le;

	if (!aucodecl)
		return NULL;

	for (le=list_head(aucodecl); le; le=le->next) {

		struct aucodec *ac = le->data;

		if (name && 0 != str_casecmp(name, ac->name))
			continue;

		if (srate && srate != ac->srate)
			continue;

		if (ch && ch != ac->ch)
			continue;

		return ac;
	}

	return NULL;
}


const struct aucodec *auenc_get(struct auenc_state *aes)
{
	return aes ? aes->ac : NULL;
}


const struct aucodec *audec_get(struct audec_state *ads)
{
	return ads ? ads->ac : NULL;
}
