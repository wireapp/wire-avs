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
#include <pthread.h>
#include <stdio.h>
#include <re.h>

#include <avs.h>
#include <avs_vie.h>
#include <avs_voe.h>

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/video_encoder.h"
#include "vie.h"

ViETransport::ViETransport(struct vie *vie_) : vie(vie_), active(true)
{
}

ViETransport::~ViETransport()
{
	active = false;
}

bool ViETransport::SendRtp(const uint8_t* packet, size_t length,
	const webrtc::PacketOptions& options)
{
	struct videnc_state *ves = vie->ves;
	int err = 0;

#if defined(VIE_PRINT_ENCODE_RTP) || defined(VIE_DEBUG_RTX)
	struct rtp_header rtph;
	struct mbuf mb;

	// TODO: use options maybe?
	
	//debug("vie: rtp[%d bytes]\n", (int)length);

	mb.buf = (uint8_t *)packet;
	mb.pos = 0;
	mb.size = length;
	mb.end = mb.size;

	err = rtp_hdr_decode(&rtph, &mb);
 #if defined(VIE_PRINT_ENCODE_RTP)
	if (!err) {
		info("vie: decode: pt=%d ssrc=%u\n", rtph.pt, rtph.ssrc);
	}
 #endif
 #if defined(VIE_DEBUG_RTX)
	if (!err && (rtph.pt == vie->rtx_pt)){
		uint16_t osn = ntohs(mbuf_read_u16(&mb));
		info("vie: Sending %d Bytee RTX packet seq = %u ssrc = %u osn = %d \n", length, rtph.seq, rtph.ssrc, osn);
	}
 #endif
#endif

	if (!active || !ves) {
		warning("cannot send RTP\n");
		return false;
	}
    
	stats_rtp_add_packet(&vie->stats_tx, packet, length);

	if (ves->rtph) {
		err = ves->rtph(packet, length, ves->arg);
		if (err) {
			warning("vie: rtp send failed (%m)\n", err);
			return -1;
		}
    
#if FORCE_VIDEO_RTP_RECORDING
		// Only Save RTP header and length;
		uint32_t len32 = (uint32_t)length;
		uint8_t buf[VIDEO_RTP_RECORDING_LENGTH + sizeof(uint32_t)]; // RTP header is 12 bytes
        
		memcpy(buf, &len32, sizeof(uint32_t));
		memcpy(&buf[sizeof(int32_t)], packet, VIDEO_RTP_RECORDING_LENGTH*sizeof(uint8_t));
        
		vie->rtp_dump_out->DumpPacket(buf, sizeof(buf));
#endif
		return true;
	}

	return false;
}

bool ViETransport::SendRtcp(const uint8_t* packet, size_t length)
{
	struct videnc_state *ves = vie->ves;
	int err = 0;

	//debug("vie: rtcp[%d bytes]\n", (int)length);

	if (!active || !ves)
		return -1;

	stats_rtcp_add_packet(&vie->stats_tx, packet, length);

#if FORCE_VIDEO_RTP_RECORDING
	vie->rtcp_dump_out->DumpPacket(packet, length);
#endif
    
	if (ves->rtcph) {
		err = ves->rtcph(packet, length, ves->arg);
		if (err) {
			warning("vie: rtcp send failed (%m)\n", err);
			return -1;
		}			
		return length;
	}

	return -1;
}

class ViELoadObserver : public webrtc::LoadObserver
{
	public:
    
	ViELoadObserver(){};
    
	~ViELoadObserver(){}
    
	void OnLoadUpdate(Load load){
		switch(load){
			case webrtc::LoadObserver::kOveruse:
				error("CPU is overused !!\n");
				break;
            
			case webrtc::LoadObserver::kUnderuse:
				debug("CPU is underused !!\n");
				break;
		}
	}
    
	private:
};

static void print_summary(struct vie *vie, int ch)
{
#if 0
	unsigned int key_frames=0;
	unsigned int delta_frames=0;
	int r;

	re_printf("~ ~ ~ ~ VIE Summary (channel %d) ~ ~ ~ ~\n", ch);

	re_printf("Send:\n");
	if (vid_eng.codec) {

		r = vid_eng.codec->GetSendCodecStastistics(ch,
							   key_frames,
							   delta_frames);
		if (r < 0) {
			re_printf("could not get send statistics\n");
		}
		else {
			re_printf("key_frames=%u, delta_frames=%u\n",
				  key_frames, delta_frames);
		}

		//re_printf("%H\n", transp_stats_print, &vie->stats_tx);
	}

#if 0
	re_printf("\nRecv:\n");
	if (vid_eng.codec) {

		r = vid_eng.codec->GetReceiveCodecStastistics(ch,
							      key_frames,
							      delta_frames);
		if (r < 0) {
			re_printf("could not get receive statistics\n");
		}
		else {
			re_printf("key_frames=%u, delta_frames=%u\n",
				  key_frames, delta_frames);
		}

		//re_printf("%H\n", transp_stats_print, &vie->stats_rx);
	}
#endif

	re_printf("Tx: %H\n", stats_print, &vie->stats_tx);
	re_printf("Rx: %H\n", stats_print, &vie->stats_rx);
	re_printf("~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ \n");
#endif
}


