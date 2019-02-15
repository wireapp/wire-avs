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
#include "webrtc/base/logging.h"
#include <vector>
#include <time.h>

extern "C" {
	#include "avs_log.h"
	#include "avs_string.h"
	#include "avs_aucodec.h"
}

#include "voe.h"

#define NUM_CODECS 1

struct load_profile {
    int  bitrate_bps_;
    int  packet_size_ms_;
    bool using_dtx_;
};


/* Global VOE data */
struct voe gvoe;


class VoETransport;


struct media_ctx {
	struct voe_channel *ve;
};




struct mq_err_data {
	int ch;
	int err;
};

static void mq_callback(int id, void *data, void *arg)
{
	switch(id) {

	case VOE_MQ_ERR: {
		struct mq_err_data *med = (struct mq_err_data *)data;
		struct le *le;

		le = gvoe.encl.head;
		while (le) {
			struct auenc_state *aes;

			aes = (struct auenc_state *)list_ledata(le);
			le = le->next;
			if (!aes)
				continue;

			if (med->ch == -1
			    || (aes->ve && aes->ve->ch == med->ch)) {
				if (aes->errh)
					aes->errh(med->err, NULL, aes->arg);
			}
		}

		le = gvoe.decl.head;
		while (le) {
			struct audec_state *ads;

			ads = (struct audec_state *)list_ledata(le);
			le = le->next;
			if (!ads)
				continue;

			if (med->ch == -1
			    || (ads->ve && ads->ve->ch == med->ch)) {
				if (ads->errh)
					ads->errh(med->err, NULL, ads->arg);
			}
		}

		mem_deref(med);
		break;
    }
        
    }
}








class VoELogCallback : public webrtc::TraceCallback {
public:
	VoELogCallback() {};
	virtual ~VoELogCallback() {};

	virtual void Print(webrtc::TraceLevel lvl, const char* message,
			   int len) override
	{
		size_t sz = (size_t)((len > 0) ? len - 1 : 0);
		
		if (lvl & (webrtc::kTraceCritical | webrtc::kTraceError))
			error("%b\n", message, sz);
		else if (lvl & webrtc::kTraceWarning)
			warning("%b\n", message, sz);
		else
			debug("%b\n", message, sz);
	};
};


static VoELogCallback logCb;

class MyObserver : public webrtc::VoiceEngineObserver {
public:
    virtual void CallbackOnError(int channel, int err_code);
};

void MyObserver::CallbackOnError(int channel, int err_code)
{
	bool call_errh = false;
	std::string msg;

	// Add printf for other error codes here
	if (err_code == VE_RECEIVE_PACKET_TIMEOUT) {
		msg = "VE_RECEIVE_PACKET_TIMEOUT\n"; 
	} else if (err_code == VE_PACKET_RECEIPT_RESTARTED) {
		msg = "VE_PACKET_RECEIPT_RESTARTED\n";
	} else if (err_code == VE_RUNTIME_PLAY_WARNING) {
		msg = "VE_RUNTIME_PLAY_WARNING\n";
	} else if (err_code == VE_RUNTIME_REC_WARNING) {
		msg = "VE_RUNTIME_REC_WARNING\n";
	} else if (err_code == VE_SATURATION_WARNING) {
		msg = "VE_SATURATION_WARNING\n";
	} else if (err_code == VE_RUNTIME_PLAY_ERROR) {
		call_errh = true;
		msg = "VE_RUNTIME_PLAY_ERROR\n";
	} else if (err_code == VE_RUNTIME_REC_ERROR) {
		call_errh = true;
		msg = "VE_RUNTIME_REC_ERROR\n";
	} else if (err_code == VE_REC_DEVICE_REMOVED) {
		msg = "VE_REC_DEVICE_REMOVED\n";
	}

	info((std::string("voe::CallbackOnError:") + msg).c_str());

	if (call_errh) {
		struct mq_err_data *med;

		med = (struct mq_err_data *)mem_zalloc(sizeof(*med), NULL);
		if (med) {
			med->ch = channel;
			med->err = err_code;
			mqueue_push(gvoe.mq, VOE_MQ_ERR, med);
		}
	}
}


static MyObserver my_observer;

