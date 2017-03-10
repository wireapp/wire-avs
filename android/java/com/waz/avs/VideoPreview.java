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



package com.waz.avs;

import android.content.Context;

import android.util.Log;
import android.view.TextureView;



public class VideoPreview extends TextureView
{
	private float aspectRatio = 4.0f/3.0f;
	private boolean shouldFill = true;
	private int orientation = 90;
	private static final String TAG = "VideoPreview";

	public VideoPreview(Context ctx) {
		super(ctx);
	}
	
	public void setAspectRatio(float ar) {
		this.aspectRatio = ar;
	}


	public void setShouldFill(boolean fill) {
		shouldFill = fill;
	}
	
	public void setVideoOrientation(int ori) {
		orientation = ori;
		Log.d(TAG, "setVideoOrientation: ori: " + orientation);
	}
	
	@Override
	protected void onMeasure(int wSpec, int hSpec) {
		final int width;
		final int height;
		int w;
		int h;
		float ar = this.aspectRatio;

		if (orientation == 90 || orientation == 270) {
			ar = 1.0f / ar;
		}

		width = MeasureSpec.getSize(wSpec);
		height = MeasureSpec.getSize(hSpec);
		float vr = ((float)width) / height;
		
		Log.d(TAG, "onMeasure: ori: " + orientation + " ar: " + this.aspectRatio +
			" ar2: " + ar +  " vr: " + vr + " fill: " + this.shouldFill +
			" w: " + width + " h: " + height);

		if ((shouldFill && (vr > ar))
		 || (!shouldFill && (vr <= ar))) {
			w = width;
			h = (int)((float)width/ar);
		}
		else {
			w = (int)(ar * (float)height);
			h = height;
		}

		setMeasuredDimension(w, h);
	}
}
