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
#include "webrtc/common_audio/resampler/include/push_resampler.h"

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
#include "avs_base.h"
#include "avs_audio_effect.h"
}

#include "voe.h"
#include "interleaver.h"

//#define NETEQ_LOGGING 1

static void tmr_transport_handler(void *arg);

class VoETransport : public webrtc::Transport {
public:
	VoETransport(struct voe_channel *ve_) : ve(ve_), active(true),
					rtp_started(false)
	{
		debug("VoETransport::ctor\n");

		tmr_init(&tmr);
		le = (struct le)LE_INIT;
		list_append(&gvoe.transportl, &le, this);
		intlv.set_mode(INTERLEAVING_MODE_OFF);
	};

	virtual ~VoETransport()
	{
		debug("VoETransport::dtor\n");

		tmr_cancel(&tmr);
		list_unlink(&le);
	};

	bool SendRtp_core(const uint8_t* packet, size_t length)
	{
		int err = 0;
		struct auenc_state *aes = NULL;
		if (!active) {
			err = ENOENT;
			goto out;
		}
        
		aes = ve->aes;
		if (aes->rtph) {
			err = aes->rtph(packet, length, aes->arg);
			if (err) {
				warning("voe: rtp send failed (%m)\n", err);
				return false;
			}
#if FORCE_AUDIO_RTP_RECORDING
			ve->rtp_dump_out->DumpPacket(packet, length);
#endif
		}
        
	out:
		return err ? false : true;
	}
    
	virtual bool SendRtp(const uint8_t* packet, size_t length, const webrtc::PacketOptions& options)
	{
		int err = 0;
		struct auenc_state *aes = NULL;
		if (!active) {
			return false;
		}

		bool ret;
		uint8_t *packet_ptr;
		size_t packet_length;// = intlv.flush(&packet_ptr);
        
#if 0
		while(packet_length > 0){
			ret = SendRtp_core(packet_ptr, packet_length);
			if(!ret){
				return ret;
			}
		}
#endif
		packet_length = intlv.update(packet, length, &packet_ptr);
		ret = SendRtp_core(packet_ptr, packet_length);
        
		return ret;
	};

    
	virtual bool SendRtcp(const uint8_t* packet, size_t length)
	{
		int err = 0;
		struct auenc_state *aes = NULL;
        
		debug("VoETransport::SendRTCP: ve=%p active=%d\n", ve, active);

		if (!active) {
			err = ENOENT;
			goto out;
		}

		aes = ve->aes;

		if (!aes->started)
			return true;

		if (aes->rtcph) {
			err = aes->rtcph(packet, length, aes->arg);
			if (err) {
				warning("voe: rtcp send failed (%m)\n", err);
				return false;
			}
		}

	out:
		return err ? false : true;
	};

	void deregister()
	{
		active = false;

		debug("VoETransport::deregister\n");

		tmr_start(&tmr, MILLISECONDS_PER_SECOND,
			  tmr_transport_handler, this);
	}

private:
	struct voe_channel *ve;
	bool active;
	bool rtp_started;
	struct tmr tmr;
	struct le le;
	interleaver intlv;
};


static void tmr_transport_handler(void *arg)
{
	VoETransport *tp = (VoETransport *)arg;

	delete tp;
}