void voe_update_conf_parts(const struct audec_state *adsv[], size_t adsc)
{
	std::list<int> confl;
	size_t i;

	info("voe: update_conf_parts: adsv=%p adsc=%zu\n", adsv, adsc);

	/* Map mediaflows to channels, used by the conferencing positioner */
	for (i=0; i<adsc; i++) {
		const struct audec_state *ads = adsv[i];

		if (ads && ads->ve)
			confl.push_back(ads->ve->ch);
	}
}

static bool all_interrupted()
{
	int ch_interrupted = 0;
	int nch = 0;

	if(nch == 0){
		return false;
	}
    
	struct le *le;
	for (le = gvoe.channel_data_list.head; le; le = le->next) {
		struct channel_data *cd = (struct channel_data *)le->data;
        
		nch++;
		if(cd->interrupted){
			ch_interrupted++;
		}
	}
    
	return(ch_interrupted == nch);
}

static void set_interrupted(int ch, bool interrupted)
{
	if(!interrupted && all_interrupted()){
		info("Interruption stopped \n");
		if(gvoe.state.chgh){
			gvoe.state.chgh(FLOWMGR_AUDIO_INTERRUPTION_STOPPED,
					gvoe.state.arg);
		}
	}
    
	struct channel_data *cd = find_channel_data(&gvoe.channel_data_list, ch);
	if(cd){
		cd->interrupted = interrupted;
	}
}


#if 0
static void tmr_rtp_timeout_handler(void *arg)
{
	struct audec_state *ads = (struct audec_state *)arg;
    
	set_interrupted(ads->ve->ch, true);
	if(all_interrupted()){
		info("Interruption started \n");
		if(gvoe.state.chgh){
			gvoe.state.chgh(FLOWMGR_AUDIO_INTERRUPTION_STARTED,
					gvoe.state.arg);
		}
	}
}
#endif


static int rtp_handler(struct audec_state *ads,
		       const uint8_t *pkt, size_t len)
{
	if (!ads || !pkt || !len)
		return EINVAL;

	if(gvoe.nch == 0){
		// Voiceengine is not initialized
		return 0;
	}
	    
	if (gvoe.nw){
		set_interrupted(ads->ve->ch, false);

		gvoe.nw->ReceivedRTPPacket(ads->ve->ch, pkt, len);

#if FORCE_AUDIO_RTP_RECORDING
		if(ads->ve->rtp_dump_in){
			ads->ve->rtp_dump_in->DumpPacket(pkt, len);
		}
#endif
	}
        
	return 0;
}

static int rtcp_handler(struct audec_state *ads,
			const uint8_t *pkt, size_t len)
{
	if (!ads || !pkt || !len)
		return EINVAL;

	if(gvoe.nch == 0){
		// Voiceengine is not initialized
		return 0;
	}
    
	if (gvoe.nw)
		gvoe.nw->ReceivedRTCPPacket(ads->ve->ch, pkt, len);
    
	if (!gvoe.rtp_rtcp)
		return 0;
    
	webrtc::CallStatistics stats;
	int rtt_ms = 0, frac_lost_Q8 = 0;
    
	if (0 == gvoe.rtp_rtcp->GetRTCPStatistics(ads->ve->ch, stats)) {
		unsigned int NTPHigh = 0, NTPLow = 0, timestamp = 0, playoutTimestamp = 0, jitter = 0;
		unsigned short fractionLostUp_Q8 = 0; // Uplink packet loss as reported by remote side
		gvoe.rtp_rtcp->GetRemoteRTCPData( ads->ve->ch, NTPHigh, NTPLow, timestamp, playoutTimestamp, &jitter, &fractionLostUp_Q8);
		debug("voe: Channel %d RTCP:  RTT = %d ms; uplink packet loss perc = %d downlink packet loss perc = %d\n", ads->ve->ch, stats.rttMs, (int)(fractionLostUp_Q8/2.55f+0.5f), (int)(stats.fractionLost/2.55f+0.5f));
        
        voe_update_channel_stats(&gvoe, ads->ve->ch, stats.rttMs, fractionLostUp_Q8);
    }
	return 0;
}

static char fmtp_no_cbr[] = "stereo=0;sprop-stereo=0;useinbandfec=1";
static char fmtp_cbr[] = "stereo=0;sprop-stereo=0;useinbandfec=1;cbr=1";

