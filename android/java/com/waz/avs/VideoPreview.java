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

import android.graphics.Matrix;
import android.util.Log;
import android.view.TextureView;

import java.lang.Math;



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
	protected void onLayout(boolean changed,
				int l, int t, int r, int b) {

		final float aspectRatio = 4.0f/3.0f;

		super.onLayout(changed, l, t, r, b);
	  
		Matrix m = new Matrix();
		float vWidth = (float)(r - l);
		float vHeight = (float)(b - t);
		float vAspRatio = vWidth / vHeight;
		float tAspRatio = aspectRatio;

		if (orientation % 180 != 0)
			tAspRatio = 1.0f/aspectRatio;
		
		float scaleX = Math.max(1.0f, tAspRatio / vAspRatio);
		float scaleY = Math.max(1.0f, vAspRatio / tAspRatio);
		float dx = - (scaleX * vWidth - vWidth) / 2.0f;
		float dy = - (scaleY * vHeight - vHeight) / 2.0f;

		m.postTranslate(dx, dy);
		m.setScale(scaleX, scaleY);
		setTransform(m);
	}

	@Override
	protected void onMeasure(int wSpec, int hSpec) {
		int w;
		int h;
	  
		w = getDefaultSize(getSuggestedMinimumWidth(), wSpec);
		h = getDefaultSize(getSuggestedMinimumHeight(), hSpec);
		setMeasuredDimension(w, h);
	}
}
