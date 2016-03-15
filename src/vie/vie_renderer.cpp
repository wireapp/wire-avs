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
#include <avs.h>
#include <avs_vie.h>
#include "vie_renderer.h"
#include "vie_render_view.h"
#include "webrtc/common_video/libyuv/include/scaler.h"
#include "vie_timeout_image_data.h"

static void generate_timeout_image(webrtc::VideoFrame& timeout_frame)
{
    timeout_frame.CreateEmptyFrame(TIMEOUT_FULL_IMAGE_W, TIMEOUT_FULL_IMAGE_H,
                                   TIMEOUT_FULL_IMAGE_W,
                                   TIMEOUT_FULL_IMAGE_W/2,
                                   TIMEOUT_FULL_IMAGE_W/2);
    
	int w = timeout_frame.width();
	int h = timeout_frame.height();
    
	memset(timeout_frame.buffer(webrtc::kYPlane), vie_timeout_image_Y[0], w * h);
	memset(timeout_frame.buffer(webrtc::kUPlane), vie_timeout_image_U[0],
			((w + 1) / 2) * ((h + 1) / 2));
	memset(timeout_frame.buffer(webrtc::kVPlane), vie_timeout_image_V[0],
			((w + 1) / 2) * ((h + 1) / 2));
    
	if(TIMEOUT_IMAGE_W > w || TIMEOUT_IMAGE_H > h){
		error("TimeOutImage too large !! \n");
	} else {
		int d_w = (w-TIMEOUT_IMAGE_W) >> 1;
		int d_h = (h-TIMEOUT_IMAGE_H) >> 1;
        
		uint8_t *ptr1 = timeout_frame.buffer(webrtc::kYPlane);
		const uint8_t *ptr2 = vie_timeout_image_Y;
		ptr1 += (d_w + d_h*w);
		for(int i = 0; i < TIMEOUT_IMAGE_H; i++){
			memcpy(ptr1, ptr2, TIMEOUT_IMAGE_W*sizeof(uint8_t));
			ptr1 += w;
			ptr2 += TIMEOUT_IMAGE_W;
		}
        
        ptr1 = timeout_frame.buffer(webrtc::kUPlane);
        ptr2 = vie_timeout_image_U;
        ptr1 += (d_w/2 + d_h/2*w/2);
        for(int i = 0; i < TIMEOUT_IMAGE_H/2; i++){
            memcpy(ptr1, ptr2, TIMEOUT_IMAGE_W/2*sizeof(uint8_t));
            ptr1 += w/2;
            ptr2 += TIMEOUT_IMAGE_W/2;
        }

        ptr1 = timeout_frame.buffer(webrtc::kVPlane);
        ptr2 = vie_timeout_image_V;
        ptr1 += (d_w/2 + d_h/2*w/2);
        for(int i = 0; i < TIMEOUT_IMAGE_H/2; i++){
            memcpy(ptr1, ptr2, TIMEOUT_IMAGE_W/2*sizeof(uint8_t));
            ptr1 += w/2;
            ptr2 += TIMEOUT_IMAGE_W/2;
        }
	}
}

ViERenderer::ViERenderer()
	: _cb(NULL), _platRenderer(NULL), _rtcRenderer(NULL), _view(NULL),
	_vWidth(0), _vHeight(0), _mirror(false), _max_dim(10000), _running(false), _use_timeout_image(true)
{
	lock_alloc(&_lock);
}

ViERenderer::~ViERenderer()
{
	lock_write_get(_lock);
	_running = false;
	if(_rtcRenderer) {
		_rtcRenderer->StopRender(0);
		_cb = NULL;
		delete _rtcRenderer;
	}

	if(_platRenderer) {
		vie_remove_renderer(_platRenderer);
	}
	lock_rel(_lock);

	mem_deref(_lock);
}

