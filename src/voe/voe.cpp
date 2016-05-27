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
		else if (lvl & (webrtc::kTracePersist))
			info("%b\n", message, sz);
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

static void tmr_neteq_stats_handler(void *arg)
{
    struct znw_stats nwstat;
    struct voe *voe = (struct voe *)arg;
    
    if(voe->nws.size() > 0 && voe->nch > 0 && !voe->isSilenced){
#if NETEQ_LOGGING
        info("------ %d active channels ------- \n", voe->nws.size());
#endif
        for ( auto it = voe->nws.begin(); it < voe->nws.end(); it++) {

            nwstat.in_vol = 20 * log10(voe->in_vol_smth + 1.0f);
            nwstat.out_vol = 20 * log10(it->out_vol_smth_ + 1.0f);
            
            int ch_id = it->channel_number_;
            voe->neteq_stats->GetNetworkStatistics(ch_id, nwstat.neteq_nw_stats);
            
            webrtc::CallStatistics stats;
            voe->rtp_rtcp->GetRTCPStatistics(ch_id, stats);
            nwstat.Rtt_ms = stats.rttMs;
            nwstat.jitter_smpls = stats.jitterSamples;
            
            unsigned int NTPHigh = 0, NTPLow = 0, timestamp = 0, playoutTimestamp = 0, jitter = 0;
            unsigned short fractionLostUp_Q8 = 0; // Uplink packet loss as reported by remote side
            voe->rtp_rtcp->GetRemoteRTCPData( ch_id, NTPHigh, NTPLow, timestamp, playoutTimestamp, &jitter, &fractionLostUp_Q8);
            
            nwstat.uplink_loss_q8 = fractionLostUp_Q8;
            nwstat.uplink_jitter_smpls = jitter;
            
            memcpy(&it->nw_stats[it->idx_], &nwstat, sizeof(nwstat));
            it->idx_++;
            if(it->idx_ >= NUM_STATS){
               it->idx_ = 0;
            }
            it->n++;
#if NETEQ_LOGGING
            float pl_rate = ((float)nwstat.neteq_nw_stats.currentPacketLossRate)/163.84f; // convert Q14 -> float and fraction to percent
            float fec_rate = ((float)nwstat.neteq_nw_stats.currentSecondaryDecodedRate)/163.84f; // convert Q14 -> float and fraction to percent
            float exp_rate = ((float)nwstat.neteq_nw_stats.currentExpandRate)/163.84f;
            float acc_rate = ((float)nwstat.neteq_nw_stats.currentAccelerateRate)/163.84f;
            float dec_rate = ((float)nwstat.neteq_nw_stats.currentPreemptiveRate)/163.84f;
            info("ch# %d BufferSize = %d ms PacketLossRate = %.2f ExpandRate = %.2f fec_rate = %.2f AccelerateRate = %.2f DecelerateRate = %.2f \n", ch_id, nwstat.neteq_nw_stats.currentBufferSize, pl_rate, exp_rate, fec_rate, acc_rate, dec_rate);
#endif
        }
    }
    tmr_start(&voe->tmr_neteq_stats, NW_STATS_DELTA*MILLISECONDS_PER_SECOND, tmr_neteq_stats_handler, voe);
}


static const char *AGCmode2Str(webrtc::AgcModes AGCmode)
{
    switch (AGCmode) {
            
        case webrtc::kAgcDefault:                 return "Default";
        case webrtc::kAgcAdaptiveAnalog:          return "AdaptiveAnalog";
        case webrtc::kAgcAdaptiveDigital:         return "AdaptiveDigital";
        case webrtc::kAgcFixedDigital:            return "FixedDigital";
        default: return "?";
    }
}

static const char *ECmode2Str(webrtc::EcModes ECmode)
{
	switch (ECmode) {
            
        case webrtc::kEcDefault:                  return "Default";
        case webrtc::kEcConference:               return "Aec (Agressive)";
        case webrtc::kEcAec:                      return "Aec";
        case webrtc::kEcAecm:                     return "Aecm";
        default: return "?";
	}
}


