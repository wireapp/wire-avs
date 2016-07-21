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

#ifndef CAPTURE_ROUTER_H
#define CAPTURE_ROUTER_H

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/video_send_stream.h"

int  vie_capture_router_init(void);
void vie_capture_router_deinit(void);

void vie_capture_router_attach_stream(webrtc::VideoCaptureInput *stream_input,
	bool needs_buffer_rotation);

void vie_capture_router_detach_stream(webrtc::VideoCaptureInput *stream_input);

#endif  // CAPTURE_ROUTER_H

