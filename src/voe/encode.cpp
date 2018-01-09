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
    
	if(list_isempty(&gvoe.encl)){
		voe_set_mute(false);
	}
}


int voe_enc_alloc(struct auenc_state **aesp,
		  const struct aucodec *ac, const char *fmtp,
		  struct aucodec_param *prm,
		  auenc_rtp_h *rtph,
		  auenc_rtcp_h *rtcph,
		  auenc_err_h *errh,
		  void *extcodec_arg,
		  void *arg)
{
	struct auenc_state *aes;
	int err = 0;

	(void)extcodec_arg; /* Not an external codec */
	
	if (!aesp || !ac) {
		return EINVAL;
	}

	info("voe: enc_alloc: allocating codec:%s(%d)\n", ac->name, prm->pt);

	aes = (struct auenc_state *)mem_zalloc(sizeof(*aes), aes_destructor);
	if (!aes)
		return ENOMEM;

	list_append(&gvoe.encl, &aes->le, aes);

	aes->ac = ac;
	aes->rtph = rtph;
	aes->rtcph = rtcph;
	aes->errh = errh;
	aes->arg = arg;
	aes->local_ssrc = prm->local_ssrc;
	aes->remote_ssrc = prm->remote_ssrc;
	aes->pt = prm->pt;
	aes->srate = prm->srate;
	//aes->ch = prm
	aes->cbr = prm->cbr;
    
	*aesp = aes;

	return err;
}


int voe_enc_start(struct auenc_state *aes, bool cbr,
		  const struct aucodec_param *prm,
		  struct media_ctx **mctxp)
{
	int err = 0;
	if (!aes || !mctxp)
		return EINVAL;

	aes->cbr = cbr;
	
	if (*mctxp) {
		aes->ve = (struct voe_channel *)mem_ref(*mctxp);
	}
	else {
		err = voe_ve_alloc(&aes->ve, aes->ac, aes->srate, aes->pt);
		if (err) {
			goto out;
		}
		*mctxp = (struct media_ctx *)aes->ve;
	}
    
	aes->ve->aes = aes;
	if (prm) {		
		aes->local_ssrc = prm->local_ssrc;
	}
        
	if(gvoe.rtp_rtcp){
		gvoe.rtp_rtcp->SetLocalSSRC(aes->ve->ch, aes->local_ssrc);
	}
	if(gvoe.codec){
		gvoe.codec->SetOpusCbr(aes->ve->ch, aes->cbr);
	}

	info("voe: starting encoder -- StartSend ch %d \n", aes->ve->ch);
    
	aes->started = true;

	if (gvoe.base){
		err = gvoe.base->StartSend(aes->ve->ch);
	}
	if(err){
		err = EIO;
	}
out:
	return err;
}


void voe_enc_stop(struct auenc_state *aes)
{
	if (!aes)
		return;

	aes->started = false;

	if(!aes->ve)
		return;
    
	if (gvoe.base){
		info("voe: stopping encoder -- StopSend ch %d \n",
		     aes->ve->ch);

		gvoe.base->StopSend(aes->ve->ch);
	}
	aes->ve = (struct voe_channel *)mem_deref(aes->ve);
}

