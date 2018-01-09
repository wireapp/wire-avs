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


int voe_set_mute(bool mute)
{
	int err = 0;
    
	gvoe.isMuted = mute;
    
	return voe_update_mute(&gvoe);
}


int voe_get_mute(bool *muted)
{
	int err;
	bool enabled;

	/* return the cached value */
	*muted = gvoe.isMuted;
	return 0;
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
#ifdef ZETA_USE_BUILD_IN_AEC_SPEAKER
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

#if !defined(ANDROID)
    char strNameUTF8[128];
    if(!voe->hw->GetPlayoutDeviceName(0, strNameUTF8, NULL)){
        voe_set_auplay(strNameUTF8);
    }
#endif

#if FORCE_AUDIO_PREPROC_RECORDING
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
    
    file.insert(file.size(),buf);
    file.insert(file.size(),".aecdump");
    
    voe_start_preproc_recording(file.c_str());
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

int voe_set_auplay(const char *dev)
{
    bool enabled;
    webrtc::EcModes ECmode;
    webrtc::AgcModes AGCmode;
    
    auto voeProc = gvoe.processing;
    
    info("voe: Playout device switched from %s to %s \n",
         gvoe.playout_device, dev);

    gvoe.playout_device = (char *)mem_deref(gvoe.playout_device);
    str_dup(&gvoe.playout_device, dev);
    
    if (gvoe.nch == 0) {
        return 0;
    }
    
    /* Setup AEC based on the routing */
    voe_update_aec_settings(&gvoe);
    
    /* Setup AGC based on the routing and if in a conference or not */
    voe_update_agc_settings(&gvoe);
    
    /* Reset the AVS level normalizer */
    if(gvoe.voe_audio_effect){
        gvoe.voe_audio_effect->ResetNormalizer();
    }
    return 0;
}

void voe_update_aec_settings(struct voe *voe)
{
    int ret;
    auto voeProc = voe->processing;
    const webrtc::EcModes mode = ZETA_AEC_MODE;
    
    if (!voeProc)
        return;
    
    if (streq(voe->playout_device, "speaker")) {
        info("voe: Setup aec settings for speakerphone \n");
        bool build_in_aec = voe->hw->BuiltInAECIsAvailable();
#ifdef ZETA_USE_BUILD_IN_AEC_SPEAKER
        bool use_build_in_aec = build_in_aec;
#else
        bool use_build_in_aec = false;
#endif
        if(build_in_aec){
            voe->hw->EnableBuiltInAEC(use_build_in_aec);
        }
        if (use_build_in_aec){
            info("voe: using build in AEC \n");
            int ret = voeProc->SetEcStatus(false, ZETA_AEC_MODE);
        } else {
            int ret = voeProc->SetEcStatus(ZETA_USE_AEC_SPEAKER, ZETA_AEC_MODE);
            if ( ZETA_USE_AEC_SPEAKER && mode == webrtc::kEcAecm) {
                ret += voeProc->SetAecmMode(ZETA_AECM_MODE_SPEAKER, false);
                info("voe: Change AECM mode to %d \n", ZETA_AECM_MODE_SPEAKER);
            }
        }
    }
    else if (streq(voe->playout_device, "earpiece")
	     || streq(voe->playout_device, "bt")) {
        info("voe: Setup aec settings for earpiece \n");
        bool build_in_aec = voe->hw->BuiltInAECIsAvailable();
#ifdef ZETA_USE_BUILD_IN_AEC_EARPIECE
        bool use_build_in_aec = build_in_aec;
#else
        bool use_build_in_aec = false;
#endif
        if(build_in_aec){
            voe->hw->EnableBuiltInAEC(use_build_in_aec);
        }
        if (use_build_in_aec){
            info("voe: using build in AEC \n");
            int ret = voeProc->SetEcStatus(false, ZETA_AEC_MODE);
        } else {
            int ret = voeProc->SetEcStatus(ZETA_USE_AEC_EARPIECE, ZETA_AEC_MODE);
            if ( ZETA_USE_AEC_EARPIECE && mode == webrtc::kEcAecm) {
                ret += voeProc->SetAecmMode(ZETA_AECM_MODE_EARPIECE, false);
                info("voe: Change AECM mode to %d \n", ZETA_AECM_MODE_EARPIECE);
            }
        }
    }
    else if (streq(voe->playout_device, "headset")) {
        info("voe: Setup aec settings for headset \n");
        bool build_in_aec = voe->hw->BuiltInAECIsAvailable();
#ifdef ZETA_USE_BUILD_IN_AEC_HEADSET
        bool use_build_in_aec = build_in_aec;
#else
        bool use_build_in_aec = false;
#endif
        if(build_in_aec){
            voe->hw->EnableBuiltInAEC(use_build_in_aec);
        }
        if (use_build_in_aec){
            info("voe: using build in AEC \n");
            int ret = voeProc->SetEcStatus(false, ZETA_AEC_MODE);
        } else {
            /* Setup AEC (Reset by disabeling + enabeling) */
            int ret = voeProc->SetEcStatus(ZETA_USE_AEC_HEADSET, ZETA_AEC_MODE);
            if ( ZETA_USE_AEC_HEADSET && mode == webrtc::kEcAecm) {
                ret += voeProc->SetAecmMode(ZETA_AECM_MODE_HEADSET, false);
                info("voe: Change AECM mode to %d \n", ZETA_AECM_MODE_HEADSET);
            }
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
    
    if (streq(voe->playout_device, "speaker")) {
        use_agc = ZETA_USE_AGC_SPEAKER;
        if(list_count(&voe->channel_data_list) > 1){ /* In a conference we have more than one active channel */
            info("voe: Setup agc settings for speakerphone + conference call \n");
            wantedAGCmode = ZETA_AGC_MODE_SPEAKER_CONF;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER_CONF;
        }else{
            info("voe: Setup agc settings for speakerphone + one-one call \n");
            wantedAGCmode = ZETA_AGC_MODE_SPEAKER;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER;
        }
    }
    else if (streq(voe->playout_device, "earpiece")
	     || streq(voe->playout_device, "bt")) {
        use_agc = ZETA_USE_AGC_EARPIECE;
        if(list_count(&voe->channel_data_list) > 1){
            info("voe: Setup agc settings for earpiece + conference call \n");
            wantedAGCmode = ZETA_AGC_MODE_EARPIECE_CONF;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE_CONF;
        }else{
            info("voe: Setup agc settings for earpiece + one-one call \n");
            wantedAGCmode = ZETA_AGC_MODE_EARPIECE;
            wanted_digCompresGaindB = ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE;
        }
    }
    else if (streq(voe->playout_device, "headset")) {
        use_agc = ZETA_USE_AGC_HEADSET;
        if(list_count(&voe->channel_data_list) > 1){
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

