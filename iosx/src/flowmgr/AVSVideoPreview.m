/*
 * Wire
 * Copyright (C) 2016 Wire Swiss GmbH
 *
 * The Wire Software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * The Wire Software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Wire Software. If not, see <http://www.gnu.org/licenses/>.
 *
 * This module of the Wire Software uses software code from
 * WebRTC (https://chromium.googlesource.com/external/webrtc)
 *
 * *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 * *
 * *  Use of the WebRTC source code on a stand-alone basis is governed by a
 * *  BSD-style license that can be found in the LICENSE file in the root of
 * *  the source tree.
 * *  An additional intellectual property rights grant can be found
 * *  in the file PATENTS.  All contributing project authors to Web RTC may
 * *  be found in the AUTHORS file in the root of the source tree.
 */

#import "AVSVideoPreview.h"
#import "AVSFlowManager.h"

#include <re/re.h>
#include <avs.h>

@implementation AVSVideoPreview
{
	BOOL _attached;
}

- (void)didMoveToWindow
{
	info("Preview(%p) didMoveToWindow %p attached %s\n", self, self.window,
		_attached ? "TRUE" : "FALSE");
	AVSFlowManager *fm = [AVSFlowManager getInstance];
	if (self.window) {
		if (!_attached) {
			if (fm) {
				debug("attachPreviewView %p\n", self);
				[fm attachVideoPreview: self];
				_attached = YES;
			}
		}
	}
	else {
		if (fm) {
			debug("detachPreviewView %p\n", self);
			[fm detachVideoPreview: self];
		}
		_attached = NO;
	}
}

- (void)layoutSubviews
{
	CGRect frame = self.bounds;

	for (CALayer *l in self.layer.sublayers) {
		l.frame = frame;
	} 
}

- (void)startVideoCapture
{
	AVSFlowManager *fm = [AVSFlowManager getInstance];
	if (fm) {
		debug("preview startCapture\n");
		[fm startVideoCapture];
	}
}

- (void)stopVideoCapture
{
	AVSFlowManager *fm = [AVSFlowManager getInstance];
	if (fm) {
		debug("preview stopCapture\n");
		[fm stopVideoCapture];
	}
}

@end