static void tmr_neteq_stats_handler(void *arg)
{
	struct channel_stats chstat;
	struct voe *voe = (struct voe *)arg;
        
	if(list_count(&voe->channel_data_list) > 0 && voe->nch > 0 && !voe->isSilenced){
#if NETEQ_LOGGING
		info("------ %d active channels ------- \n", list_count(&voe->channel_data_list));
#endif
		struct le *le;
		for(le = voe->channel_data_list.head; le; le = le->next){
			struct channel_data *cd = (struct channel_data *)le->data;
            
			chstat.in_vol = 20 * log10(voe->in_vol_smth + 1.0f);
			chstat.out_vol = 20 * log10(cd->out_vol_smth + 1.0f);
            
			int ch_id = cd->channel_number;
			voe->neteq_stats->GetNetworkStatistics(ch_id, chstat.neteq_nw_stats);
            
			webrtc::CallStatistics stats;
			voe->rtp_rtcp->GetRTCPStatistics(ch_id, stats);
			chstat.Rtt_ms = stats.rttMs;
			chstat.jitter_smpls = stats.jitterSamples;
			
			unsigned int NTPHigh = 0, NTPLow = 0, timestamp = 0, playoutTimestamp = 0, jitter = 0;
			unsigned short fractionLostUp_Q8 = 0; // Uplink packet loss as reported by remote side
			voe->rtp_rtcp->GetRemoteRTCPData( ch_id, NTPHigh, NTPLow, timestamp, playoutTimestamp, &jitter, &fractionLostUp_Q8);
            
			chstat.uplink_loss_q8 = fractionLostUp_Q8;
			chstat.uplink_jitter_smpls = jitter;
			
            
			memcpy(&cd->ch_stats[cd->stats_idx], &chstat, sizeof(chstat));			
			cd->stats_idx++;
			if(cd->stats_idx >= NUM_STATS){
				cd->stats_idx = 0;
			}
			cd->stats_cnt++;

			cd->quality.downloss = (int)(((float)(chstat.neteq_nw_stats.currentPacketLossRate)/163.84f) + 0.5f);
			cd->quality.rtt = chstat.Rtt_ms;
			if (cd->last_rtcp_ploss < 0)
				cd->quality.uploss = -1;
			else {
				int uploss;

				uploss = (int)((float)cd->last_rtcp_ploss/2.55f
					       + 0.5f);
				cd->quality.uploss = uploss;
			}
			
#if NETEQ_LOGGING
			float pl_rate = ((float)chstat.neteq_nw_stats.currentPacketLossRate)/163.84f; // convert Q14 -> float and fraction to percent
			float fec_rate = ((float)chstat.neteq_nw_stats.currentSecondaryDecodedRate)/163.84f; // convert Q14 -> float and fraction to percent
			float exp_rate = ((float)chstat.neteq_nw_stats.currentExpandRate)/163.84f;
			float acc_rate = ((float)chstat.neteq_nw_stats.currentAccelerateRate)/163.84f;
			float dec_rate = ((float)chstat.neteq_nw_stats.currentPreemptiveRate)/163.84f;
			info("ch# %d BufferSize = %d ms PacketLossRate = %.2f(%d) ExpandRate = %.2f fec_rate = %.2f AccelerateRate = %.2f DecelerateRate = %.2f \n", ch_id, chstat.neteq_nw_stats.currentBufferSize, pl_rate, (int)chstat.neteq_nw_stats.currentPacketLossRate, exp_rate, fec_rate, acc_rate, dec_rate);
#endif
		}
	}
	tmr_start(&voe->tmr_neteq_stats, NW_STATS_DELTA*MILLISECONDS_PER_SECOND, tmr_neteq_stats_handler, voe);
}

static void voe_setup_opus(bool use_stereo, int32_t rate_bps,
			   webrtc::CodecInst *codec_params)
{
	if (strncmp(codec_params->plname, "opus", 4) == 0) {
		if (use_stereo){
			codec_params->channels = 2;
		}
		else{
			codec_params->channels = 1;
		}
		codec_params->rate = rate_bps;
	}
}

