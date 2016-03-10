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

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/modules/video_capture/include/video_capture_factory.h"
#include "webrtc/modules/video_render/include/video_render.h"
#include "webrtc/video_encoder.h"
#include "vie_render_view.h"
#include "vie_renderer.h"
#include "vie.h"

// TODO: move these to vie and add get_instance func for global access
static webrtc::VideoCaptureModule *_capturer = NULL;
static ViECaptureRouter *_capture_router = NULL;
static ViERenderer *_send_renderer = NULL;
static enum flowmgr_video_send_state _send_state = FLOWMGR_VIDEO_SEND_NONE;
static bool _backgrounded = false;
static char *_dev_id = NULL;

static int capture_start_device(struct videnc_state *ves, struct vie *vie,
				const char *dev_id);

std::vector<webrtc::VideoStream> CreateVideoStreams(size_t num_streams) {
	assert(num_streams > 0);

	int idx = 0;
	// Add more streams to the settings above with reasonable values if required.
	static const size_t kNumSettings = 3;
	assert(num_streams <= kNumSettings);

	std::vector<webrtc::VideoStream> stream_settings(kNumSettings);
/*
	stream_settings[idx].width = 320;
	stream_settings[idx].height = 180;
	stream_settings[idx].max_framerate = 30;
	stream_settings[idx].min_bitrate_bps = 50000;
	stream_settings[idx].target_bitrate_bps = 
		stream_settings[idx].max_bitrate_bps = 150000;
	stream_settings[idx].max_qp = 56;
	idx++;

	stream_settings[idx].width = 640;
	stream_settings[idx].height = 360;
	stream_settings[idx].max_framerate = 30;
	stream_settings[idx].min_bitrate_bps = 200000;
	stream_settings[idx].target_bitrate_bps =
		stream_settings[idx].max_bitrate_bps = 450000;
	stream_settings[idx].max_qp = 56;
	idx++;
*/
	stream_settings[idx].width = 360;
	stream_settings[idx].height = 640;
	stream_settings[idx].max_framerate = 15;
	stream_settings[idx].min_bitrate_bps = 100000;
	stream_settings[idx].target_bitrate_bps =
		stream_settings[idx].max_bitrate_bps = 600000;
	stream_settings[idx].max_qp = 56;
	idx++;
	stream_settings.resize(num_streams);
	return stream_settings;
}

webrtc::VideoEncoderConfig CreateEncoderConfig() {
	webrtc::VideoEncoderConfig encoder_config;
	encoder_config.streams = CreateVideoStreams(1);
	webrtc::VideoStream* stream = &encoder_config.streams[0];
	return encoder_config;
}

class ViECaptureRouter : public webrtc::VideoCaptureDataCallback
{
public:
	ViECaptureRouter() : _input(NULL), _renderer(NULL){}
	void SetRenderer(ViERenderer *renderer) {_renderer = renderer;}
	void SetInput(webrtc::VideoCaptureInput *input) {_input = input;}

	void OnIncomingCapturedFrame(const int32_t id, const webrtc::VideoFrame& videoFrame)
	{
		if (_input) {
			_input->IncomingCapturedFrame(videoFrame);
		}
		if (_renderer) {
			_renderer->RenderFrame(videoFrame, 0);
		}
	}

	void OnCaptureDelayChanged(const int32_t id, const int32_t delay)
	{
	}

private:
	webrtc::VideoCaptureInput *_input;
	ViERenderer *_renderer;
};

static void ves_destructor(void *arg)
{
	struct videnc_state *ves = (struct videnc_state *)arg;

	vie_capture_stop(ves);

	mem_deref(ves->sdpm);
	mem_deref(ves->vie);
	mem_deref(_dev_id);
	_dev_id = NULL;

	_send_state = FLOWMGR_VIDEO_SEND_NONE;
}


