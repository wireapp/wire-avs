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

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_file.h"
#include "webrtc/voice_engine/include/voe_audio_processing.h"
#include "webrtc/voice_engine/include/voe_volume_control.h"
#include "webrtc/voice_engine/include/voe_rtp_rtcp.h"
#include "webrtc/voice_engine/include/voe_neteq_stats.h"
#include "webrtc/voice_engine/include/voe_errors.h"
#include "webrtc/voice_engine/include/voe_hardware.h"
#include "voe_settings.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include <vector>
#include <algorithm>

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
}

#include "voe.h"
#include "avs_voe.h"


static void ads_destructor(void *arg)
{
	struct audec_state *ads = (struct audec_state *)arg;

	debug("voe: ads_destructor: %p ve=%p(%d)\n",
	      ads, ads->ve, mem_nrefs(ads->ve));

	voe_dec_stop(ads);

	list_unlink(&ads->le);
}


int voe_dec_alloc(struct audec_state **adsp,
		  const struct aucodec *ac,
		  const char *fmtp,
		  struct aucodec_param *prm,
		  audec_err_h *errh,
		  void *extcodec_arg,
		  void *arg)
{
	struct audec_state *ads;
	int err = 0;

	(void)extcodec_arg; /* Not an external codec */

	if (!adsp || !ac) {
		return EINVAL;
	}

	info("voe: dec_alloc: allocating codec:%s(%d)\n", ac->name, prm->pt);

	ads = (struct audec_state *)mem_zalloc(sizeof(*ads), ads_destructor);
	if (!ads)
		return ENOMEM;

	list_append(&gvoe.decl, &ads->le, ads);

	ads->ac = ac;
	ads->errh = errh;
	ads->arg = arg;
	ads->srate = prm->srate;
	ads->pt = prm->pt;
    
 out:
	if (err) {
		mem_deref(ads);
	}
	else {
		*adsp = ads;
	}

	return err;
}


int voe_dec_start(struct audec_state *ads,
		  struct media_ctx **mctxp)
{
	int err = 0;
	if (!ads || !mctxp)
		return EINVAL;

	info("voe: starting decoder\n");

	if (*mctxp) {
		ads->ve = (struct voe_channel *)mem_ref(*mctxp);
	} else {
		err = voe_ve_alloc(&ads->ve, ads->ac, ads->srate, ads->pt);
		if (err) {
			goto out;
		}
		*mctxp = (struct media_ctx *)ads->ve;
	}
    
	webrtc::CodecInst c;
    
	err = gvoe.base->StartReceive(ads->ve->ch);
	err += gvoe.base->StartPlayout(ads->ve->ch);

	gvoe.codec->GetSendCodec(ads->ve->ch, c);

	channel_data_add(&gvoe.channel_data_list, ads->ve->ch, c);
    
	voe_multi_party_packet_rate_control(&gvoe);
    
	voe_update_aec_settings(&gvoe);
    
	voe_update_agc_settings(&gvoe);
    
#if ZETA_USE_RCV_NS
	info("voe: Enabling rcv ns in mode = %d \n", (int)ZETA_RCV_NS_MODE);

	gvoe.processing->SetRxNsStatus(ads->ve->ch, true,  ZETA_RCV_NS_MODE);
#endif

	if(err){
		err = EIO;
	}
out:
	return err;
}

void voe_dec_stop(struct audec_state *ads)
{
	if (!ads || !ads->ve)
		return;

	info("voe: stopping decoder\n");

	if (!gvoe.base)
		return;

	gvoe.base->StopPlayout(ads->ve->ch);
	gvoe.base->StopReceive(ads->ve->ch);

	struct channel_data *cd = find_channel_data(&gvoe.channel_data_list, ads->ve->ch);
	if(cd){
		mem_deref(cd);
	}
	voe_multi_party_packet_rate_control(&gvoe);
        
	voe_update_aec_settings(&gvoe);
    
	voe_update_agc_settings(&gvoe);

#if ZETA_USE_RCV_NS
	bool enabled;
	webrtc::NsModes NSmode;

	gvoe.processing->GetRxNsStatus(ads->ve->ch, enabled, NSmode);
	if (enabled) {
		info("voe: turning rcv ns off\n");
		gvoe.processing->SetRxNsStatus(ads->ve->ch, false,
					      NSmode);
	}
#endif
	ads->ve = (struct voe_channel *)mem_deref(ads->ve);
}