static void ve_destructor(void *arg)
{
	struct voe_channel *ve = (struct voe_channel *)arg;

	debug("ve_destructor: %p voe.nch = %d \n", ve, gvoe.nch);

	if (ve->transport)
		ve->transport->deregister();

	if (gvoe.nw)
		gvoe.nw->DeRegisterExternalTransport(ve->ch);

	if (gvoe.base)
		gvoe.base->DeleteChannel(ve->ch);

	if (gvoe.nch > 0)
		--gvoe.nch;
	if (gvoe.nch == 0) {
		info("voe: Last Channel deleted call voe.base->Terminate()\n");

		voe_stop_audio_proc(&gvoe);

		voe_stop_silencing();
        
		gvoe.external_media->DeRegisterExternalMediaProcessing(-1, webrtc::kRecordingAllChannelsMixed);
		if(gvoe.voe_audio_effect){
			delete gvoe.voe_audio_effect;
		}
        
		tmr_cancel(&gvoe.tmr_neteq_stats);
        
		gvoe.base->Terminate();
        
		voe_stop_audio_test(&gvoe);
	}
    
#if FORCE_AUDIO_RTP_RECORDING
	if(ve->rtp_dump_in){
		ve->rtp_dump_in->Stop();
	}
	if(ve->rtp_dump_out){
		ve->rtp_dump_out->Stop();
    }
#endif
	if(ve->rtp_dump_in){
		delete ve->rtp_dump_in;
	}
	if(ve->rtp_dump_out){
		delete ve->rtp_dump_out;
	}
}

int voe_ve_alloc(struct voe_channel **vep, const struct aucodec *ac,
                 uint32_t srate, int pt)
{
	struct voe_channel *ve;
	webrtc::CodecInst c;
	int err = 0;
	int bitrate_bps;
	bool test_mode = false;

	ve = (struct voe_channel *)mem_zalloc(sizeof(*ve), ve_destructor);
	if (!ve)
		return ENOMEM;

	debug("voe: ve_alloc: ve=%p(%d) voe.nch = %d \n",
	      ve, mem_nrefs(ve), gvoe.nch);

	ve->ac = ac;
	ve->srate = srate;
	ve->pt = pt;
    
	++gvoe.nch;
	if (gvoe.nch == 1) {
		if (!gvoe.base) {
			warning("voe: no gvoe base!\n");
			err = ENOSYS;
			goto out;
		}

		webrtc::AudioDeviceModule* adm = NULL;
		if(gvoe.aio){
			adm = (webrtc::AudioDeviceModule*)gvoe.aio->aioc;
		}
		if (avs_get_flags() & AVS_FLAG_AUDIO_TEST){
			audio_io_alloc(&gvoe.autest.aio, AUDIO_IO_MODE_MOCK_REALTIME);
			adm = (webrtc::AudioDeviceModule*)gvoe.autest.aio->aioc;
		}
		info("voe: First Channel created call voe.base->Init() gvoe.adm = %p \n", adm);
		gvoe.base->Init(adm);
        
		if (avs_get_flags() & AVS_FLAG_AUDIO_TEST){
			voe_start_audio_test(&gvoe);
			test_mode = true;
		}
        
		gvoe.voe_audio_effect = new VoEAudioEffect(test_mode);
		if(gvoe.voe_audio_effect){
			gvoe.external_media->RegisterExternalMediaProcessing(-1,
									webrtc::kRecordingAllChannelsMixed, *gvoe.voe_audio_effect);
		}
        
		voe_start_audio_proc(&gvoe);

		voe_set_mute(gvoe.isMuted);

		voe_start_silencing();
        
		gvoe.packet_size_ms = 20;
        
		gvoe.in_vol_smth = 0.0f;
		gvoe.in_vol_max = 0;
		gvoe.out_vol_max = 0;
        
		tmr_start(&gvoe.tmr_neteq_stats, 5*MILLISECONDS_PER_SECOND, tmr_neteq_stats_handler, &gvoe);
	}
    
	bitrate_bps = gvoe.manual_bitrate_bps ? gvoe.manual_bitrate_bps : gvoe.bitrate_bps;
    
	ve->ch = gvoe.base->CreateChannel();
	if (ve->ch == -1) {
		err = ENOMEM;
		goto out;
	}

	ve->transport = new VoETransport(ve);
	gvoe.nw->RegisterExternalTransport(ve->ch, *ve->transport);

	c = *(webrtc::CodecInst *)ac->data;
	c.pltype = pt;

	voe_setup_opus( ZETA_OPUS_USE_STEREO, ZETA_OPUS_BITRATE_HI_BPS, &c);
    
	/* AUDIO-1450 - start with 20ms packets, will be updated later */
	c.pacsize = (c.plfreq * 20) / 1000;
	c.rate = bitrate_bps;
    
	gvoe.codec->SetSendCodec(ve->ch, c);
	gvoe.codec->SetRecPayloadType(ve->ch, c);

	gvoe.codec->SetFECStatus( ve->ch, ZETA_USE_INBAND_FEC );
    
	gvoe.codec->SetOpusDtx( ve->ch, ZETA_USE_DTX );
    
	gvoe.codec->SetOpusCbr( ve->ch, gvoe.cbr_enabled );
    
	ve->rtp_dump_in = new wire_avs::RtpDump();
	ve->rtp_dump_out = new wire_avs::RtpDump();
    
#if FORCE_AUDIO_RTP_RECORDING
	voe_start_rtp_dump(ve);
#endif
        
 out:
	if (err) {
		mem_deref(ve);
	}
	else {
		*vep = ve;
	}

	return err;
}


