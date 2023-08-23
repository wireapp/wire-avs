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
import android.view.SurfaceView;

import java.lang.Math;



public class VideoPreview extends SurfaceView
{
	private float aspectRatio = 4.0f/3.0f;
	private boolean shouldFill = false;
	private int orientation = 0;
	private static final String TAG = "VideoPreview";
	private float vWidth = 1.0f;
	private float vHeight = 1.0f;
	private int tWidth = 0;
	private int tHeight = 0;

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

		updateTransform();
	}

	public void setPreviewSize(int w, int h) {
		this.tWidth = w;
		this.tHeight = h;

		updateTransform();
	}

	@Override
	protected void onLayout(boolean changed,
				int l, int t, int r, int b) {

		super.onLayout(changed, l, t, r, b);
		this.vWidth = (float)(r - l);
		this.vHeight = (float)(b - t);

		Log.d(TAG, "onLayout: " + this.vWidth + "x" + this.tHeight);
		updateTransform();
	}

	private void updateTransform() {

		Log.d(TAG, "updateTransform: " + this.tWidth + "x" + this.tHeight + " -> "
		      + this.vWidth + "x" + this.vHeight
		      + " ori:" + this.irientation);

		if (this.vHeight == 0.0f) {
			return;
		}
		if (this.tHeight == 0.0f) {
			return;
		}

		final float ar = (float)this.tWidth / (float)this.tHeight;
		//Matrix m = new Matrix();
		float vAspRatio = this.vWidth / this.vHeight;
		float tAspRatio = ar;

		if (orientation % 180 != 0)
			tAspRatio = 1.0f/ar;
		
		Log.d(TAG, "updateTransform: " + this.vWidth + "x" + this.vHeight +
		      " ori: " + orientation + " va: " + vAspRatio +
		      " ta: " + tAspRatio);

		float scaleX = 1.0f;
		float scaleY = 1.0f;
		if (this.shouldFill) {
			scaleX = Math.max(1.0f, tAspRatio / vAspRatio);
			scaleY = Math.max(1.0f, vAspRatio / tAspRatio);
		}
		else {
			scaleX = Math.min(1.0f, tAspRatio / vAspRatio);
			scaleY = Math.min(1.0f, vAspRatio / tAspRatio);
		}
		this.setScaleX(scaleX);
		this.setScaleY(scaleY);
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