static const char *AECMmode2Str(webrtc::AecmModes AECMmode)
{
	switch (AECMmode) {
            
        case webrtc::kAecmQuietEarpieceOrHeadset: return "QuietEarpieceOrHeadset";
        case webrtc::kAecmEarpiece:               return "Earpiece";
        case webrtc::kAecmLoudEarpiece:           return "LoudEarpiece";
        case webrtc::kAecmSpeakerphone:           return "Speakerphone";
        case webrtc::kAecmLoudSpeakerphone:       return "LoudSpeakerphone";
        default: return "?";
	}
}


static const char *NSmode2Str(webrtc::NsModes NSMode)
{
	switch (NSMode) {
            
        case webrtc::kNsDefault:                  return "Default";
        case webrtc::kNsConference:               return "Conference";
        case webrtc::kNsLowSuppression:           return "LowSuppression";
        case webrtc::kNsModerateSuppression:      return "ModerateSuppression";
        case webrtc::kNsHighSuppression:          return "HighSuppression";
        case webrtc::kNsVeryHighSuppression:      return "VeryHighSuppression";
        default: return "?";
	}
}


void voe_start_audio_proc(struct voe *voe)
{
	int ret = 0;
	bool enabled;
	webrtc::EcModes ECmode;
	webrtc::AgcModes AGCmode;
	webrtc::AecmModes AECMmode;
	webrtc::NsModes NSMode;
	auto proc = voe->processing;

	if (!proc)
		return;

	/* SetUp HP filter */
	ret = proc->EnableHighPassFilter( ZETA_USE_HP );
    
	/* SetUp AGC */
	ret = proc->SetAgcStatus( ZETA_USE_AGC_SPEAKER, ZETA_AGC_MODE_SPEAKER);

	/* SetUp AEC */
    
    /* Does the device have build in AEC ?*/
#ifdef ZETA_USE_BUILD_IN_AEC
	bool build_in_aec = voe->hw->BuiltInAECIsAvailable();
#else
	bool build_in_aec = false;
#endif
	if (build_in_aec){
		info("voe: using build in AEC !! \n");
		voe->hw->EnableBuiltInAEC(true);
	}
	else {
		ret = proc->SetEcStatus( ZETA_USE_AEC_SPEAKER, ZETA_AEC_MODE);
		if(ZETA_AEC_DELAY_CORRECTION){
			webrtc::Config config;
			config.Set<webrtc::ExtendedFilter>(new webrtc::ExtendedFilter(ZETA_AEC_DELAY_CORRECTION));
			voe->base->audio_processing()->SetExtraOptions(config); // Not Supported by real API but can be found by going through base API
		}
		if(ZETA_AEC_DELAY_AGNOSTIC){
			webrtc::Config config;
			config.Set<webrtc::DelayAgnostic>(new webrtc::DelayAgnostic(ZETA_AEC_DELAY_AGNOSTIC));
			voe->base->audio_processing()->SetExtraOptions(config); // Not Supported by real API but can be found by going through base API
		}
        
		// If AECM used set agressivness and use of comfort noise
		ret = proc->GetEcStatus(enabled, ECmode);
		if ( enabled && ECmode == webrtc::kEcAecm) {
			proc->SetAecmMode( ZETA_AECM_MODE_SPEAKER, ZETA_AECM_CNG );
		}
	}
	/* Setup Noise Supression */
	ret = proc->SetNsStatus( ZETA_USE_NS, ZETA_NS_MODE_SPEAKER );
	
	info("voe: -- Preproc Settings -- \n");

	if(proc->IsHighPassFilterEnabled()){
		info("voe: Microphone High Pass filter enabled \n");
	}
	ret = proc->GetAgcStatus( enabled, AGCmode);
	if (enabled){
		info("voe: AGC enabled in mode %s \n", AGCmode2Str(AGCmode));
	}
	else {
		info("voe: AGC disabled \n");
	}
	ret = proc->GetEcStatus(enabled, ECmode);
	if ( enabled ) {
		if ( ECmode == webrtc::kEcAecm) {
			ret = proc->GetAecmMode( AECMmode, enabled );
			info("voe: AECM enabled in mode %s CNG = %d \n",
			     AECMmode2Str(AECMmode), enabled);
		} else {
			info("voe: AEC enabled in mode %s \n", ECmode2Str(ECmode));
			if(ZETA_AEC_DELAY_CORRECTION){
				info("voe: AEC in extended filter mode \n");
			}
		}
	} else {
		info("voe: AEC disabled \n");
	}

	ret = proc->GetNsStatus( enabled, NSMode );
	if (enabled){
		info("voe: Noise Supression enabled in mode %s \n", NSmode2Str(NSMode));
	} else {
		info("voe: Noise Supression disabled \n");
	}
	info(" ---------------------- \n");

	char strNameUTF8[128];
	if(!voe->hw->GetPlayoutDeviceName(0, strNameUTF8, NULL)){
		voe_set_auplay(strNameUTF8);
    }
    
	/* Enable stereo conferencing if the device supports stereo playback */
	bool stereoAvailable;
	voe->conferencing->SupportsStereo(stereoAvailable);
	if (stereoAvailable){
		info("voe: stereo playout available \n");
	}
	else {
		info("voe: stereo playout not available \n");
	}

	voe->conferencing->SetUseStereoConf(stereoAvailable);

#if FORCE_RECORDING
        if( voe->path_to_files ){
#if TARGET_OS_IPHONE
		std::string prefix = "/Ios_";
#elif defined(ANDROID)
		std::string prefix = "/Android_";
#else
		std::string prefix = "Osx_";
#endif
		std::string file;
            
		file.insert(0,voe->path_to_files);
		file.insert(file.size(),prefix);
		file.insert(file.size(),"apm_");
        
		char  buf[80];
		time_t     now = time(0);
		struct tm  tstruct;
            
		tstruct = *localtime(&now);
		strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
            
		//file.insert(0,fileNameUTF8);
		file.insert(file.size(),buf);
		file.insert(file.size(),".aecdump");
            
		voe_start_preproc_recording(file.c_str());
        }
#endif
}


