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

#include <stdlib.h>
#include <re.h>
#include "avs_log.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_vidcodec.h"


/* note: shadow definition */
struct videnc_state {
	const struct vidcodec *vc;  /* base class (inheritance) */
};

/* note: shadow definition */
struct viddec_state {
	const struct vidcodec *vc;  /* base class (inheritance) */
};


/**
 * Register a Video Codec
 *
 * @param vc Video Codec
 */
void vidcodec_register(struct list *vidcodecl, struct vidcodec *vc)
{
	if (!vc)
		return;

	if (vc->pt) {
		int pt = atoi(vc->pt);

		if (pt >= MEDIA_PT_DYNAMIC_START) {
#if 0

			if (pt < MEDIA_PT_VIDEO_START ||
			    pt > MEDIA_PT_VIDEO_END) {
				warning("vidcodec: pt outside range\n");
				return;
			}
#endif
		}
	}

	list_append(vidcodecl, &vc->le, vc);

	info("vidcodec: %s (pt=%s)\n", vc->name, vc->pt);
}


/**
 * Unregister a Video Codec
 *
 * @param vc Video Codec
 */
void vidcodec_unregister(struct vidcodec *vc)
{
	if (!vc)
		return;

	list_unlink(&vc->le);
}


/**
 * Find a Video Codec by name
 *
 * @param name    Name of the Video Codec to find
 * @param variant Codec Variant
 *
 * @return Matching Video Codec if found, otherwise NULL
 */
const struct vidcodec *vidcodec_find(const struct list *vidcodecl,
				     const char *name, const char *variant)
{
	struct le *le;

	if (!vidcodecl)
		return NULL;

	for (le=vidcodecl->head; le; le=le->next) {

		struct vidcodec *vc = le->data;

		if (name && 0 != str_casecmp(name, vc->name))
			continue;

		if (variant && 0 != str_casecmp(variant, vc->variant))
			continue;

		return vc;
	}

	return NULL;
}


const struct vidcodec *videnc_get(struct videnc_state *ves)
{
	return ves ? ves->vc : NULL;
}


const struct vidcodec *viddec_get(struct viddec_state *vds)
{
	return vds ? vds->vc : NULL;
}