int vie_enc_alloc(struct videnc_state **vesp,
		  struct media_ctx **mctxp,
		  const struct vidcodec *vc,
		  const char *fmtp, int pt,
		  struct sdp_media *sdpm,
		  videnc_rtp_h *rtph,
		  videnc_rtcp_h *rtcph,
		  videnc_create_preview_h *cpvh,
		  videnc_release_preview_h *rpvh,
		  viddec_err_h *errh,
		  void *arg)
{
	struct videnc_state *ves;
	int err = 0;

	if (!vesp || !vc || !mctxp)
		return EINVAL;

	info("%s: allocating codec:%s(%d)\n", __FUNCTION__, vc->name, pt);

	ves = (struct videnc_state *)mem_zalloc(sizeof(*ves), ves_destructor);
	if (!ves)
		return ENOMEM;

	if (*mctxp) {
		ves->vie = (struct vie *)mem_ref(*mctxp);
	}
	else {
		err = vie_alloc(&ves->vie, vc, pt);
		if (err) {
			goto out;
		}

		*mctxp = (struct media_ctx *)ves->vie;
	}

	ves->vc = vc;
	ves->pt = pt;
	ves->sdpm = (struct sdp_media *)mem_ref(sdpm);
	ves->vie->ves = ves;
	ves->rtph = rtph;
	ves->rtcph = rtcph;
	ves->cpvh = cpvh;
	ves->rpvh = rpvh;
	ves->errh = errh;
	ves->arg = arg;

 out:
	if (err) {
		mem_deref(ves);
	}
	else {
		*vesp = ves;
	}

	return err;
}

static int vie_capture_start_int(struct videnc_state *ves)
{
	struct vie *vie = ves ? ves->vie: NULL;
	struct list *devlist;
	struct videnc_capture_device *dev;
	int err = 0;

	if (!ves)
		return EINVAL;

	// if in background already return OK and start on return
	if (_backgrounded)
		return 0;

	err = vie_get_video_capture_devices(&devlist);
	if (err != 0) {
		goto out;
	}

	if (list_count(devlist) < 1) {
		err = ENODEV;
		goto out;
	}

	dev = (struct videnc_capture_device*)list_head(devlist)->data;

	err = capture_start_device(ves, vie, dev->dev_id);
	if (err != 0) {
		goto out;
	}

	vie->send_stream->Start();

out:
	if (err != 0)
		error("%s: err=%d\n", __FUNCTION__, err);

	if (devlist) {
		mem_deref(devlist);
	}
	return err;
}

int vie_capture_start(struct videnc_state *ves)
{
	int err = 0;
	if (_send_state == FLOWMGR_VIDEO_SEND) {
		return 0;
	}

	debug("%s: ss %d\n", __FUNCTION__, _send_state);
	err = vie_capture_start_int(ves);
	if (err == 0) {
		_send_state = FLOWMGR_VIDEO_SEND;
	}

	return err;
}

static void vie_capture_stop_int(struct videnc_state *ves)
{
	struct vie *vie = ves ? ves->vie: NULL;

	if (!ves || _send_state == FLOWMGR_VIDEO_SEND_NONE) {
		return;
	}

	if (vie->send_stream) {
		vie->send_stream->Stop();
	}

	if (_capture_router) {
		_capture_router->SetInput(NULL);
	}

	if (_send_state != FLOWMGR_VIDEO_PREVIEW) {
		if (_capture_router) {
			_capture_router->SetRenderer(NULL);
		}
		if (_capturer) {
			_capturer->StopCapture();
			_capturer->DeRegisterCaptureDataCallback();
		}

		if (_send_renderer) {
			void *view = _send_renderer->View();
			_send_renderer->DetachFromView();
			if (ves->rpvh && view) {
				ves->rpvh(view, ves->arg);
			}
		}

		if (_capturer) {
			_capturer->Release();
			_capturer = NULL;
		}
		delete _capture_router;
		_capture_router = NULL;

		delete _send_renderer;
		_send_renderer = NULL;
	}

	if (vie->send_stream) {
		vie->call->DestroyVideoSendStream(vie->send_stream);
		vie->send_stream = NULL;
	}
	vie->encoder = NULL;
}


void vie_capture_hold(struct videnc_state *ves, bool hold)
{
	struct vie *vie;

	if (!ves)
		return;	

	vie = ves->vie;
	
	if (hold) {	
		if (vie->send_stream) {
			vie->send_stream->Stop();
		}

		if (_capturer) {
			_capturer->StopCapture();
		}
	}
	else { /* Resume */
		webrtc::VideoCaptureCapability capture_caps;

		capture_caps.width = ves->capture_width;
		capture_caps.height = ves->capture_height;
		capture_caps.maxFPS = ves->capture_fps;
		capture_caps.rawType = webrtc::kVideoI420;

		if (_capturer) {
			if (_capturer->StartCapture(capture_caps) != 0) {
				warning("%s: failed to start capture\n",
					__func__);
			}
		}

		if (vie->send_stream) {
			vie->send_stream->Start();
		}
	}
}