static void vie_destructor(void *arg)
{
	struct vie *vie = (struct vie *)arg;

	list_unlink(&vie->le);

	print_summary(vie, vie->ch);

	if (vie->ves) {
		vie_capture_stop(vie->ves);
	}

	if (vie->vds) {
		vie_render_stop(vie->vds);
	}

	if (vie->transport)
		delete vie->transport;
    
	if(vie->load_observer)
		delete  vie->load_observer;
    
	if(vie->call)
		delete vie->call;
    
#if FORCE_VIDEO_RTP_RECORDING
	if(vie->rtp_dump_in){
		vie->rtp_dump_in->Stop();
	}
	if(vie->rtp_dump_out){
		vie->rtp_dump_out->Stop();
	}
	if(vie->rtcp_dump_in){
		vie->rtcp_dump_in->Stop();
	}
	if(vie->rtcp_dump_out){
		vie->rtcp_dump_out->Stop();
	}
#endif
	delete vie->rtp_dump_in;
	delete vie->rtp_dump_out;
	delete vie->rtcp_dump_in;
	delete vie->rtcp_dump_out;
}

static void vie_start_rtp_dump(struct vie *vie)
{
	char  buf[80];
	time_t     now = time(0);
	struct tm  tstruct;
    
#if TARGET_OS_IPHONE
	std::string prefix = voe_iosfilepath();
	prefix.insert(prefix.size(),"/Ios_");
#elif defined(ANDROID)
	std::string prefix = "/data/local/tmp/Android_";
#else
	std::string prefix = "Osx_";
#endif
    
	tstruct = *localtime(&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

	std::string name_in = prefix + "VidIn";
	std::string name_out = prefix + "VidOut";
	name_in.insert(name_in.size(),buf);
	name_in.insert(name_in.size(),".rtp");
	name_out.insert(name_out.size(),buf);
	name_out.insert(name_out.size(),".rtp");
    
	vie->rtp_dump_in->Start(name_in.c_str());
	vie->rtp_dump_out->Start(name_out.c_str());
    
	name_in.insert(name_in.size()-1,"c");
	vie->rtcp_dump_in->Start(name_in.c_str());

	name_out.insert(name_out.size()-1,"c");
	vie->rtcp_dump_out->Start(name_out.c_str());
}

int vie_alloc(struct vie **viep, const struct vidcodec *vc, int pt)
{
	struct vie *vie;
	int err = 0;

	if (!viep || !vc)
		return EINVAL;

	debug("vie_alloc: codec=%s\n", vc->name);

	vie = (struct vie *)mem_zalloc(sizeof(*vie), vie_destructor);
	if (vie == NULL)
		return ENOMEM;

	vie->ch = -1;

	vie->transport = new ViETransport(vie);
	vie->load_observer = new ViELoadObserver();
	webrtc::Call::Config config;

	// TODO: find how to set overuse callback
	// config.overuse_callback = vie->load_observer;
/* TODO: set bitrate limits */
/*
all_config.bitrate_config.min_bitrate_bps =
   static_cast<int>(config_.min_bitrate_kbps) * 1000;
all_config.bitrate_config.start_bitrate_bps =
   static_cast<int>(config_.start_bitrate_kbps) * 1000;
all_config.bitrate_config.max_bitrate_bps =
   static_cast<int>(config_.max_bitrate_kbps) * 1000;
*/

	vie->call = webrtc::Call::Create(config);
	if (vie->call == NULL) {
		/* return here to avoid protected scope error */
		mem_deref((void *)vie);
		return ENOSYS;
	}

	vie->rtp_dump_in = new wire_avs::RtpDump();
	vie->rtp_dump_out = new wire_avs::RtpDump();
	vie->rtcp_dump_in = new wire_avs::RtpDump();
	vie->rtcp_dump_out = new wire_avs::RtpDump();
    
#if FORCE_VIDEO_RTP_RECORDING
	vie_start_rtp_dump(vie);
#endif

out:
	if (err)
		mem_deref((void *)vie);
	else
		*viep = vie;

	return err;
}

void vie_update_ssrc_array( uint32_t array[], size_t *count, uint32_t val)
{
	int i;
	for(i = 0; i < *count; i++){
		if(val == array[i]){
			break;
		}
	}
	if( i == *count){
		array[*count] = val;
		(*count)++;
	}
}

