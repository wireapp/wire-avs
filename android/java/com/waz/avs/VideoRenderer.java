package com.waz.avs;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;
import android.opengl.GLES20;
import android.util.AttributeSet;
import android.util.Log;
import android.view.TextureView;
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


public class VideoRenderer extends TextureView implements TextureView.SurfaceTextureListener {

	private static final String TAG = "VideoRenderer";
	
	private static final int TARGET_FRAME_RATE = 15;
	private static final int EGL_OPENGL_ES2_BIT = 4;
	private static final int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
	private SurfaceTexture mSurface;
	private EGLDisplay mEglDisplay;
	private EGLSurface mEglSurface;
	private EGLContext mEglContext;
	private EGL10 mEgl;
	private EGLConfig eglConfig;
	private GL10 mGl;

	private int targetFrameDurationMillis;

	private int surfaceHeight;
	private int surfaceWidth;

	public boolean isRunning = false;
	
	private boolean paused = false;
	private boolean isRounded = false;

	private ReentrantLock nativeFunctionLock = new ReentrantLock();	
	private boolean nativeCreated = false;
	private long nativeObject = 0;

	private int targetFps;

	public VideoRenderer(Context context, boolean rounded) {
		super(context);

		Log.d(TAG, "Creating new VideoRenderer");
		
		isRounded = rounded;
		
		init(context);
	}

	private void init(Context context) {
		targetFps = TARGET_FRAME_RATE;

		setSurfaceTextureListener(this);
		setOpaque(false);
	}

	public void onSurfaceTextureAvailable(SurfaceTexture surface,
					      int width, int height) {
		Log.d(TAG, "onSurfaceTextureAvailable: "
		      + width + " x " + height);

		nativeFunctionLock.lock();
		mSurface = surface;

		if (nativeObject != 0)
			destroyRenderer();

		nativeObject = createNative(width, height, isRounded);
		
		nativeFunctionLock.unlock();
	}

	@Override
	public void onSurfaceTextureSizeChanged(SurfaceTexture surface,
						int width, int height) {
		Log.d(TAG, "onSurfaceTextureSizeChanged: wxh="
		      + width + " x " + height);
		setDimensions(width, height);
	}

	@Override
	public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
		Log.d(TAG, "onSurfaceTextureDestroyed: " + surfaceWidth + " x " + surfaceHeight);
		nativeFunctionLock.lock();		
		destroyRenderer();

		mSurface = null;
		nativeFunctionLock.unlock();

		return false;
	}

	public void setDimensions(int width, int height){
		surfaceWidth = width;
		surfaceHeight = height;

		destroyRenderer();
		nativeObject = createNative(width, height, isRounded);
	}

	private void makeCurrent() {
		if (!mEglContext.equals(mEgl.eglGetCurrentContext())
	    || !mEglSurface.equals(mEgl.eglGetCurrentSurface(EGL10.EGL_DRAW))) {
			checkEglError();
			if (!mEgl.eglMakeCurrent(mEglDisplay, mEglSurface,
						 mEglSurface, mEglContext)) {
				Log.e(TAG, "eglMakeCurrent failed");
			}
			checkEglError();
		}
	}

	private boolean enter() {
		nativeFunctionLock.lock();
		if (nativeObject == 0) {
			nativeFunctionLock.unlock();
			return false;
		}

		if (mGl == null && mSurface != null)
			initGL();
		
		makeCurrent();
		return true;
	}
	
	private void exit() {
		if (nativeObject != 0) {
			if (!mEgl.eglSwapBuffers(mEglDisplay, mEglSurface)) {
				Log.e(TAG, "cannot swap buffers!");
			}
		}

		nativeFunctionLock.unlock();		
	}

	private void checkEglError() {
		final int error = mEgl.eglGetError();
		if (error != EGL10.EGL_SUCCESS) {
			Log.e(TAG, "EGL error = 0x"
			      + Integer.toHexString(error));
		}
	}

	private void checkGlError() {
		final int error = mGl.glGetError();
		if (error != GL11.GL_NO_ERROR) {
			Log.e(TAG, "GL error = 0x"
			      + Integer.toHexString(error));
		}
	}

	private void initGL() {
		mEgl = (EGL10) EGLContext.getEGL();
		mEglDisplay = mEgl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
		if (mEglDisplay == EGL10.EGL_NO_DISPLAY) {
			Log.e(TAG, "eglGetDisplay failed "
			   + GLUtils.getEGLErrorString(mEgl.eglGetError()));
			return;
		}
		int[] version = new int[2];
		if (!mEgl.eglInitialize(mEglDisplay, version)) {
			Log.e(TAG, "eglInitialize failed "
			   + GLUtils.getEGLErrorString(mEgl.eglGetError()));
			return;
		}
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
		eglConfig = null;
		if (!mEgl.eglChooseConfig(mEglDisplay, configSpec, configs, 1,
					  configsCount)) {
			Log.e(TAG, "eglChooseConfig failed "
			+ GLUtils.getEGLErrorString(mEgl.eglGetError()));
			return;
		}
		else if (configsCount[0] > 0) {
			eglConfig = configs[0];
		}
		if (eglConfig == null) {
			Log.e(TAG, "eglConfig not initialized");
			return;
		}
		int[] attrib_list = {
			EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE
		};
		mEglContext = mEgl.eglCreateContext(mEglDisplay,
						    eglConfig, EGL10.EGL_NO_CONTEXT, attrib_list);
		checkEglError();
		mEglSurface = mEgl.eglCreateWindowSurface(mEglDisplay, eglConfig, mSurface, null);
		checkEglError();
		if (mEglSurface == null || mEglSurface == EGL10.EGL_NO_SURFACE) {
			int error = mEgl.eglGetError();
			if (error == EGL10.EGL_BAD_NATIVE_WINDOW) {
				Log.e(TAG,
				      "eglCreateWindowSurface returned EGL10.EGL_BAD_NATIVE_WINDOW");
			}
			Log.e(TAG, "eglCreateWindowSurface failed "
			      + GLUtils.getEGLErrorString(error));
			return;
		}
		if (!mEgl.eglMakeCurrent(mEglDisplay, mEglSurface,
					 mEglSurface, mEglContext)) {
			Log.e(TAG, "eglMakeCurrent failed "
			      + GLUtils.getEGLErrorString(mEgl.eglGetError()));
			return;
		}
		checkEglError();
		mGl = (GL10) mEglContext.getGL();
		checkEglError();
	}

	private void destroyGL() {
		if (mEgl != null)
			mEgl.eglDestroyContext(mEglDisplay, mEglContext);
		mGl = null;
	}


	@Override
	public void onSurfaceTextureUpdated(SurfaceTexture surface) {
	}

	private void destroyRenderer() {
		destroyNative(nativeObject);
		nativeObject = 0;
		
		destroyGL();
	}

	private native long createNative(int width, int height,
					 boolean rounded);
	private native void destroyNative(long obj);
}
        