void voe_transportl_flush(void)
{
	struct le *le;

	le = gvoe.transportl.head;
	while (le) {
		VoETransport *vt = (VoETransport *)le->data;
		le = le->next;

		delete vt;
	}
}

int voe_set_audio_effect(enum audio_effect effect_type)
{
	int ret = 0;
	if(!gvoe.voe_audio_effect){
		return -1;
	}
    
	switch (effect_type) {
		case AUDIO_EFFECT_CHORUS_MAX:
		case AUDIO_EFFECT_CHORUS_MED:
		case AUDIO_EFFECT_CHORUS_MIN:
		case AUDIO_EFFECT_CHORUS:
		case AUDIO_EFFECT_VOCODER_MIN:
		case AUDIO_EFFECT_VOCODER_MED:
		case AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE:
		case AUDIO_EFFECT_PITCH_UP_SHIFT_MAX:
		case AUDIO_EFFECT_PITCH_UP_SHIFT_MED:
		case AUDIO_EFFECT_PITCH_UP_SHIFT_MIN:
		case AUDIO_EFFECT_PITCH_UP_SHIFT:
		case AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE:
		case AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX:
		case AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED:
		case AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN:
		case AUDIO_EFFECT_PITCH_DOWN_SHIFT:
        case AUDIO_EFFECT_AUTO_TUNE_MIN:
		case AUDIO_EFFECT_AUTO_TUNE_MED:
		case AUDIO_EFFECT_AUTO_TUNE_MAX:
		case AUDIO_EFFECT_PITCH_UP_DOWN_MIN:
		case AUDIO_EFFECT_PITCH_UP_DOWN_MED:
		case AUDIO_EFFECT_PITCH_UP_DOWN_MAX:
		case AUDIO_EFFECT_HARMONIZER_MIN:
		case AUDIO_EFFECT_HARMONIZER_MED:
		case AUDIO_EFFECT_HARMONIZER_MAX:
		case AUDIO_EFFECT_NONE:
			gvoe.voe_audio_effect->AddEffect(effect_type);
			break;
		case AUDIO_EFFECT_REVERB_MAX:
		case AUDIO_EFFECT_REVERB_MID:
		case AUDIO_EFFECT_REVERB_MIN:
		case AUDIO_EFFECT_REVERB:
		case AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX:
		case AUDIO_EFFECT_PACE_DOWN_SHIFT_MED:
		case AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN:
		case AUDIO_EFFECT_PACE_UP_SHIFT_MAX:
		case AUDIO_EFFECT_PACE_UP_SHIFT_MED:
		case AUDIO_EFFECT_PACE_UP_SHIFT_MIN:
		case AUDIO_EFFECT_REVERSE:
			error("voe: audio effect cannot be used in real time \n");
			ret = -1;
			break;
		default:
			error("voe: no valid audio effect \n");
			ret = -1;
	}
	return ret;
}
    
audio_effect voe_get_audio_effect()
{
	return AUDIO_EFFECT_NONE;
}