void voe_stop_audio_proc(struct voe *voe)
{
	int ret;
	auto proc = voe->processing;

	if (!proc)
		return;

	// Disable AEC
	ret = proc->SetEcStatus(false);

	// Disable AGC
	ret = proc->SetAgcStatus(false);

	// Disable NS
	ret = proc->SetNsStatus(false);
}


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

	if (gvoe.conferencing)
		gvoe.conferencing->UpdateConference(confl);
}

static bool all_interrupted()
{
	int ch_interrupted = 0;
	int nch = 0;
	for( auto it = gvoe.active_channel_settings.begin();
		it < gvoe.active_channel_settings.end(); it++) {
        
		nch++;
		if(it->interrupted_){
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
    
	for( auto it = gvoe.active_channel_settings.begin();
		it < gvoe.active_channel_settings.end(); it++) {
        
		if( it->channel_number_ == ch ) {
			it->interrupted_ = interrupted;
			break;
		}
	}
}

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
		tmr_cancel(&ads->tmr_rtp_timeout);
		tmr_start(&ads->tmr_rtp_timeout, 2*MILLISECONDS_PER_SECOND,tmr_rtp_timeout_handler, ads);
        
		gvoe.nw->ReceivedRTPPacket(ads->ve->ch, pkt, len);
	}
        
	return 0;
}

