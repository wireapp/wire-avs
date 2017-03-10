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
#include <re/re.h>
#include "avs_aucodec.h"
#include "avs_log.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_rest.h"
#include "avs_conf_pos.h"
#include "avs_media.h"
#include "avs_flowmgr.h"
#include "avs_voe.h"
#include "flowmgr.h"


int flowmgr_ausrc_changed(struct flowmgr *fm, enum flowmgr_ausrc asrc)
{
	const char *dev;

	(void)fm;

	switch (asrc) {

	case FLOWMGR_AUSRC_INTMIC:
		dev = "intmic";
		break;

	case FLOWMGR_AUSRC_EXTMIC:
		dev = "extmic";
		break;

	case FLOWMGR_AUSRC_LINEIN:
		dev = "linein";
		break;

	case FLOWMGR_AUSRC_SPDIF:
		dev = "spdif";
		break;

	default:
		return EINVAL;
	}

	warning("flowmgr: set_ausrc(%s) should not be called!\n", dev);
	return ENOSYS;
}


int flowmgr_auplay_changed(struct flowmgr *fm, enum flowmgr_auplay aplay)
{
	const char *dev;
	int err = ENOSYS;
	(void)fm;

	switch (aplay) {

	case FLOWMGR_AUPLAY_EARPIECE:
		dev = "earpiece";
		break;

	case FLOWMGR_AUPLAY_SPEAKER:
		dev = "speaker";
		break;

	case FLOWMGR_AUPLAY_BT:
		dev = "bt";
		break;

	case FLOWMGR_AUPLAY_LINEOUT:
		dev = "lineout";
		break;

	case FLOWMGR_AUPLAY_SPDIF:
		dev = "spdif";
		break;

	case FLOWMGR_AUPLAY_HEADSET:
		dev = "headset";
		break;
            
	default:
		return EINVAL;
	}

	err = voe_set_auplay(dev);

	return err;
}


int flowmgr_set_mute(struct flowmgr *fm, bool mute)
{
	int err = 0;
	(void)fm;

	err = voe_set_mute(mute);

	return err;
}


int flowmgr_get_mute(struct flowmgr *fm, bool *muted)
{
	int err = ENOSYS;
	(void)fm;

	err = voe_get_mute(muted);

	return err;
}


int flowmgr_start_mic_file_playout(const char fileNameUTF8[1024], int fs)
{
	int err = 0;

	err = voe_start_playing_PCM_file_as_microphone(fileNameUTF8, fs);

	return err;
}


void flowmgr_stop_mic_file_playout(void)
{
	voe_stop_playing_PCM_file_as_microphone();
}

void flowmgr_set_bitrate(int rate_bps)
{
	voe_set_bitrate(rate_bps);
}


void flowmgr_set_packet_size(int packet_size_ms)
{
	voe_set_packet_size(packet_size_ms);
}


void flowmgr_silencing(bool silenced)
{
	if (!flowmgr_is_using_voe())
		return;

	if (silenced)
		voe_start_silencing();
	else
		voe_stop_silencing();
}


int flowmgr_update_conf_parts(struct list *partl)
{
	const struct audec_state **adsv;
	struct le *le;
	size_t i, adsc = list_count(partl);

	adsv = mem_zalloc(sizeof(*adsv) * adsc, NULL);
	if (!adsv)
		return ENOMEM;

	/* convert the participant list to a vector */
	for (le = list_head(partl), i=0; le; le = le->next, i++) {

		struct conf_part *part = le->data;
		struct flow *flow = part->data;
		struct userflow *uf = flow ? flow->userflow : NULL;
		struct mediaflow *mf = userflow_mediaflow(uf);
		struct audec_state *ads = mediaflow_decoder(mf);

		adsv[i] = ads;
	}

	voe_update_conf_parts(adsv, adsc);

	mem_deref(adsv);

	return 0;
}