static struct aucodec voe_aucodecv[NUM_CODECS] = {
	{
		.le        = LE_INIT,
		.pt        = "111",
		.name      = "opus",
		.srate     = 48000,
		.ch        = 2,
		.fmtp      = fmtp_no_cbr,
		.fmtp_cbr  = fmtp_cbr,

		.enc_alloc = voe_enc_alloc,
		.enc_start = voe_enc_start,
		.enc_stop  = voe_enc_stop,

		.dec_alloc = voe_dec_alloc,
		.dec_rtph  = rtp_handler,
		.dec_rtcph = rtcp_handler,
		.dec_start = voe_dec_start,
		.dec_stop  = voe_dec_stop,
		.get_stats = voe_get_stats,
	}
};


webrtc::CodecInst *find_codec(const char *name)
{
	bool found = false;
	webrtc::CodecInst *c;
	size_t i;

	for (i = 0; i < gvoe.ncodecs && !found; ++i) {
		c = &gvoe.codecs[i];
		found = streq(c->plname, name);
	}

	return found ? c : NULL;
}


void voe_close(void)
{
	info("voe: module close\n");

	tmr_cancel(&gvoe.tmr_neteq_stats);

	if (gvoe.codec) {
		gvoe.codec->Release();
		gvoe.codec = NULL;
	}
	if (gvoe.volume) {
		gvoe.volume->Release();
		gvoe.volume = NULL;
	}
	if (gvoe.nw) {
		gvoe.nw->Release();
		gvoe.nw = NULL;
	}
	if (gvoe.file) {
		gvoe.file->Release();
		gvoe.file = NULL;
	}
	if (gvoe.processing) {
		gvoe.processing->Release();
		gvoe.processing = NULL;
	}
	if (gvoe.rtp_rtcp) {
		gvoe.rtp_rtcp->Release();
		gvoe.rtp_rtcp = NULL;
	}
	if (gvoe.neteq_stats) {
		gvoe.neteq_stats->Release();
		gvoe.neteq_stats = NULL;
	}
	if (gvoe.hw) {
		gvoe.hw->Release();
		gvoe.hw = NULL;
	}
    if (gvoe.external_media) {
        gvoe.external_media->Release();
        gvoe.external_media = NULL;
    }
    
	for (int i = 0; i < NUM_CODECS; ++i) {
		struct aucodec *ac = &voe_aucodecv[i];

		aucodec_unregister(ac);
	}

	if (gvoe.codecs) {
		delete[] gvoe.codecs;
		gvoe.codecs = NULL;
	}
    
	if (gvoe.base) {
		gvoe.base->DeRegisterVoiceEngineObserver();
		gvoe.base->Terminate();
		gvoe.base->Release();
		gvoe.base = NULL;
	}

	webrtc::VoiceEngine::Delete(gvoe.ve);
	gvoe.ve = NULL;

	webrtc::Trace::ReturnTrace();

	gvoe.mq = (struct mqueue *)mem_deref(gvoe.mq);
    
	list_flush(&gvoe.channel_data_list);

	gvoe.playout_device = (char *)mem_deref(gvoe.playout_device);
	gvoe.path_to_files = (char *)mem_deref(gvoe.path_to_files);

	info("voe: module unloaded\n");

	voe_transportl_flush();
}


