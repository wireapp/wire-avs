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

#include "avs_voe.h"
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

    if (!gvoe.volume){
        return ENOSYS;
    }
    
    if(gvoe.nch == 0){
        /* return the cashed value */
        *muted = gvoe.isMuted;
        return 0;
    }
    
	err = gvoe.volume->GetInputMute(-1, enabled);
	if (err) {
		warning("voe_get_mute: GetInputMute failed\n");
		return ENOSYS;
	}

	debug("voe_get_mute: muted=%d\n", enabled);

	*muted = enabled;
	return 0;
}


int voe_start_playing_PCM_file_as_microphone(const char fileNameUTF8[1024],
					     int fs)
{
	auto voeFile = gvoe.file;

	webrtc::FileFormats format;

	if ( fs == 16000 ) {
		format = webrtc::kFileFormatPcm16kHzFile;
	}
	else if ( fs == 32000 ) {
		format = webrtc::kFileFormatPcm32kHzFile;
	}
	else {
		return -1;
	}

	int ret = voeFile->StartPlayingFileAsMicrophone(-1, fileNameUTF8,
							true, false,
							format, 1.0);
	if ( ret == 0 ) {
		gvoe.is_playing = true;
	}

	return ret;
}


void voe_stop_playing_PCM_file_as_microphone(void)
{
	auto voeFile = gvoe.file;

	if (gvoe.is_playing) {
		voeFile->StopPlayingFileAsMicrophone(-1);
		gvoe.is_playing = false;
	}
}


int voe_start_recording_playout_PCM_file(const char fileNameUTF8[1024])
{
	auto voeFile = gvoe.file;

	int ret = voeFile->StartRecordingPlayout(-1, fileNameUTF8, NULL, -1);
	if ( ret == 0 ){
		gvoe.is_recording = true;
	}

	return ret;
}


void voe_stop_recording_playout_PCM_file()
{
	auto voeFile = gvoe.file;

	if (gvoe.is_recording) {
		voeFile->StopRecordingPlayout(-1);
		gvoe.is_recording = false;
	}
}


int voe_start_packet_recording(const char fileNameUTF8[1024])
{
	auto voeRtpRtcp = gvoe.rtp_rtcp;

	if (gvoe.active_channel_settings.empty()) {
		warning("Cannot start packet recording no active channels \n");
		return EINVAL;
	}

	if (voeRtpRtcp) {
		for ( auto it = gvoe.active_channel_settings.begin();
		      it < gvoe.active_channel_settings.end(); it++) {

			// ToDo append channel number to file name
			voeRtpRtcp->StartRTPDump(it->channel_number_,
						 fileNameUTF8,
						 webrtc::kRtpIncoming);
		}

		gvoe.is_rtp_recording = true;
	}

	return 0;
}


void voe_stop_packet_recording()
{
	auto voeRtpRtcp = gvoe.rtp_rtcp;

	if (!gvoe.is_rtp_recording) {
		return;
	}

	if (gvoe.active_channel_settings.empty()) {
		info("voe: Cannot stop packet recording no active channels\n");
		return;
	}

	if (voeRtpRtcp){
		for ( auto it = gvoe.active_channel_settings.begin();
		      it < gvoe.active_channel_settings.end(); it++) {
            
			voeRtpRtcp->StopRTPDump(it->channel_number_,
						webrtc::kRtpIncoming);
		}
		gvoe.is_rtp_recording = false;
	}
}


int voe_enable_fec(bool enable)
{
#if USE_MEDIAENGINE
	int err;

	if (gvoe.codec && !gvoe.active_channel_settings.empty()) {
		for ( auto it = gvoe.active_channel_settings.begin();
		      it < gvoe.active_channel_settings.end(); it++) {

			err = gvoe.codec->SetFECStatus(it->channel_number_,
						      enable );
			if (err) {
				warning("voe_enable_fec:"
					" SetCodecFEC failed\n");
				return ENOSYS;
			}
		}

		debug("voe_enable_fec: enable=%d\n", enable);
	}
#else
	warning("voe_enable_fec: Not supported by this WebRTC version \n");
#endif
	return 0;
}


int voe_enable_aec(bool enable)
{
	webrtc::EcModes ECmode;
	bool enabled;
	int err;

	if (gvoe.processing && !gvoe.active_channel_settings.empty()) {
		err = gvoe.processing->GetEcStatus(enabled, ECmode);
		if (err) {
			warning("voe_enable_aec: voeProc->GetEcStatus"
				" failed\n");
			return ENOSYS;
		}

		if ( enable == enabled) {
			warning("voe_enable_aec: aec enabled allready %d \n",
				enable);
		}
		else {
			err = gvoe.processing->SetEcStatus(enable, ECmode);
			if (err) {
				warning("voe_enable_aec: voeProc->SetEcStatus"
					" failed\n");
				return ENOSYS;
			}
		}
	}
	else {
		warning("voe_enable_aec: no active call cannot"
			" change AEC status \n");
	}

	return 0;
}


int voe_set_bitrate(int bitrate_bps)
{
	gvoe.manual_bitrate_bps = bitrate_bps;
	voe_set_channel_load(&gvoe);
	return 0;
}


int voe_set_packet_size(int packet_size_ms)
{
	gvoe.manual_packet_size_ms = packet_size_ms;
	voe_set_channel_load(&gvoe);
	return 0;
}


int voe_start_preproc_recording(const char fileNameUTF8[1024])
{
	auto voeProc = gvoe.processing;
    
	if (voeProc)
		voeProc->StartDebugRecording(fileNameUTF8);

	return 0;
}


void voe_stop_preproc_recording()
{
	auto voeProc = gvoe.processing;

	if (gvoe.active_channel_settings.empty()) {
		return;
	}


	if (voeProc)
		voeProc->StopDebugRecording();
}