void vie_capture_stop(struct videnc_state *ves)
{
	debug("%s: ss %d\n", __FUNCTION__, _send_state);
	vie_capture_stop_int(ves);
	if (_send_state != FLOWMGR_VIDEO_PREVIEW) {
		_send_state = FLOWMGR_VIDEO_SEND_NONE;
	}
}


static bool ssrc_handler(const char *name, const char *value, void *arg)
{
	struct videnc_state *ves = (struct videnc_state*)arg;
	char *ignore;

	if (ves->ssrc_count < MAX_SSRCS) {
		uint32_t val;
		if (sscanf(value, "%u", &val) > 0) {
			vie_update_ssrc_array(ves->ssrc_array, &ves->ssrc_count, val);
		}
	}
	return false;
}

static int vie_activate_video_preview_int(flowmgr_create_preview_h hndlr, void *arg)
{
	webrtc::VideoCaptureCapability capture_caps;
	struct list *devlist;
	char *dev_id = NULL;
	struct videnc_capture_device *dev;
	int err = 0;

	debug("%s\n", __FUNCTION__);
	webrtc::VideoEncoderConfig encoder_config(CreateEncoderConfig());
	err = vie_get_video_capture_devices(&devlist);
	if (err != 0) {
		goto out;
	}

	if (list_count(devlist) < 1) {
		err = ENODEV;
		goto out;
	}

	dev = (struct videnc_capture_device*)list_head(devlist)->data;

	if (_dev_id) {
		dev_id = _dev_id;
	}
	else {
		dev_id = dev->dev_id;
	}

	_send_renderer = new ViERenderer();
	if (!_send_renderer) {
		warning("%s: Creating renderer failed\n", __FUNCTION__);
		err = ENODEV;
		goto out;
	}
#ifdef ANDROID
	_send_renderer->setMaxdim(640);
#endif
    
	_send_renderer->setUseTimeoutImage(false);
    
	_send_renderer->setMirrored(vie_should_mirror_preview(dev_id));

	debug("%s: dev_id=%s\n", __FUNCTION__, dev_id);
	_capturer = webrtc::VideoCaptureFactory::Create(0, dev_id);
	if (!_capturer) {
		warning("%s: Creating capture device failed\n", __FUNCTION__);
		err = ENODEV;
		goto out;
	}

	_capture_router = new ViECaptureRouter();
	_capture_router->SetRenderer(_send_renderer);

	_capturer->RegisterCaptureDataCallback(*_capture_router);
	capture_caps.width = encoder_config.streams[0].width;
	capture_caps.height = encoder_config.streams[0].height;
	capture_caps.maxFPS = encoder_config.streams[0].max_framerate;
	capture_caps.rawType = webrtc::kVideoI420;

	debug("%s: start capture %dx%d@%d\n", __FUNCTION__, capture_caps.width,
		capture_caps.height, capture_caps.maxFPS);
	if (_capturer->StartCapture(capture_caps) != 0) {
		err = ENODEV;
		goto out;
	}

	assert(_capturer->CaptureStarted());

	if (hndlr) {
		debug("%s create preview\n", __FUNCTION__);
		hndlr(arg);
	}

out:
	if (err != 0)
		error("%s: err=%d\n", __FUNCTION__, err);

	if (devlist) {
		mem_deref(devlist);
	}
	return err;
}

int vie_activate_video_preview(flowmgr_create_preview_h hndlr, void *arg)
{
	int err = 0;

	debug("%s: ss %d\n", __FUNCTION__, _send_state);
	switch(_send_state) {
		case FLOWMGR_VIDEO_PREVIEW:
			break;

		case FLOWMGR_VIDEO_SEND_NONE:
			err = vie_activate_video_preview_int(hndlr, arg);
			if (err == 0) {
				_send_state = FLOWMGR_VIDEO_PREVIEW;
			}
			break;

		case FLOWMGR_VIDEO_SEND:
			_send_state = FLOWMGR_VIDEO_PREVIEW;
			break;

	}

	return err;
}
		
