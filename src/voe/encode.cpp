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
#include "webrtc/system_wrappers/interface/trace.h"
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
#include "webrtc/voice_engine/include/voe_conf_control.h"
#include "voe_settings.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include <vector>

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
}

#include "voe.h"


static void aes_destructor(void *arg)
{
	struct auenc_state *aes = (struct auenc_state *)arg;

	debug("voe: aes_destructor: %p ve=%p(%d)\n",
	      aes, aes->ve, mem_nrefs(aes->ve));

	voe_enc_stop(aes);

	list_unlink(&aes->le);

	mem_deref(aes->ve);
}


int voe_enc_alloc(struct auenc_state **aesp,
		  struct media_ctx **mctxp,
		  const struct aucodec *ac, const char *fmtp,
		  struct aucodec_param *prm,
		  auenc_rtp_h *rtph,
		  auenc_rtcp_h *rtcph,
		  auenc_packet_h *pkth,
		  auenc_err_h *errh,
		  void *arg)
{
	struct auenc_state *aes;
	int err = 0;

	if (!aesp || !ac || !mctxp) {
		return EINVAL;
	}

	info("voe: enc_alloc: allocating codec:%s(%d)\n", ac->name, prm->pt);

	aes = (struct auenc_state *)mem_zalloc(sizeof(*aes), aes_destructor);
	if (!aes)
		return ENOMEM;

	if (*mctxp) {
		aes->ve = (struct voe_channel *)mem_ref(*mctxp);
	}
	else {
		err = voe_ve_alloc(&aes->ve, ac, prm->srate, prm->pt);
		if (err) {
			goto out;
		}

		*mctxp = (struct media_ctx *)aes->ve;
	}

	list_append(&gvoe.encl, &aes->le, aes);

	aes->ve->aes = aes;
	aes->ac = ac;
	aes->rtph = rtph;
	aes->rtcph = rtcph;
	aes->pkth = pkth;
	aes->errh = errh;
	aes->arg = arg;

	if(gvoe.rtp_rtcp){
		gvoe.rtp_rtcp->SetLocalSSRC(aes->ve->ch, prm->local_ssrc);
	}
        
 out:
	if (err) {
		mem_deref(aes);
	}
	else {
		*aesp = aes;
	}

	return err;
}


int voe_enc_start(struct auenc_state *aes)
{
	if (!aes)
		return EINVAL;

	info("voe: starting encoder -- StartSend ch %d \n", aes->ve->ch);

	aes->started = true;

	if (gvoe.base){
		gvoe.base->StartSend(aes->ve->ch);
	}

	return 0;
}


void voe_enc_stop(struct auenc_state *aes)
{
	if (!aes)
		return;

	aes->started = false;

	if (gvoe.base){
		info("voe: stopping encoder -- StopSend ch %d \n",
		     aes->ve->ch);

		gvoe.base->StopSend(aes->ve->ch);
	}
}