int voe_init(struct list *aucodecl)
{
	size_t i;
	int err = 0;

	info("voe: module_init \n");

#if 1
	rtc::LogMessage::SetLogToStderr(false);
#endif

#if 1
	webrtc::Trace::CreateTrace();
	webrtc::Trace::SetTraceCallback(&logCb);
	webrtc::Trace::set_level_filter(webrtc::kTraceWarning | webrtc::kTraceError | webrtc::kTraceCritical);
#endif
    
	memset(&gvoe, 0, sizeof(gvoe));

	gvoe.ve = webrtc::VoiceEngine::Create();
	if (!gvoe.ve) {
		err = ENOMEM;
		goto out;
	}

	gvoe.base  = webrtc::VoEBase::GetInterface(gvoe.ve);
	if (!gvoe.base) {
		err = ENOENT;
		goto out;
	}

	gvoe.nw    = webrtc::VoENetwork::GetInterface(gvoe.ve);
	if (!gvoe.nw) {
		err = ENOENT;
		goto out;
	}

	gvoe.volume = webrtc::VoEVolumeControl::GetInterface(gvoe.ve);
	if (!gvoe.volume) {
		err = ENOENT;
		goto out;
	}

	gvoe.codec = webrtc::VoECodec::GetInterface(gvoe.ve);
	if (!gvoe.codec) {
		err = ENOENT;
		goto out;
	}

	gvoe.file = webrtc::VoEFile::GetInterface(gvoe.ve);
	if (!gvoe.file) {
		err = ENOENT;
		goto out;
	}

	gvoe.processing = webrtc::VoEAudioProcessing::GetInterface(gvoe.ve);
	if (!gvoe.processing) {
		err = ENOENT;
		goto out;
	}

	gvoe.rtp_rtcp = webrtc::VoERTP_RTCP::GetInterface(gvoe.ve);
	if (!gvoe.rtp_rtcp) {
		err = ENOENT;
		goto out;
	}

	gvoe.neteq_stats = webrtc::VoENetEqStats::GetInterface(gvoe.ve);
	if (!gvoe.neteq_stats) {
		err = ENOENT;
		goto out;
	}

	gvoe.hw = webrtc::VoEHardware::GetInterface(gvoe.ve);
	if (!gvoe.hw) {
		err = ENOENT;
		goto out;
	}

    gvoe.external_media = webrtc::VoEExternalMedia::GetInterface(gvoe.ve);
    if (!gvoe.external_media) {
        err = ENOENT;
        goto out;
    }
    
	list_init(&gvoe.transportl);

	err = mqueue_alloc(&gvoe.mq, mq_callback, NULL);
	if (err)
		goto out;

	list_init(&gvoe.encl);
	list_init(&gvoe.decl);

	/* list all supported codecs */

	gvoe.ncodecs = (size_t)gvoe.codec->NumOfCodecs();
	gvoe.codecs = new webrtc::CodecInst[gvoe.ncodecs];
	for (i = 0; i < gvoe.ncodecs; i++) {
		webrtc::CodecInst c;

		gvoe.codec->GetCodec(i, c);

		gvoe.codecs[i] = c;

		debug("voe: name=%s pt=%d freq=%d psz=%d chans=%d srate=%d\n",
		      c.plname, c.pltype, c.plfreq, c.pacsize,
		      c.channels, c.rate);
	}

	for (int i = 0; i < NUM_CODECS; ++i) {
		webrtc::CodecInst *c;
		struct aucodec *ac = &voe_aucodecv[i];

		if (!ac->name || !ac->srate || !ac->ch)
			continue;

		c = find_codec(ac->name);

		ac->data = (void *)c;

		aucodec_register(aucodecl, ac);

		info("voe: module_init: voe: registering %s(%d) -- %p\n",
		     ac->name, ac->srate, c);
	}

	gvoe.nch = 0;
	list_init(&gvoe.channel_data_list);
	gvoe.packet_size_ms = 20;
	gvoe.min_packet_size_ms = 20;
	gvoe.manual_packet_size_ms = 0;
	gvoe.bitrate_bps = ZETA_OPUS_BITRATE_HI_BPS;
	gvoe.manual_bitrate_bps = 0;
        
	gvoe.is_playing = false;
	gvoe.is_recording = false;
	gvoe.is_rtp_recording = false;

	gvoe.isMuted = false;
	gvoe.isSilenced = false;
    
	gvoe.aio = NULL;
    
	gvoe.state.chgh = NULL;
	gvoe.state.arg = NULL;
    
	gvoe.base->RegisterVoiceEngineObserver(my_observer);
    
#if TARGET_OS_IPHONE
	voe_set_file_path(voe_iosfilepath());
#elif defined(ANDROID)
	voe_set_file_path("/data/local/tmp");
#else
	voe_set_file_path("");
#endif
    
	str_dup(&gvoe.playout_device, "uninitialized");
    
	voe_init_audio_test(&gvoe.autest);
    
	gvoe.voe_audio_effect = NULL;
 out:
	if (err)
		voe_close();

	return err;
}

void voe_register_adm(struct audio_io *aio)
{
	if(gvoe.aio){
		error("voe: cant register adm allready registered \n");
	}
	gvoe.aio = aio;
}

