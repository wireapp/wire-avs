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
import android.util.Rational;
import android.util.Size;
import android.view.Surface;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;
import androidx.camera.core.Camera;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageInfo;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.Preview;
import androidx.camera.core.ResolutionInfo;
import androidx.camera.core.UseCaseGroup;
import androidx.camera.core.ViewPort;
import androidx.camera.core.impl.ImageOutputConfig.RotationValue;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.camera.view.PreviewView;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LifecycleOwner;
import androidx.lifecycle.ViewTreeLifecycleOwner;

import com.google.common.util.concurrent.ListenableFuture;
import com.waz.call.FlowManager;

import java.nio.ByteBuffer;
import java.util.concurrent.Executor;


public class VideoCapturer implements ImageAnalysis.Analyzer {
	
	private static final String TAG = "VideoCapturer";
	
	private Context context;
	private Executor executor;
	private PreviewView previewView;
        private ImageAnalysis imageAnalysis;
	private VideoCapturerCallback capturerCallback = null;
	private boolean started = false;
	private SurfaceHolder surface = null;
	private int facing;
	private int w;
	private int h;
	private int fps;
	private boolean destroying = false;
	private int ui_rotation = Surface.ROTATION_0;
	private Camera camera;
	private CameraSelector cameraSelector = null;
	ProcessCameraProvider cameraProvider = null;

	public VideoCapturer(Context ctx,
			     final int facing,
			     final int w, final int h, final int fps) {

		this.context = ctx;
		initCamera(facing, w, h, fps);
	}

	private void stopCamera() {
		Log.d(TAG, "stopCamera: started=" + this.started);

		if (this.cameraProvider != null)
			this.cameraProvider.unbindAll();
	}
	
	public void destroy() {
		Log.d(TAG, "destroy:");

		destroying = true;
		// How to destroy CameraX??
	}

	public void setCallback(VideoCapturerCallback cb) {
		this.capturerCallback = cb;
	}

	
	public int startCapture(final PreviewView vp) {
		Log.d(TAG, "startCapture on: " + vp);

		this.previewView = vp;

		startCamera();

		return 0;
	}

	private void bindPreview(@NonNull ProcessCameraProvider cameraProvider) {

		Log.i(TAG, "bindPreview: provider=" + cameraProvider);

		Preview preview = new Preview.Builder()
			.setTargetResolution(new Size(this.w, this.h))
			.build();

		this.imageAnalysis = new ImageAnalysis.Builder()
		        .setTargetResolution(new Size(this.w, this.h))
		        .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
		        .build();
		
		preview.setSurfaceProvider(this.previewView.getSurfaceProvider());		

		imageAnalysis.setAnalyzer(this.executor, this);
		imageAnalysis.setTargetRotation(this.ui_rotation);

		ViewPort viewPort = new ViewPort.Builder(new Rational(16, 9), 0)
			.setScaleType(ViewPort.FIT)
			.build();
		
		LifecycleOwner lowner = ViewTreeLifecycleOwner.get(this.previewView);
		UseCaseGroup useCaseGroup = new UseCaseGroup.Builder()
			.setViewPort(viewPort)
			.addUseCase(preview)
			.addUseCase(imageAnalysis)
			.build();
		cameraProvider.unbindAll();
		this.camera = cameraProvider.bindToLifecycle(lowner,
							     this.cameraSelector,
							     useCaseGroup);
		Log.i(TAG, "bindPreview: camera=" + this.camera + " facing: " + this.cameraSelector);

		Log.i(TAG, "bindPreview: viewPort: " + this.previewView.getWidth() + "x" + this.previewView.getHeight() +
		      " preview_rot=" + preview.getTargetRotation() + " output_rot=" + imageAnalysis.getTargetRotation());
		
		ResolutionInfo resInfo = preview.getResolutionInfo();
		if (resInfo == null) {
			Log.i(TAG, "bindPreview: resolution is null");
		}
		else {
			Size res = resInfo.getResolution();
			Log.i(TAG, "bindPreview: preview resolution=" + res.getWidth() + "x" + res.getHeight());
		}
	}

	
	private void initCamera(int facing, int w, int h, int fps) {

		/* Android's camera is 90 degrees rotated, 
		 * thus requires the width and height to be reversed
		 */
		this.w = h;
		this.h = w;
		this.fps = fps;

		if (facing == VideoCapturerInfo.FACING_BACK)
			this.cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA;
		else 
			this.cameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA;
	}

	private void startCamera() {

		Log.i(TAG, "startCamera: " + this.w + "x" + this.h);
		this.executor = ContextCompat.getMainExecutor(this.context);

		final ListenableFuture<ProcessCameraProvider> cameraProviderFuture = ProcessCameraProvider.getInstance(this.context);

		cameraProviderFuture.addListener(new Runnable() {
			@Override
			public void run() {
				try {
					cameraProvider = cameraProviderFuture.get();
					Log.i(TAG, "startCamera: binding preview");
					bindPreview(cameraProvider);

				} catch (Exception e) {
					Log.e(TAG, "startCamera: exception: " + e);
					// No errors need to be handled for this Future.
					// This should never be reached.
				}
			}
		}, this.executor);		
	}

	private void cameraFailed() {
		FlowManager.cameraFailed();
	}

	@Override
	public void analyze(@NonNull ImageProxy image)	{
		 
		ImageProxy.PlaneProxy[] planes = image.getPlanes();
		ImageInfo imageInfo = image.getImageInfo();

		/*
		Log.d(TAG, "analyze: " + image.getWidth() + "x" + image.getHeight() +
		      " format=" + image.getFormat() + 
		      " planes=" + planes.length + " rot=" + imageInfo.getRotationDegrees() +
		      " ystride=" + planes[0].getRowStride() + " / " + planes[0].getPixelStride() +
		      " ustride=" + planes[1].getRowStride() + " / " + planes[1].getPixelStride() +
		      " vstride=" + planes[2].getRowStride());
		*/

		handleCameraFrame2(image.getWidth(), image.getHeight(),
				   planes[0].getBuffer(), planes[0].getRowStride(),
				   planes[1].getBuffer(), planes[1].getRowStride(),
				   planes[2].getBuffer(), planes[1].getPixelStride(),
				   imageInfo.getRotationDegrees(),
				   imageInfo.getTimestamp());
		image.close();
	}

	public int getRotation() {
		return this.ui_rotation;
	}

	/**
	 * Sets the rotation of the UI, so that video frames being sent can be rotated accordingly.
	 * @param rotation Rotation of the UI, expressed as one of
	 *                 {@link Surface#ROTATION_0}, {@link Surface#ROTATION_90},
	 *                 {@link Surface#ROTATION_180}, or {@link Surface#ROTATION_270}.
	 */
	public void setUIRotation(@RotationValue int rotation) {
		this.ui_rotation = rotation;
		Log.d(TAG, "setUIRotation: uirot: " + rotation);
		if (this.imageAnalysis != null) {
			this.imageAnalysis.setTargetRotation(rotation);
		}
	}

	private static native void handleCameraFrame(int w, int h,
						     byte[] data,	     
						     int rotation,
						     long ts);

	private static native void handleCameraFrame2(int w, int h,
						      ByteBuffer ybuf, int ystride,
						      ByteBuffer ubuf, int ustride,
						      ByteBuffer vbuf, int vstride,
						      int rotation,
						      long ts);
}
