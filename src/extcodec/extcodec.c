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
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_vidcodec.h"

#include "avs_extcodec.h"



static struct {
	struct list aucodecl;
	struct list vidcodecl;
} extcodec = {
	.aucodecl = LIST_INIT,
	.vidcodecl = LIST_INIT,
};


int extcodec_audio_register(struct aucodec *ac)
{
	if (!ac)
		return EINVAL;

	list_append(&extcodec.aucodecl, &ac->ext_le, ac);

	return 0;
}


void extcodec_audio_unregister(struct aucodec *ac)
{
	if (!ac)
		return;

	list_unlink(&ac->ext_le);
}


int extcodec_video_register(struct vidcodec *vc)
{
	if (!vc)
		return EINVAL;

	list_append(&extcodec.vidcodecl, &vc->ext_le, vc);

	return 0;
}


void extcodec_video_unregister(struct vidcodec *vc)
{
	if (!vc)
		return;

	list_unlink(&vc->ext_le);
}


int extcodec_audio_init(struct list *aucodecl)
{
	struct le *le;
	int err = 0;

	/* list all supported codecs */

	LIST_FOREACH(&extcodec.aucodecl, le) {
		struct aucodec *ac = le->data;

		if (!ac->name || !ac->srate || !ac->ch)
			continue;

		ac->data = NULL;

		aucodec_register(aucodecl, ac);

		info("extcodec_audio_init: registering %s(%d) ch=%d\n",
		     ac->name, ac->srate, ac->ch);
	}

	if (err)
		extcodec_audio_close();

	return err;
}


int extcodec_video_init(struct list *vidcodecl)
{
	struct le *le;
	int err = 0;

	/* list all supported codecs */

	LIST_FOREACH(&extcodec.vidcodecl, le) {
		struct vidcodec *vc = le->data;

		vc->data = NULL;

		vidcodec_register(vidcodecl, vc);
		info("extcodec_video_init: registering %s(%d)", vc->name);
	}

	if (err)
		extcodec_audio_close();

	return err;
}


void extcodec_audio_close(void)
{
	struct le *le;
	
	LIST_FOREACH(&extcodec.aucodecl, le) {	
		struct aucodec *ac = le->data;

		aucodec_unregister(ac);

		info("extcodec_audio_close: unregistered %s(%d) ch=%d\n",
		     ac->name, ac->srate, ac->ch);
	}
}


void extcodec_video_close(void)
{
}


