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
#include "avs_base.h"
}

#include "voe.h"


static void tmr_transport_handler(void *arg);


class VoETransport : public webrtc::Transport {
public:
	VoETransport(struct voe_channel *ve_) : ve(ve_), active(true),
					rtp_started(false) {
		tmr_init(&tmr);
		le = (struct le)LE_INIT;
		list_append(&gvoe.transportl, &le, this);
	};
	virtual ~VoETransport() {
		tmr_cancel(&tmr);
		list_unlink(&le);
	};

    virtual bool SendRtp(const uint8_t* packet, size_t length, const webrtc::PacketOptions& options)
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
		tmr_start(&tmr, MILLISECONDS_PER_SECOND,
			  tmr_transport_handler, this);
	}

private:
	struct voe_channel *ve;
	bool active;
	bool rtp_started;
	struct tmr tmr;
	struct le le;
};


static void tmr_transport_handler(void *arg)
{
	VoETransport *tp = (VoETransport *)arg;

	delete tp;
}


static void setup_external_audio_device()
{
	if(gvoe.adm){
		return;
	}
#if ZETA_USE_EXTERNAL_AUDIO_DEVICE
 #if TARGET_OS_IPHONE
	webrtc::audio_io_ios* ap = new webrtc::audio_io_ios();
	voe_register_adm((void*)ap);
 #elif defined(ANDROID)
	// Not Supported yet
 #else // Desktop
	webrtc::audio_io_osx* ap = new webrtc::audio_io_ios();
	voe_register_adm((void*)ap);
#endif
#endif
}

static void clean_external_audio_device()
{
	if(!gvoe.adm){
		return;
	}
#if USING_EXTERNAL_AUDIO_DEVICE
 #if TARGET_OS_IPHONE
	webrtc::audio_io_ios* ap = (webrtc::audio_io_ios*)gvoe.adm;
	delete ap;
 #elif defined(ANDROID)
	// Not Supported yet
 #else // Desktop
	webrtc::audio_io_osx* ap = (webrtc::audio_io_osx*)gvoe.adm;
	delete ap
 #endif
#endif
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

	if (gvoe.nch == 1)
		// This means we are ending a call. But voe_set_mute dosnt work when nch == 0
		voe_set_mute(false);
	if (gvoe.nch > 0)
		--gvoe.nch;
	if (gvoe.nch == 0) {
		info("voe: Last Channel deleted call voe.base->Terminate()\n");

		voe_stop_audio_proc(&gvoe);

		voe_stop_silencing();

		voe_stop_audio_test(&gvoe);
        
		gvoe.base->Terminate();
        
		if(gvoe.autest.fad){
			info("voe: Deleting fake audio device \n");
			voe_deregister_adm();
			delete gvoe.autest.fad;
		}
		clean_external_audio_device();
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

static void voe_start_rtp_dump(struct voe_channel *ve)
{
	if ( gvoe.path_to_files.length() ) {
#if TARGET_OS_IPHONE
		std::string prefix = "/Ios_";
#elif defined(ANDROID)
		std::string prefix = "/Android_";
#else
		std::string prefix = "Osx_";
#endif
		std::string file_in, file_out;
        
		file_in.insert(0, gvoe.path_to_files);
		file_in.insert(file_in.size(),prefix);
		file_in.insert(file_in.size(),"packets_in_");
		file_out.insert(0, gvoe.path_to_files);
		file_out.insert(file_out.size(),prefix);
		file_out.insert(file_out.size(),"packets_out_");
        
		char  buf[80];
		sprintf(buf,"ch%d_", ve->ch);
		file_in.insert(file_in.size(),buf);
		file_out.insert(file_out.size(),buf);
        
		time_t     now = time(0);
		struct tm  tstruct;
        
		tstruct = *localtime(&now);
		strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
        
		file_in.insert(file_in.size(),buf);
		file_in.insert(file_in.size(),".rtpdump");
		file_out.insert(file_out.size(),buf);
		file_out.insert(file_out.size(),".rtpdump");
        
		if(ve->rtp_dump_in){
			ve->rtp_dump_in->Start(file_in.c_str());
		}
		if(ve->rtp_dump_out){
			ve->rtp_dump_out->Start(file_out.c_str());
		}
	}
}

int voe_ve_alloc(struct voe_channel **vep, const struct aucodec *ac,
                 uint32_t srate, int pt)
{
	struct voe_channel *ve;
	webrtc::CodecInst c;
	int err = 0;
	int bitrate_bps;
	int packet_size_ms;

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
		if (avs_get_flags() & AVS_FLAG_AUDIO_TEST){
			info("voe: Using fake audio device \n");
			gvoe.autest.fad = new webrtc::fake_audiodevice(true);
			voe_register_adm((void*)gvoe.autest.fad);
		}        
		setup_external_audio_device();
        
		info("voe: First Channel created call voe.base->Init() \n");

		if (!gvoe.base) {
			warning("voe: no gvoe base!\n");
			err = ENOSYS;
			goto out;
		}

		gvoe.base->Init(gvoe.adm);

		voe_start_audio_proc(&gvoe);

		voe_get_mute(&gvoe.isMuted);

		voe_start_silencing();
        
		gvoe.packet_size_ms = 20;
        
		gvoe.in_vol_smth = 0.0f;
		gvoe.in_vol_max = 0;
		gvoe.out_vol_max = 0;
	}

	if (avs_get_flags() & AVS_FLAG_AUDIO_TEST){
		voe_start_audio_test(&gvoe);
	}
    
	bitrate_bps = gvoe.manual_bitrate_bps ? gvoe.manual_bitrate_bps : gvoe.bitrate_bps;
	packet_size_ms = gvoe.manual_packet_size_ms ? gvoe.manual_packet_size_ms :
        std::max( gvoe.packet_size_ms, gvoe.min_packet_size_ms );
    
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
    
	c.pacsize = (c.plfreq * packet_size_ms) / 1000;
	c.rate = bitrate_bps;
    
	gvoe.codec->SetSendCodec(ve->ch, c);
	gvoe.codec->SetRecPayloadType(ve->ch, c);

	gvoe.codec->SetFECStatus( ve->ch, ZETA_USE_INBAND_FEC );
    
	gvoe.codec->SetOpusDtx( ve->ch, ZETA_USE_DTX );
    
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