#define SWITCH_TO_SHORTER_PACKETS_RTT_MS  500
#define SWITCH_TO_LONGER_PACKETS_RTT_MS   800

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
        for( auto it = gvoe.active_channel_settings.begin();
             it < gvoe.active_channel_settings.end(); it++) {
            
            if( it->channel_number_ == ads->ve->ch ) {
                it->last_rtcp_rtt = stats.rttMs;
                it->last_rtcp_ploss = fractionLostUp_Q8;
            }
            rtt_ms = std::max(rtt_ms, it->last_rtcp_rtt);
            frac_lost_Q8 = std::max(frac_lost_Q8, it->last_rtcp_ploss);
        }
        int packet_size_ms = gvoe.packet_size_ms;
        if( rtt_ms < SWITCH_TO_SHORTER_PACKETS_RTT_MS && frac_lost_Q8 < (int)(0.03 * 255) ) {
            packet_size_ms -= 20;
        } else
        if( rtt_ms > SWITCH_TO_LONGER_PACKETS_RTT_MS || frac_lost_Q8 > (int)(0.10 * 255) ) {
            packet_size_ms += 20;
        }
        packet_size_ms = std::max( packet_size_ms, gvoe.min_packet_size_ms );
        packet_size_ms = std::min( packet_size_ms, 40 );
        if( packet_size_ms != gvoe.packet_size_ms ) {
            gvoe.packet_size_ms = packet_size_ms;
            gvoe.bitrate_bps = packet_size_ms == 20 ? ZETA_OPUS_BITRATE_HI_BPS : ZETA_OPUS_BITRATE_LO_BPS;
            voe_set_channel_load(&gvoe);
        }
    }
	return 0;
}


static struct aucodec voe_aucodecv[NUM_CODECS] = {
	{
		.name      = "opus",
		.srate     = 48000,
		.ch        = 2,
		.fmtp      = "stereo=0;sprop-stereo=0;useinbandfec=1",
		.has_rtp   = true,