static void vie_deactivate_video_preview_int(flowmgr_release_preview_h hndlr, void *arg)
{
	if (_capturer) {
		_capturer->StopCapture();
		_capturer->DeRegisterCaptureDataCallback();
	}

	if (_capture_router) {
		_capture_router->SetRenderer(NULL);
		delete _capture_router;
		_capture_router = NULL;
	}

	if (_send_renderer) {
		_send_renderer->Stop();
		void *view = _send_renderer->View();
		_send_renderer->DetachFromView();
		if (hndlr && view) {
			debug("%s release preview\n", __FUNCTION__);
			hndlr(view, arg);
		}
	}

	if (_capturer) {
		_capturer->Release();
		_capturer = NULL;
	}

	delete _send_renderer;
	_send_renderer = NULL;
}

void vie_deactivate_video_preview(flowmgr_release_preview_h hndlr, void *arg)
{
	if (_send_state != FLOWMGR_VIDEO_PREVIEW) {
		return;
	}

	debug("%s: ss %d\n", __FUNCTION__, _send_state);
	vie_deactivate_video_preview_int(hndlr, arg);
	_send_state = FLOWMGR_VIDEO_SEND_NONE;
}

static int capture_start_device(struct videnc_state *ves, struct vie *vie,
				const char *dev_id)
{
	webrtc::VideoSendStream::Config send_config;
	int err = 0;

	webrtc::VideoEncoderConfig encoder_config(CreateEncoderConfig());

	ves->capture_width = encoder_config.streams[0].width;
	ves->capture_height = encoder_config.streams[0].height;
	ves->capture_fps = encoder_config.streams[0].max_framerate;

	ves->ssrc_count = 0;
	sdp_media_lattr_apply(ves->sdpm, "ssrc", ssrc_handler, ves);

	if (ves->ssrc_count < 1) {
		error("%s: No local SSRCS to use for video\n", __FUNCTION__);
		return EINVAL;
	}

	send_config.rtp.nack.rtp_history_ms = 0;
	send_config.rtp.ssrcs.push_back(ves->ssrc_array[0]);
#if USE_RTX
	if (ves->ssrc_count > 1) {
		sdp_format *rtx;
		
		rtx = sdp_media_format(ves->sdpm, true, NULL, -1, "rtx",
				       -1, -1);

		if (!rtx) {
			warning("vie: %s: rtx_fmt not found\n", __func__);
		}
		else {
			debug("vie: %s: rtx ssrc=%u pt=%d\n",
			      __func__, ves->ssrc_array[1], rtx->pt);
			send_config.rtp.nack.rtp_history_ms = 5000;
			send_config.rtp.rtx.ssrcs.push_back(ves->ssrc_array[1]);
			send_config.rtp.rtx.payload_type = rtx->pt;
		}
	}
#endif
	
	send_config.rtp.extensions.push_back(
		webrtc::RtpExtension(webrtc::RtpExtension::kAbsSendTime,
				     kAbsSendTimeExtensionId));

	vie->encoder = webrtc::VideoEncoder::Create(webrtc::VideoEncoder::kVp8);

	send_config.encoder_settings.encoder = vie->encoder;
	send_config.encoder_settings.payload_name = ves->vc->name;
	send_config.encoder_settings.payload_type = ves->pt;
	send_config.suspend_below_min_bitrate = true;
    
	debug("%s: srend %p cap %p capr %p\n", __FUNCTION__, _send_renderer,
		_capturer, _capture_router);
	if(_send_renderer == NULL || _capturer == NULL ||
		_capture_router == NULL) {
		vie_activate_video_preview_int(ves->cpvh, ves->arg);
	}

	if (!_capturer) {
		warning("%s: Creating capture device failed\n", __FUNCTION__);
		err = ENODEV;
		goto out;
	}

	//send_config.local_renderer = vie->send_renderer;
	vie->send_stream = vie->call->CreateVideoSendStream(send_config,
							    encoder_config);
	if (vie->send_stream == NULL) {
		err = ENOENT;
		goto out;
	}

	debug("capture_start_device\n");
	_capture_router->SetInput(vie->send_stream->Input());

out:
	if (err != 0)
		error("%s: err=%d\n", __FUNCTION__, err);

	return err;
}

