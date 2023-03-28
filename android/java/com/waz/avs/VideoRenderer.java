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
import android.graphics.SurfaceTexture;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;
import android.opengl.GLES20;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.SurfaceHolder;

import java.util.concurrent.locks.ReentrantLock;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.opengles.GL11;


public class VideoRenderer extends GLSurfaceView
        implements GLSurfaceView.Renderer {

	private static final String TAG = "VideoRenderer";
	
	private static final int TARGET_FRAME_RATE = 15;
	private static final int EGL_OPENGL_ES2_BIT = 4;
	private static final int EGL_CONTEXT_CLIENT_VERSION = 0x3098;

	private int targetFrameDurationMillis;

	public boolean isRunning = false;
	
	private boolean paused = false;
	private boolean shouldFill = true;
	private float fillRatio = 0.0f;
	private boolean isRounded = false;
	private boolean hasSurface = false;

	private ReentrantLock nativeFunctionLock = new ReentrantLock();	
	private boolean nativeCreated = false;
	private long nativeObject = 0;

	private int targetFps;
	private String userId = null;
	private String clientId = null;

	public VideoRenderer(Context context, String userId, String clientId, boolean rounded) {
		super(context);

		Log.d(TAG, "Creating new VideoRenderer( " + this + "): " + userId + "." + clientId + "r=" + rounded);
		
		this.isRounded = rounded;
		this.userId = userId;
		this.clientId = clientId;

		init(context);
	}

	private void init(Context context) {
		targetFps = TARGET_FRAME_RATE;

		// Setup the context factory for 2.0 rendering.
		// See ContextFactory class definition below
		setEGLContextFactory(new ContextFactory());

		// We need to choose an EGLConfig that matches the format of
		// our surface exactly. This is going to be done in our
		// custom config chooser. See ConfigChooser class definition
		// below.
		setEGLConfigChooser(new ConfigChooser());

		// Set the renderer responsible for frame rendering
		this.setRenderer(this);
		this.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
	}


	@Override
	public void onDrawFrame(GL10 gl) {
		nativeFunctionLock.lock();

		//Log.d(TAG, "onDrawFrame(" + this + ") native=" + nativeObject);

		if (nativeObject == 0 || !hasSurface) {
			nativeFunctionLock.unlock();
			return;
		}

		renderFrame(nativeObject);
		
		nativeFunctionLock.unlock();
	}

	@Override
	public void onSurfaceCreated(GL10 gl, EGLConfig config) {
		Log.d(TAG, "onSurfaceCreated(" + this + ")");

		hasSurface = true;
	}

	@Override
	public void onSurfaceChanged(GL10 gl, int width, int height) {
		Log.d(TAG, "onSurfaceChanged(" + this + "): wxh="
		      + width + "x" + height + " native=" + nativeObject);

		nativeFunctionLock.lock();
		if (nativeObject != 0)
			destroyNative(nativeObject);
		nativeObject = createNative(userId, clientId,
					    width, height,
					    isRounded);
		if (nativeObject != 0) {
			nativeSetShouldFill(nativeObject, this.shouldFill);
			nativeSetFillRatio(nativeObject, this.fillRatio);
		}

		nativeFunctionLock.unlock();
		Log.d(TAG, "onSurfaceChanged(" + this + "): native=" + nativeObject);

		if (nativeObject != 0)
			hasSurface = true;
	}

	private boolean enter() {
		nativeFunctionLock.lock();
		return true;
	}
	
	private void exit() {
		this.requestRender();
		nativeFunctionLock.unlock();
	}

	public void destroyRenderer() {
		destroyNativeObject();
	}

	public void setShouldFill(boolean shouldFill) {
		this.shouldFill = shouldFill;
		nativeFunctionLock.lock();
		nativeSetShouldFill(nativeObject, shouldFill);
		nativeFunctionLock.unlock();
	}

	public void setFillRatio(float ratio) {
		this.fillRatio = ratio;
		nativeFunctionLock.lock();
		nativeSetFillRatio(nativeObject, ratio);
		nativeFunctionLock.unlock();
	}

	private void createNativeObject(int width, int height) {
		nativeFunctionLock.lock();
		nativeObject = createNative(userId, clientId,
					    width, height,
					    isRounded);
		nativeFunctionLock.unlock();
	}

	private void destroyNativeObject() {
		nativeFunctionLock.lock();

		if (nativeObject != 0)
			destroyNative(nativeObject);
		nativeObject = 0;

		nativeFunctionLock.unlock();
	}

	public void surfaceDestroyed(SurfaceHolder holder) {
		Log.d(TAG, "surfaceDestroyed(" + this + ")");

		nativeFunctionLock.lock();
		hasSurface = false;
		nativeFunctionLock.unlock();

		super.surfaceDestroyed(holder);
		Log.d(TAG, "surfaceDestroyed(" + this + ") done");
	}

	private native long createNative(String userId,
					 String clientId,
					 int width, int height,
					 boolean rounded);
	private native void renderFrame(long obj);
	private native void destroyNative(long obj);

	private native void nativeSetShouldFill(long obj, boolean shouldFill);

	private native void nativeSetFillRatio(long obj, float ratio);

	private static class ContextFactory implements GLSurfaceView.EGLContextFactory {
		private static int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
		public EGLContext createContext(EGL10 egl, EGLDisplay display, EGLConfig eglConfig) {
			Log.d(TAG, "creating OpenGL ES 2.0 context");
			int[] attrib_list = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE };
			EGLContext context = egl.eglCreateContext(display, eglConfig,
								  EGL10.EGL_NO_CONTEXT, attrib_list);
			Log.d(TAG, "After eglCreateContext");
			return context;
		}

		public void destroyContext(EGL10 egl, EGLDisplay display, EGLContext context) {
			egl.eglDestroyContext(display, context);
		}
	}

	private static class ConfigChooser implements GLSurfaceView.EGLConfigChooser {
		public ConfigChooser() {
		}
		public EGLConfig chooseConfig(EGL10 egl, EGLDisplay display) {
			Log.d(TAG, "chooseConfig");

			int[] configsCount = new int[1];
			EGLConfig[] configs = new EGLConfig[1];
			int[] configSpec = {
				EGL10.EGL_RENDERABLE_TYPE,
				EGL_OPENGL_ES2_BIT,
				EGL10.EGL_RED_SIZE, 8,
				EGL10.EGL_GREEN_SIZE, 8,
				EGL10.EGL_BLUE_SIZE, 8,
				EGL10.EGL_ALPHA_SIZE, 8,
				EGL10.EGL_DEPTH_SIZE, 0,
				EGL10.EGL_STENCIL_SIZE, 0,
				EGL10.EGL_NONE
			};
			EGLConfig eglConfig = null;
			if (!egl.eglChooseConfig(display, configSpec, configs, 1,
						 configsCount)) {
				Log.e(TAG, "eglChooseConfig failed "
				      + GLUtils.getEGLErrorString(egl.eglGetError()));
				return null;
			}
			else if (configsCount[0] > 0) {
				Log.d(TAG, "eglChoose returned: " + configsCount[0] + " configs");
				eglConfig = configs[0];
			}
			if (eglConfig == null) {
				Log.e(TAG, "eglConfig not initialized");
				return null;
			}
			return eglConfig;
		}
	}
}



