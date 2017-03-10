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

	if (voeProc){
		voeProc->StartDebugRecording(fileNameUTF8);
	}
	return 0;
}

void voe_stop_preproc_recording()
{
	auto voeProc = gvoe.processing;
    
	if (voeProc){
		voeProc->StopDebugRecording();
	}
}

void voe_start_rtp_dump(struct voe_channel *ve)
{
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