		.enc_alloc = voe_enc_alloc,
		.ench      = NULL,
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
	if (gvoe.conferencing) {
		gvoe.conferencing->Release();
		gvoe.conferencing = NULL;
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

	tmr_cancel(&gvoe.tmr_neteq_stats);
    
	gvoe.mq = (struct mqueue *)mem_deref(gvoe.mq);

	info("voe: module unloaded\n");

	voe_transportl_flush();
}


int voe_init(struct list *aucodecl)
{
	size_t i;
	int err = 0;

	info("voe: module_init \n");

#if 1
	webrtc::Trace::CreateTrace();
	webrtc::Trace::SetTraceCallback(&logCb);
	webrtc::Trace::set_level_filter(webrtc::kTraceWarning | webrtc::kTraceError | webrtc::kTraceCritical | webrtc::kTracePersist);
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

	gvoe.conferencing = webrtc::VoEConfControl::GetInterface(gvoe.ve);
	if (!gvoe.conferencing) {
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
	gvoe.active_channel_settings.clear();
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
    
	gvoe.adm = NULL;
    
	gvoe.state.chgh = NULL;
	gvoe.state.arg = NULL;
    
	gvoe.base->RegisterVoiceEngineObserver(my_observer);
    
#if TARGET_OS_IPHONE
    gvoe.path_to_files = voe_iosfilepath();
#elif defined(ANDROID)
    gvoe.path_to_files = "/data/local/tmp";
#else
    gvoe.path_to_files = "";
#endif
    
    gvoe.playout_device = "uninitialized";
    
    tmr_start(&gvoe.tmr_neteq_stats, MILLISECONDS_PER_SECOND, tmr_neteq_stats_handler, &gvoe);
    
    voe_vm_init(&gvoe.vm);
    
    voe_init_audio_test(&gvoe.autest);
 out:
	if (err)
		voe_close();

	return err;
}

void voe_register_adm(void* adm)
{
	gvoe.adm = (webrtc::AudioDeviceModule*)adm;
}

void voe_deregister_adm()
{
	gvoe.adm = NULL;
}

int voe_set_auplay(const char *dev)
{
	bool enabled;
	webrtc::EcModes ECmode;
	webrtc::AgcModes AGCmode;
    bool should_reset = false;
    
	auto voeProc = gvoe.processing;
    
	if((streq(gvoe.playout_device.c_str(), "headset") || streq(dev, "headset")) && !streq(gvoe.playout_device.c_str(), dev)) {
		should_reset = true;
	}

    info("voe: Playout device switched from %s to %s \n",
        gvoe.playout_device.c_str(), dev);
    
    gvoe.playout_device = dev;
    
	if (gvoe.nch == 0) {
		return 0;
	}
    
	if(should_reset){
		info("voe: Reset Audio Device\n");
		gvoe.hw->ResetAudioDevice();
	}
        
    /* Setup AEC based on the routing */
    voe_update_aec_settings(&gvoe);

    /* Setup AGC based on the routing and if in a conference or not */
    voe_update_agc_settings(&gvoe);
    
	/* Enable stereo conferencing if the device supports stereo playback */
	bool stereoAvailable;
	gvoe.conferencing->SupportsStereo(stereoAvailable);
    info("voe: stereoAvailable = %d \n", stereoAvailable);
	if( stereoAvailable ) {
		gvoe.conferencing->SetUseStereoConf(true);
	}
    
	return 0;
}

void voe_update_aec_settings(struct voe *voe)
{
    int ret;
    auto voeProc = voe->processing;

    if (!voeProc)
	    return;

    if (streq(voe->playout_device.c_str(), "speaker")) {
        info("voe: Setup aec settings for speakerphone \n");
        int ret = voeProc->SetEcStatus(ZETA_USE_AEC_SPEAKER, ZETA_AEC_MODE);
        if ( ZETA_USE_AEC_SPEAKER && ZETA_AEC_MODE == webrtc::kEcAecm) {
            ret += voeProc->SetAecmMode(ZETA_AECM_MODE_SPEAKER, false);
            info("voe: Change AECM mode to %d \n", ZETA_AECM_MODE_SPEAKER);
        }
    }
    else if (streq(voe->playout_device.c_str(), "earpiece") || streq(voe->playout_device.c_str(), "bt")) {
        info("voe: Setup aec settings for earpiece \n");
        int ret = voeProc->SetEcStatus(ZETA_USE_AEC_EARPIECE, ZETA_AEC_MODE);
        if ( ZETA_USE_AEC_EARPIECE && ZETA_AEC_MODE == webrtc::kEcAecm) {
            ret += voeProc->SetAecmMode(ZETA_AECM_MODE_EARPIECE, false);
            info("voe: Change AECM mode to %d \n", ZETA_AECM_MODE_EARPIECE);
        }
    }
    else if (streq(voe->playout_device.c_str(), "headset")) {
        info("voe: Setup aec settings for headset \n");
        /* Setup AEC (Reset by disabeling + enabeling) */
        int ret = voeProc->SetEcStatus(ZETA_USE_AEC_HEADSET, ZETA_AEC_MODE);
        if ( ZETA_USE_AEC_HEADSET && ZETA_AEC_MODE == webrtc::kEcAecm) {
            ret += voeProc->SetAecmMode(ZETA_AECM_MODE_HEADSET, false);
            info("voe: Change AECM mode to %d \n", ZETA_AECM_MODE_HEADSET);
        }
    }

}

void voe_update_agc_settings(struct voe *voe)
{
    bool enabled, use_agc = true;
    webrtc::AgcModes AGCmode, wantedAGCmode = ZETA_AGC_MODE_EARPIECE;
    webrtc::AgcConfig AGCconfig;
    unsigned short wanted_digCompresGaindB;
    int ret;
    auto voeProc = voe->processing;
    
    /* Get current AGC settings */
    ret = voeProc->GetAgcStatus(enabled , AGCmode);
    ret = voeProc->GetAgcConfig(AGCconfig);
    
    wanted_digCompresGaindB = AGCconfig.digitalCompressionGaindB;
    
    if (streq(voe->playout_device.c_str(), "speaker")) {
        use_agc = ZETA_USE_AGC_SPEAKER;
        if(voe->active_channel_settings.size() > 1){ /* In a conference we have more than one active channel */
            info("voe: Setup agc settings for speakerphone + conference call \n");
            wantedAGCmode = ZETA_AGC_MODE_SPEAKER_CONF;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER_CONF;
        }else{
            info("voe: Setup agc settings for speakerphone + one-one call \n");
            wantedAGCmode = ZETA_AGC_MODE_SPEAKER;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER;
        }
    }
    else if (streq(voe->playout_device.c_str(), "earpiece") || streq(voe->playout_device.c_str(), "bt")) {
        use_agc = ZETA_USE_AGC_EARPIECE;
        if(voe->active_channel_settings.size() > 1){
            info("voe: Setup agc settings for earpiece + conference call \n");
            wantedAGCmode = ZETA_AGC_MODE_EARPIECE_CONF;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE_CONF;
        }else{
            info("voe: Setup agc settings for earpiece + one-one call \n");
            wantedAGCmode = ZETA_AGC_MODE_EARPIECE;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE;
        }
    }
    else if (streq(voe->playout_device.c_str(), "headset")) {
        use_agc = ZETA_USE_AGC_HEADSET;
        if(voe->active_channel_settings.size() > 1){
            info("voe: Setup agc settings for headset + conference call \n");
            wantedAGCmode = ZETA_AGC_MODE_HEADSET_CONF;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET_CONF;
        }else{
            info("voe: Setup agc settings for headset + one-one call \n");
            wantedAGCmode = ZETA_AGC_MODE_HEADSET;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET;
        }
    }

    /* Update AGC Mode */
    if( AGCmode != wantedAGCmode || enabled != use_agc){
        ret = voeProc->SetAgcStatus(false , wantedAGCmode);
        ret = voeProc->SetAgcStatus(use_agc, wantedAGCmode);
    }

    /* Update Fixed Digital Settings */
    if(wanted_digCompresGaindB != AGCconfig.digitalCompressionGaindB
       && wanted_digCompresGaindB != AGC_COMPRESSION_GAIN_IGNORE){
        
        AGCconfig.digitalCompressionGaindB = wanted_digCompresGaindB;
        ret = voeProc->SetAgcConfig(AGCconfig);
    }
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
	
	for ( auto it = gvoe.nws.begin(); it != gvoe.nws.end(); it++) {
		if ( it->channel_number_ == ads->ve->ch) {
			if(it->out_vol_smth_ == -1.0f){
				it->out_vol_smth_ = (float)level;
			} else {
				float tmp = it->out_vol_smth_;
				tmp = ((float)level - tmp) * 0.05f;
				it->out_vol_smth_ = tmp;
			}
			break;
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
    
	err = voe->volume->SetInputMute(-1, voe->isMuted, voe->isSilenced);
	if (err) {
		warning("voe_start_silencing: SetInputMute failed\n");
		return ENOSYS;
	}

	err = voe->volume->SetOutputMute(voe->isSilenced);
	if (err) {
		warning("voe_start_silencing: SetOutputMute failed\n");
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


int voe_enable_rcv_ns(bool enable)
{
	webrtc::NsModes NSmode;
	bool enabled;
	int err;
    
	if (gvoe.processing && !gvoe.active_channel_settings.empty()) {
		for( auto it = gvoe.active_channel_settings.begin();
		     it < gvoe.active_channel_settings.end(); it++) {
            
			err = gvoe.processing->GetRxNsStatus
				(it->channel_number_, enabled, NSmode);
			if (err) {
				warning("voe_enable_rcv_ns:"
					" voeProc->GetEcStatus failed\n");
				return ENOSYS;
			}
			if ( enable == enabled){
				warning("voe_enable_rcv_ns: rcv_ns"
					" enabled already %d \n", enable);
			}
			else {
				err = gvoe.processing->SetRxNsStatus
					(it->channel_number_, enable, NSmode);
				if (err) {
					warning("voe_enable_rcv_ns:"
						" voeProc->SetRxNsStatus failed\n");
					return ENOSYS;
				}
			}
		}
	}
	else {
		warning("voe_enable_rcv_ns: no active call"
			" cannot change NS status \n");
	}

	return 0;
}


int voe_debug(struct re_printf *pf, void *unused)
{
	struct le *le;
	int err = 0;

	err |= re_hprintf(pf, " voe.nch:         %d\n", gvoe.nch);
	err |= re_hprintf(pf, " voe.active_chs:  %d\n", gvoe.active_channel_settings.size());

    for( auto it = gvoe.active_channel_settings.begin();
        it < gvoe.active_channel_settings.end(); it++){
            
		int ch = it->channel_number_;

		err |= re_hprintf(pf, " ...channel=%d\n", ch);
    }
	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, " encoders (%u):\n", list_count(&gvoe.encl));
	for (le = gvoe.encl.head; le; le = le->next) {
		struct auenc_state *aes = (struct auenc_state *)le->data;

		err |= re_hprintf(pf, " ...%s channel=%d\n",
				  aes->ac->name, aes->ve->ch);
	}
	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, " decoders (%u):\n", list_count(&gvoe.decl));
	for (le = gvoe.decl.head; le; le = le->next) {
		struct audec_state *ads = (struct audec_state *)le->data;

		err |= re_hprintf(pf, " ...%s channel=%d\n",
				  ads->ac->name, ads->ve->ch);
	}
	err |= re_hprintf(pf, "\n");

	return err;
}


void voe_set_channel_load(struct voe *voe)
{
	webrtc::CodecInst c;
    
	int bitrate_bps = voe->manual_bitrate_bps ? voe->manual_bitrate_bps : voe->bitrate_bps;
	int packet_size_ms = voe->manual_packet_size_ms ? voe->manual_packet_size_ms :
		std::max( voe->packet_size_ms, voe->min_packet_size_ms );
    
	for( auto it = voe->active_channel_settings.begin();
        it < voe->active_channel_settings.end(); it++) {
		
        gvoe.codec->GetSendCodec(it->channel_number_, c);
		c.pacsize = (c.plfreq * packet_size_ms) / 1000;
		c.rate = bitrate_bps;
		gvoe.codec->SetSendCodec(it->channel_number_, c);

		info("voe: Changing codec settings parameters for channel %d\n", it->channel_number_);
		info("voe: pltype = %d \n", c.pltype);
		info("voe: plname = %s \n", c.plname);
		info("voe: plfreq = %d \n", c.plfreq);
		info("voe: pacsize = %d (%d ms)\n", c.pacsize, c.pacsize * 1000 / c.plfreq);
		info("voe: channels = %d \n", c.channels);
		info("voe: rate = %d \n", c.rate);
	}
}


void voe_multi_party_packet_rate_control(struct voe *voe)
{
#define ACTIVE_FLOWS_FOR_40MS_PACKETS 2
#define ACTIVE_FLOWS_FOR_60MS_PACKETS 4
    
	webrtc::CodecInst c;
    
	/* Change Packet size based on amount of flows in use */
	int active_flows = voe->active_channel_settings.size();
	int min_packet_size_ms = 20;

	if ( active_flows >= ACTIVE_FLOWS_FOR_60MS_PACKETS ) {
		min_packet_size_ms = 60;
	}
	else if( active_flows >=  ACTIVE_FLOWS_FOR_40MS_PACKETS) {
		min_packet_size_ms = 40;
	}

	if ( std::max( voe->packet_size_ms,     min_packet_size_ms ) !=
	     std::max( voe->packet_size_ms, voe->min_packet_size_ms ) ) {
		voe->min_packet_size_ms = min_packet_size_ms;
		voe_set_channel_load(voe);
	}

	voe->min_packet_size_ms = min_packet_size_ms;
}

void voe_set_audio_state_handler(flowmgr_audio_state_change_h *state_chgh,
				 void *arg)
{
	gvoe.state.chgh = state_chgh;
	gvoe.state.arg = arg;
}
