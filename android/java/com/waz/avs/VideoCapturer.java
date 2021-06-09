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


import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;

import android.hardware.Camera;
import android.hardware.Camera.CameraInfo;
import android.hardware.Camera.PreviewCallback;

import android.os.Looper;
import android.os.Handler;
import android.os.SystemClock;

import android.util.Log;

import android.view.Gravity;
import android.view.TextureView;

import com.waz.call.FlowManager;

import java.io.IOException;

import java.util.concurrent.locks.ReentrantLock;

import java.util.List;


public class VideoCapturer implements PreviewCallback,
				      TextureView.SurfaceTextureListener {
	
	private static final String TAG = "VideoCapturer";
	
	@SuppressWarnings("deprecation")	
	private Camera camera;
	private CameraInfo cameraInfo;
	private Camera.Size size;
	private VideoCapturerCallback capturerCallback = null;
	private boolean started = false;
	private TextureView previewView = null;
	private int facing;
	private int w;
	private int h;
	private int fps;
	private float ftime;
	private float lastFtime;
	private ReentrantLock lock = new ReentrantLock();
	private boolean destroying = false;
	
	public static VideoCapturerInfo[] getCapturers() {


		@SuppressWarnings("deprecation")	
		int n = Camera.getNumberOfCameras();
		VideoCapturerInfo[] devInfos;
		
		CameraInfo ci = new CameraInfo();

		devInfos = new VideoCapturerInfo[n];		
		
		for (int i = 0; i < n; i++) {
			int k = (n - 1) - i;
			devInfos[k] = new VideoCapturerInfo();
			Camera.getCameraInfo(i, ci);
			switch(ci.facing) {
			case CameraInfo.CAMERA_FACING_BACK:
				devInfos[k].facing =
					VideoCapturerInfo.FACING_BACK;
				break;
				
			case CameraInfo.CAMERA_FACING_FRONT:
				devInfos[k].facing =
					VideoCapturerInfo.FACING_FRONT;
				break;
				
			default:
				break;
			}

			devInfos[k].orientation = ci.orientation;
			devInfos[k].dev = i;
		}

		return devInfos;
	}

	@SuppressWarnings("deprecation")		
	public VideoCapturer(final VideoCapturerInfo cap,
			     final int w, final int h, final int fps) {
		Runnable r = new Runnable() {
			@Override
			public void run() {
				initCameraByCap(cap, w, h, fps);
				synchronized(this) {
					this.notify();
				}
			}
		};
		runSyncOnMain(r);
		
	}
	public VideoCapturer(final int facing,
			     final int w, final int h, final int fps) {

		Runnable r = new Runnable() {
			@Override
			public void run() {
				initCamera(facing, w, h, fps);
				synchronized(this) {
					this.notify();
				}
			}
		};
		runSyncOnMain(r);
	}

	private void stopCamera() {
		Log.d(TAG, "stopCamera: started=" + this.started);
		lock.lock();

		if (camera != null && this.started) {
			try {
				camera.setPreviewCallback(null);
				camera.stopPreview();
				this.started = false;
				//camera.unlock();
				Log.d(TAG, "stopCamera: stopped");
			}
			catch(Exception e) {
				Log.e(TAG, "stopCamera: failed: " + e);
			}
			
			camera.release();
			camera = null;
		}

		lock.unlock();
		
	}
	
	public void destroy() {

	    if (camera == null)
		    return;

	    destroying = true;

	    Runnable r = new Runnable() {
		@Override
		public void run() {
			stopCamera();

			lock.lock();
			/*
			try {
				camera.reconnect();
				camera.release();
				camera = null;
			}
			catch(IOException ioe) {
				Log.e(TAG, "destroy: reconnect failed: " + ioe);
			}
			*/
		
			Log.d(TAG, "destroy: camera released");
			previewView = null;
			destroying = false;

			lock.unlock();
			synchronized(this) {
				this.notify();
			}
		}
	    };

	    runSyncOnMain(r);
	}


	private void runSyncOnMain(Runnable r) {
		Handler mh = new Handler(Looper.getMainLooper());
		
		if (Looper.myLooper() == Looper.getMainLooper()) {
			r.run();
		}
		else {
			synchronized(r) {
				mh.post(r);
				try {
					r.wait();
				}
				catch(Exception e) {
					Log.e(TAG, "wait failed: " + e);
				}
			}
		}
	}
	
	public void setCallback(VideoCapturerCallback cb) {
		this.capturerCallback = cb;
	}

	
	public int startCapture(final TextureView view) {
		Log.d(TAG, "startCapture on: " + view);

		this.previewView = view;

		Runnable r = new Runnable() {
			@Override
			public void run() {
				view.setSurfaceTextureListener(VideoCapturer.this);
				if (view.isAvailable()) {
					startCamera(view.getSurfaceTexture());
				}

				synchronized(this) {
					this.notify();
				}
			}
		};

		runSyncOnMain(r);

		return 0;
	}

	private void initCameraByCap(VideoCapturerInfo cap,
				     int w, int h, int fps) {
		
		boolean failed = false;
		Camera.Parameters params;

		this.facing = cap.facing;
		this.w = w;
		this.h = h;
		this.fps = fps;
		this.ftime = (1000.0f / (float)fps) * 0.8f; /* Allow 20% skew */
		this.lastFtime = 0.0f;
		
		lock.lock();
		try {
			Log.d(TAG, "Using camera dev="+ cap.dev + " facing=" + cap.facing);
			if (camera != null) {
				camera.release();
				camera = null;
			}
			cameraInfo = new CameraInfo();
			Camera.getCameraInfo(cap.dev, cameraInfo);
			Log.d(TAG, "Camera.open");
			camera = Camera.open(cap.dev);
			Log.d(TAG, "Camera.getParameters");
			params = camera.getParameters();

			/* Auto focus */
			Log.d(TAG, "Params.getSupportedFocusModes");
			List<String> focusModes = params.getSupportedFocusModes();
			if (supportsAutoFocus(focusModes)) {
				Log.d(TAG, "Params.setFocusMode: AUTO");
				params.setFocusMode(Camera.Parameters.FOCUS_MODE_AUTO);
			}
			else {
				Log.d(TAG, "no AUTO_FOCUS");
			}

			/* FPS */
			Log.d(TAG, "Params.getSupportedPreviewFps");
			List<int[]> ranges = params.getSupportedPreviewFpsRange();
			int[] range = getFpsRange(fps, ranges);
			if (range != null) {				
				Log.d(TAG, "Params.setPreviewFps: " + range[0] + "-" + range[1]);
				params.setPreviewFpsRange(range[0], range[1]);
			}
			else {
				Log.d(TAG, "no range");	
			}

			/* Size */
			Log.d(TAG, "Params.getSupportedPreviewSize");
			List<Camera.Size> sizes = params.getSupportedPreviewSizes();
			this.size = getPreviewSize(w, h, sizes);
			Log.d(TAG, "Params.setPreviewSize: " + this.size.width + "x" + this.size.height);
			params.setPreviewSize(this.size.width, this.size.height);
			int format = ImageFormat.NV21;
			Log.d(TAG, "Params.setPreviewFormat");
			params.setPreviewFormat(format);
			Log.d(TAG, "Camera.setParams");
			camera.setParameters(params);

			/*
			  int bufSize = size.width * size.height
			  * ImageFormat.getBitsPerPixel(format) / 8;
			  for (int i = 0; i < 4; i++) {
			  camera.addCallbackBuffer(new byte[bufSize]);
			  }
			  camera.setPreviewCallbackWithBuffer(this);
			*/

		}
		catch(Exception e) {
			Log.w(TAG, "initCamera failed: " + e);
			if (camera != null) {
				camera.release();
				camera = null;
			}
			failed = true;
		}
		lock.unlock();


		if (failed)
			cameraFailed();
	}

	private void initCamera(int facing, int w, int h, int fps) {
		VideoCapturerInfo[] capturers;

		try {
			capturers = getCapturers();
		}
		catch (Exception e) {
			Log.e(TAG, "initCamera: failed: " + e);
			cameraFailed();
			return;
		}
		
		int n = capturers.length;		
		if (n < 1) {
			cameraFailed();
			return;
		}
		
		int dev = 0;
		for(VideoCapturerInfo cap: capturers) {
			if (cap.facing == facing) {
				initCameraByCap(cap, w, h, fps);
				break;
			}
		}
	}


	@SuppressWarnings("deprecation")
	private boolean supportsAutoFocus(List<String> modes) {
		for(String mode: modes) {
			Log.d(TAG, "supportsAutoFocus: mode=" + mode);
			if (mode.equals(Camera.Parameters.FOCUS_MODE_AUTO))
				return true;
		}
		return false;
	}

	private int[] getFpsRange(int fps, List<int[]> ranges) {
		int[] sr = null;
		int diff;
		int curr = -1;

		fps = fps * 1000;
				
		for(int[] range: ranges) {
			Log.d(TAG, "getFpsRange(" + fps + "): "
			      + range[0] + " -> " + range[1]);

			
			diff = Math.abs(fps - range[1]);
			if (curr < 0)
				curr = diff;
			if (diff <= curr)
				sr = range;
		}

		return sr;
	}

	private Camera.Size getPreviewSize(int w, int h,
					   List<Camera.Size> sizes) {

		int wdiff;
		int hdiff;
		int minh = -1;
		int minw = -1;
		Camera.Size csz = sizes.get(0);
		
		for(Camera.Size sz: sizes) {
			Log.d(TAG, "getPreviewSize(" + w + " x " + h
			      + ") size=" + sz.width + " x " + sz.height);

			if (sz.width == w && sz.height == h) {
				csz = sz;
				break;
			}
			
			if (sz.width > w)
				wdiff = sz.width - w;
			else
				wdiff = w - sz.width;

			if (sz.height > h)
				hdiff = sz.height - h;
			else
				hdiff = h - sz.height;

			if (minw == -1)
				minw = wdiff;
			if (minh == -1)
				minh = hdiff;

			minw = Math.min(minw, wdiff);
			minh = Math.min(minh, hdiff);

			if (minh == hdiff || minw == wdiff)
				csz = sz;
		}

		Log.i(TAG, "getPreviewSize(" + w + " x " + h
		      + ") using size=" + csz.width + " x " + csz.height);
		
		return csz;
	}

	@Override
	public void onSurfaceTextureAvailable(SurfaceTexture surface,
					      int width, int height) {
		Log.d(TAG, "onSurfaceTextureAvailable: "
		      + width + "x" + height + " camera=" + camera);

		if (!destroying && previewView != null)
			startCamera(surface);
	}


	private void startCamera(SurfaceTexture surface) {

		if (destroying)
			return;
		
		lock.lock();

		Log.d(TAG, "startCamera: cam=" + this.camera
		      + " surface=" + surface);

		if (this.camera == null)
			initCamera(this.facing, this.w, this.h, this.fps);

		if (this.camera == null) {
			Log.w(TAG, "startCamera: failed to initCamera");
		}
		else {
			VideoPreview vp = (VideoPreview)this.previewView;	
			int vrot = getViewRotation();
			
			Log.d(TAG, "startCamera: " + vp + " rot:" + vrot +
					"facing: " + cameraInfo.facing);

			try {
				camera.setDisplayOrientation(vrot);
			}
			catch (RuntimeException e){
				Log.d(TAG, "startCapture: exception calling " +
				      "setDisplayOrientation: " +
				      e.getMessage());
			}
			vp.setVideoOrientation(vrot);

			try {
				if (!this.started) {
					//this.camera.reconnect();
					this.camera.setPreviewTexture(surface);
					this.camera.setPreviewCallback(this);
					this.camera.startPreview();
					this.started = true;
					Log.d(TAG, "startCamera: started");
				}
			}
			catch (Exception e) {
				Log.e(TAG, "startCamera: failed: " + e);
				this.camera.release();
				this.camera = null;
				cameraFailed();
			}
		}

		lock.unlock();
	}

	private void cameraFailed() {
		FlowManager.cameraFailed();
	}
	
	@Override
	public void onSurfaceTextureSizeChanged(SurfaceTexture surface,
						int width, int height) {
		// Ignored, Camera does all the work for us

		Log.d(TAG, "onSurfaceTextureSizeChanged: " + width + "x" + height);
	}

	@Override
	public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
		Log.d(TAG, "onSurfaceTextureDestroyed: " + surface);

		stopCamera();
		if (capturerCallback != null) {
			capturerCallback.onSurfaceDestroyed(this);
		}
		return true;
	}

	@Override
	public void onSurfaceTextureUpdated(SurfaceTexture surface) {
		// Invoked every time there's a new Camera preview frame
	}

	@Override
	public void onPreviewFrame(byte[] frame, Camera cam) {
		long capTime = SystemClock.elapsedRealtime();
		int rotation = getRotation();

		/* Discard too frequent frames */
		if ((capTime - this.lastFtime) < this.ftime) {
			return;
		}
		
		this.lastFtime = capTime;

		handleCameraFrame(this.size.width, this.size.height,
				  frame, rotation, capTime);
		//if (camera != null)
		//	camera.addCallbackBuffer(frame);
	}

	public int getViewRotation() {
		if (camera == null || cameraInfo == null)
			return 0;

		Log.d(TAG, "getViewRotation: camrot: " + cameraInfo.orientation +
			" facing: " + cameraInfo.facing);

		switch (cameraInfo.facing) {
		case CameraInfo.CAMERA_FACING_BACK:
			return cameraInfo.orientation;
			
		case CameraInfo.CAMERA_FACING_FRONT:
			return (360 - cameraInfo.orientation) % 360;

		default:
			return 0;
		}
	}
	

	public int getRotation() {
		if (camera == null || cameraInfo == null)
			return 0;

		return (360 - cameraInfo.orientation) % 360;
	}

	private static native void handleCameraFrame(int w, int h,
						     byte[] data,	     
						     int rotation,
						     long ts);
}