void ViERenderer::RenderFrame(const webrtc::VideoFrame& video_frame, int time_to_render_ms)
{
	webrtc::VideoFrame resampled_frame;
	webrtc::VideoFrame* video_frame_ptr = (webrtc::VideoFrame*)&video_frame;

	if( video_frame.width() > _max_dim || video_frame.height() > _max_dim){
		int N;
		webrtc::Scaler scaler;
        
		//N = std::max(video_frame.width()/_max_dim, video_frame.height()/_max_dim);
		N = 2;
        
		scaler.Set(video_frame.width(), video_frame.height(),
					video_frame.width()/N, video_frame.height()/N,
					webrtc::kI420, webrtc::kI420,
					webrtc::kScaleBox);
        
		scaler.Scale(video_frame, &resampled_frame);
        
		video_frame_ptr = &resampled_frame;
	}
    
	int nw = video_frame_ptr->width();
	int nh = video_frame_ptr->height();

	lock_write_get(_lock);

	if (_running) {
		if (nw > 0 && nh > 0 && (nw != _vWidth || nh != _vHeight)) {
			if (_platRenderer != NULL && _view != NULL && _cb != NULL) {
				info("%s: new video size %dx%d (old %dx%d)\n",
					__FUNCTION__, nw, nh, _vWidth, _vHeight);

				if (vie_resize_renderer_for_video(_platRenderer, _view, 
					_rtcRenderer, &_cb, nw, nh, _mirror)) {
					_vWidth = nw;
					_vHeight = nh;
				}

				if(_use_timeout_image){
					webrtc::VideoFrame timeout_frame;
					generate_timeout_image(timeout_frame);
		    
					info("Setting render timeout image to 10 seconds \n");
					_rtcRenderer->SetTimeoutImage(0, timeout_frame, 10000);
				}
			}
		}
		if (_cb) {
			_cb->RenderFrame(0, *video_frame_ptr);
		}
	}
	lock_rel(_lock);
}

bool ViERenderer::IsTextureSupported() const
{
	return false;
}

void ViERenderer::setMirrored(bool mirror)
{
	_mirror = mirror;
	// Force recalc
	_vWidth = _vHeight = 0;
}

void ViERenderer::setMaxdim(int max_dim)
{
	_max_dim = max_dim;
}

void ViERenderer::setUseTimeoutImage(bool use_timeout_image)
{
	_use_timeout_image = use_timeout_image;
}

int ViERenderer::AttachToView(void *view)
{
	int err = 0;

	lock_write_get(_lock);
	if (_rtcRenderer) {
		_rtcRenderer->StopRender(0);
		_rtcRenderer->DeleteIncomingRenderStream(0);
		delete _rtcRenderer;
		_rtcRenderer = NULL;
		_cb = NULL;
	}
		
	if (_platRenderer) {
		vie_remove_renderer(_platRenderer);
		_platRenderer = NULL;
	}
	_platRenderer = vie_render_view_attach(_platRenderer, view);
	if (!_platRenderer) {
		error("%s: _platRenderer NULL\n", __FUNCTION__);
		err = ENODEV;
		goto out;
	}
	_view = view;
	_vWidth = _vHeight = 0;
	
	if (!_rtcRenderer) {
		_rtcRenderer =  webrtc::VideoRender::CreateVideoRender(0, _platRenderer, false);
		_cb = _rtcRenderer->AddIncomingRenderStream(0, 0, 0.0, 0.0, 1.0, 1.0);

		if (!_rtcRenderer) {
			error("%s: _rtcRenderer NULL\n", __FUNCTION__);
			err = ENODEV;
			goto out;
		}
		if (!_cb) {
			error("%s: _cb NULL\n", __FUNCTION__);
			err = ENODEV;
			goto out;
		}
	}
out:
	if(err != 0) {
		vie_remove_renderer(_platRenderer);
		delete _rtcRenderer;
		_cb = NULL;
	}

	lock_rel(_lock);
	return err;
}

void ViERenderer::DetachFromView()
{
	lock_write_get(_lock);
	if(_platRenderer) {
		vie_remove_renderer(_platRenderer);
		_view = NULL;
	}
	lock_rel(_lock);
}

int ViERenderer::Start()
{
	int err = 0;
	if (!_rtcRenderer) {
		return 0;
	}
	lock_write_get(_lock);
	err = _rtcRenderer->StartRender(0);
	_running = (err == 0) ? true : false;
	lock_rel(_lock);
	return err;
}

int ViERenderer::Stop()
{
	int err = 0;
	_vWidth = _vHeight = 0;
	_mirror = false;

	if (!_rtcRenderer) {
		return 0;
	}
	lock_write_get(_lock);
	err = _rtcRenderer->StopRender(0);
	_running = false;
	lock_rel(_lock);
	return err;
}

void *ViERenderer::View() const
{
	return _view;
}