int vie_set_capture_device(struct videnc_state *ves, const char *dev_id)
{
	int err = 0;
	webrtc::VideoCaptureModule *capturer;
	struct vie *vie = ves ? ves->vie: NULL;
	webrtc::VideoCaptureCapability capture_caps;

	if(!vie) {
		err = EINVAL;
		goto out;
	}

	if (!_capturer || _send_state == FLOWMGR_VIDEO_SEND_NONE) {
		warning("vie_set_capture_device: no capturer yet");
		err = 0;
		goto out;
	}

	mem_deref(_dev_id);
	str_dup(&_dev_id, dev_id);

	debug("vie_set_capture: dev_id=%s\n", dev_id);
	capturer = webrtc::VideoCaptureFactory::Create(0, dev_id);
	if (!capturer) {
		warning("%s: Creating capture device failed\n", __FUNCTION__);
		err = ENODEV;
		goto out;
	}

	_capturer->StopCapture();
	_capturer->DeRegisterCaptureDataCallback();
	_capturer->Release();

        _capturer = capturer;
	_capturer->RegisterCaptureDataCallback(*_capture_router);
	capture_caps.width = ves->capture_width;
	capture_caps.height = ves->capture_height;
	capture_caps.maxFPS = ves->capture_fps;
	capture_caps.rawType = webrtc::kVideoI420;

	if (_send_renderer) {
		_send_renderer->setMirrored(vie_should_mirror_preview(dev_id));
	}

	debug("%s: start capture %dx%d@%d\n", __FUNCTION__, capture_caps.width,
		capture_caps.height, capture_caps.maxFPS);
	if (_capturer->StartCapture(capture_caps) != 0) {
		err =  ENODEV;
		goto out;
	}

 out:
	if (err != 0)
		error("%s: err=%d\n", __FUNCTION__, err);

	return err;
}


int vie_set_preview(struct videnc_state* ves, void *view)
{
	struct vie *vie = ves ? ves->vie: NULL;

	debug("%s: ves %p vie %p sendr %p parent %p\n", __FUNCTION__,
		ves, vie, _send_renderer, view);
	if (_send_renderer) {
		_send_renderer->AttachToView(view);
		if (!_backgrounded) {
			_send_renderer->Start();
		}
	}

	return 0;
}

void vie_capture_background(struct videnc_state *ves, enum media_bg_state state)
{
	struct vie *vie = ves ? ves->vie : NULL;

	debug("%s: %s ves %p vie %p sendr %p sstate %d\n", __FUNCTION__,
		state == MEDIA_BG_STATE_ENTER ? "enter" : "exit",
		ves, vie, _send_renderer, _send_state);

	if (ves && vie && _send_state == FLOWMGR_VIDEO_SEND) {
		switch (state) {
			case MEDIA_BG_STATE_ENTER:
				debug("stop send renderer\n");
				_backgrounded = true;
				vie_capture_stop_int(ves);
				break;

			case MEDIA_BG_STATE_EXIT:
				debug("start send renderer\n");
				_backgrounded = false;
				vie_capture_start_int(ves);
				break;
		}
	}
}

void vie_preview_background(enum media_bg_state state, flowmgr_create_preview_h cpvh,
	flowmgr_release_preview_h rpvh, void* arg)
{
	debug("%s: %s psendr %p sstate %d\n", __FUNCTION__,
		state == MEDIA_BG_STATE_ENTER ? "enter" : "exit",
		_send_renderer, _send_state);

	if (_send_state == FLOWMGR_VIDEO_PREVIEW) {
		switch (state) {
			case MEDIA_BG_STATE_ENTER:
				debug("stop preview\n");
				vie_deactivate_video_preview_int(rpvh, arg);
				break;

			case MEDIA_BG_STATE_EXIT:
				debug("start preview\n");
				vie_activate_video_preview_int(cpvh, arg);
				break;
		}
	}

	_backgrounded = (state == MEDIA_BG_STATE_ENTER) ? true : false;
}