void voe_deregister_adm()
{
	gvoe.aio = NULL;
}

int voe_invol(struct auenc_state *aes, double *invol)
{
	unsigned int level = 0;

	if (!aes)
		return EINVAL;

	gvoe.volume->GetSpeechInputLevelFullRange(level);
	*invol = (double)(level >> 5)/1024.0;
	
	gvoe.in_vol_smth += ((float)level - gvoe.in_vol_smth) * 0.05f;
    
	if(level > gvoe.in_vol_max){
		gvoe.in_vol_max = level;
	}
    
	return 0;
}


int voe_outvol(struct audec_state *ads, double *outvol)
{
	unsigned int level;

	if (!ads || !ads->ve)
		return EINVAL;

	gvoe.volume->GetSpeechOutputLevelFullRange(ads->ve->ch, level);
	*outvol = (double)(level >> 5)/1024.0;
	
    struct channel_data *cd = find_channel_data(&gvoe.channel_data_list, ads->ve->ch);
    if(cd){
        if(cd->out_vol_smth == -1.0f){
            cd->out_vol_smth = (float)level;
        } else {
            float tmp = cd->out_vol_smth;
            tmp = ((float)level - tmp) * 0.05f;
            cd->out_vol_smth = tmp;
        }
    }
    
	if(level > gvoe.out_vol_max){
		gvoe.out_vol_max = level;
	}
    
	return 0;
}


int voe_update_mute(struct voe *voe)
{
	int err;

	info("voe: update_mute: isMuted=%d, isSilenced=%d\n",
	     voe->isMuted,
	     voe->isSilenced);

	if (!voe->volume) {
		return ENOSYS;
	}
    
	if (voe->nch == 0) {
		/* We cannot set the mute state in VE as it is not created */
		return 0;
	}
    
	err = voe->volume->SetInputMute(-1, voe->isMuted || voe->isSilenced);
	if (err) {
		warning("voe_start_silencing: SetInputMute failed\n");
		return ENOSYS;
	}

	return 0;
}


int voe_start_silencing()
{
	debug("voe_start_silencing: \n");

	gvoe.isSilenced = true;
	
	return voe_update_mute(&gvoe);
}


int voe_stop_silencing()
{
	debug("voe_stop_silencing: isMuted=%d\n", gvoe.isMuted);

	gvoe.isSilenced = false;

	return voe_update_mute(&gvoe);
}


int voe_debug(struct re_printf *pf, void *unused)
{
	struct le *le;
	int err = 0;

	err |= re_hprintf(pf, " voe.nch:         %d\n", gvoe.nch);
	err |= re_hprintf(pf, " voe.active_chs:  %d\n", list_count(&gvoe.channel_data_list));

    for (le = gvoe.channel_data_list.head; le; le = le->next) {
        struct channel_data *cd = (struct channel_data *)le->data;
        
        int ch = cd->channel_number;
        
        err |= re_hprintf(pf, " ...channel=%d\n", ch);
    }
	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, " encoders (%u):\n", list_count(&gvoe.encl));
	for (le = gvoe.encl.head; le; le = le->next) {
		struct auenc_state *aes = (struct auenc_state *)le->data;
		if(aes->ve){
			err |= re_hprintf(pf, " ...%s channel=%d\n",
				  aes->ac->name, aes->ve->ch);
		}
	}
	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, " decoders (%u):\n", list_count(&gvoe.decl));
	for (le = gvoe.decl.head; le; le = le->next) {
		struct audec_state *ads = (struct audec_state *)le->data;
		if(ads->ve){
			err |= re_hprintf(pf, " ...%s channel=%d\n",
				  ads->ac->name, ads->ve->ch);
		}
	}
	err |= re_hprintf(pf, "\n");

	return err;
}

void voe_set_audio_state_handler(flowmgr_audio_state_change_h *state_chgh,
				 void *arg)
{
	gvoe.state.chgh = state_chgh;
	gvoe.state.arg = arg;
}

void voe_set_file_path(const char *path)
{
//    info("avs: setting path_to_files to %s \n", path);
    info("avs: setting path_to_files\n");
    gvoe.path_to_files = (char *)mem_deref(gvoe.path_to_files);
    str_dup(&gvoe.path_to_files, path);
}
