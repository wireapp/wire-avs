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
package com.waz.call;


import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.view.View;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.File;

import com.waz.avs.AVSystem;
import com.waz.avs.VideoCapturer;
import com.waz.avs.VideoCapturerCallback;
import com.waz.avs.VideoCapturerInfo;
import com.waz.call.FlowSource;
import com.waz.call.RequestHandler;

import com.waz.call.FlowManagerListener;

import com.waz.media.manager.MediaManager;
import com.waz.media.manager.MediaManagerListener;

import com.waz.media.manager.router.AudioRouter;

import com.waz.log.LogHandler;

import java.util.Set;
import java.util.Map;

import java.util.HashSet;
import java.util.HashMap;

import java.util.Iterator;

import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONException;


import android.view.TextureView;

public class FlowManager
	implements VideoCapturerCallback {

  static {
	  AVSystem.load();
  }
	
  // Keep these in sync with avs_flowmgr.h
  public final static int MCAT_NORMAL   = 0;
  public final static int MCAT_HOLD     = 1;
  public final static int MCAT_PLAYBACK = 2;
  public final static int MCAT_CALL     = 3;
  public final static int MCAT_CALL_VIDEO = 4;
    
  public final static int AUSRC_INTMIC  = 0;
  public final static int AUSRC_EXTMIC  = 1;
  public final static int AUSRC_HEADSET = 2;
  public final static int AUSRC_BT      = 3;	
  public final static int AUSRC_LINEIN  = 4;
  public final static int AUSRC_SPDIF   = 5;

  public final static int AUPLAY_EARPIECE = 0;
  public final static int AUPLAY_SPEAKER  = 1;
  public final static int AUPLAY_HEADSET  = 2;
  public final static int AUPLAY_BT       = 3;
  public final static int AUPLAY_LINEOUT  = 4;
  public final static int AUPLAY_SPDIF    = 5;

  public final static int VIDEO_SEND_NONE = 0;
  public final static int VIDEO_PREVIEW   = 1;
  public final static int VIDEO_SEND      = 2;

  public final static int VIDEO_STATE_STOPPED = 0;
  public final static int VIDEO_STATE_STARTED = 1;

  public final static int VIDEO_REASON_NORMAL = 0;
  public final static int VIDEO_REASON_BAD_CONNECTION = 1;

  public final static int AUDIO_INTERRUPTION_STOPPED = 0;
  public final static int AUDIO_INTERRUPTION_STARTED = 1;
               
  public final static int LOG_LEVEL_DEBUG = 0;
  public final static int LOG_LEVEL_INFO  = 1;
  public final static int LOG_LEVEL_WARN  = 2;
  public final static int LOG_LEVEL_ERROR = 3;

  private static FlowManager sharedFm = null;
	
  public long fmPointer;
	
  private final Context context;
  private final RequestHandler handler;
    
  private HashSet<FlowManagerListener> _listenerSet = null;


  private HashSet<FlowManagerListener> getListenerSet ( ) {
    if ( this._listenerSet == null ) {
      this._listenerSet = new HashSet<FlowManagerListener>();
    }

    return this._listenerSet;
  }


  private FlowManager getFlowManager ( ) {
    return this;
  }

  private MediaManager getMediaManager ( ) {
    return MediaManager.getInstance(this.context);
  }

  public FlowManager ( final Context context, RequestHandler handler ) {
	  this(context, handler, 0);
  }

  // AVS Flags is used as a bitfield to enable AVS settings.
  // Current settings are:
  // AVS_FLAG_EXPERIMENTAL   = 1<<0. Should be enabled for internal builds.
  // AVS_FLAG_AUDIO_TEST     = 1<<1. Audio Test mode for autmatic testing by QA.
  // AVS_FLAG_VIDEO_TEST     = 1<<2. Video Test mode for autmatic testing by QA.
  public FlowManager (final Context context, RequestHandler handler,
		      long flags ) {
      this.context = context;
      this.handler = handler;
                   
      sharedFm = this;
      
      attach(context, flags);

      File fileDir = context.getFilesDir();      
      setFilePath(fileDir.getAbsolutePath());
  }
               
	
  public static FlowManager getInstance() {
	  return sharedFm;
  }

  public static void cameraFailed() {
	  if (sharedFm == null)
		  return;

	  sharedFm.onCameraFailed();
  }
	
  protected void finalize ( ) throws Throwable  {
    detach();

    super.finalize();
  }


  public void addListener ( FlowManagerListener listener ) {
    if ( listener != null ) {
      this.getListenerSet().add(listener);
    }
  }

  public void removeListener ( FlowManagerListener listener ) {
    if ( listener != null ) {
      this.getListenerSet().remove(listener);
    }
  }


  public native int audioPlayDeviceChanged ( int auplay );
  public native int audioSourceDeviceChanged ( int ausrc );

  public native int mediaCategoryChanged ( String convId, int mCat );


  private native void attach ( Context context, long avs_flags );
  private native void detach ( );


  private void updateMode ( String convId, int mCat ) {
    FlowManager flowManager = this.getFlowManager();
    MediaManager mediaManager = this.getMediaManager();
  
    if ( mCat == FlowManager.MCAT_CALL_VIDEO){
      mediaManager.setVideoCallState(convId);
    } else if ( mCat == FlowManager.MCAT_CALL ) {
      mediaManager.setCallState(convId, true);
    } else {
      mediaManager.setCallState(convId, false);
    }
  }

  private void updateVolume ( String convId, String partId, float volume ) {
    this.volumeChanged(convId, partId, volume);
  }


  public void conferenceParticipants( String convId, String[] participants ) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

    while ( iterator.hasNext() ) {
      FlowManagerListener listener = iterator.next();

      listener.conferenceParticipants(convId, participants);
    }	  
  }

  private void onCameraFailed() {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

    while ( iterator.hasNext() ) {
      FlowManagerListener listener = iterator.next();

      listener.cameraFailed();
    }
  }
	
  private void handleError ( String convId, int error ) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

    while ( iterator.hasNext() ) {
      FlowManagerListener listener = iterator.next();

      listener.handleError(convId, error);
    }
  }

  private void mediaEstablished ( String convId ) {

     Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

     while ( iterator.hasNext() ) {
	     FlowManagerListener listener = iterator.next();

	     listener.mediaEstablished(convId);
     }
  }

  private void changeVideoState(int state, int reason) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();
      while ( iterator.hasNext() ) {
        FlowManagerListener listener = iterator.next();

        listener.changeVideoState(state, reason);
      }
  }

  private void changeVideoSize(int width, int height) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();
      while ( iterator.hasNext() ) {
        FlowManagerListener listener = iterator.next();

        listener.changeVideoSize(width, height);
      }
  }
	
  private void releaseVideoView (String convId, String partId) {

    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

    while ( iterator.hasNext() ) {
      FlowManagerListener listener = iterator.next();

      listener.releaseVideoView(convId, partId);
    }
  }

  private void changeAudioState(int state) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();
      while ( iterator.hasNext() ) {
        FlowManagerListener listener = iterator.next();
        
        listener.changeAudioState(state);
    }
  }
               
  private void volumeChanged ( String convId, String partId, float volume ) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

    while ( iterator.hasNext() ) {
      FlowManagerListener listener = iterator.next();

      listener.volumeChanged(convId, partId, volume);
    }
  }

  public native void refreshAccessToken(String token, String type);
  public native void setSelfUser(String userId);
    
  public native void response(int status, String ctype,
			      byte[] content, long ctx);
  // Return true if event was handled, false otherwise
  public native boolean event(String ctype, byte[] content);
  
  public native boolean acquireFlows(String convId, String sessionId);
  public native void releaseFlowsNative(String convId);

  public void releaseFlows(String convId) {
	  releaseCapturer();

	  releaseFlowsNative(convId);
  }
	  
  public native void setActive(String convId, boolean active);
  public native void addUser(String convId, String userId, String name);

  public native void setLogHandler(LogHandler logh);
	
  public native int setMute(boolean mute);
  public native boolean getMute();

  public native void setEnableLogging(boolean enable);
  public native void setEnableMetrics(boolean enable);
  public native void setSessionId(String convId, String sessId);
  public native void callInterruption(String convId, boolean interrupted);

  // Called after network has changed, AND the event channel
  // (aka websocket) has been (re-)established	
  public native void networkChanged();

  public native static void setLogLevel(int logLevel);
  public native static String[] sortConferenceParticipants(String[] participants);

  private int request(String path, String method,
  		    String ctype, byte[] content, long ctx)
  {
  	return handler.request(this, path, method, ctype, content, ctx);
  }

  public native boolean canSendVideo(String convId);

  private VideoCapturer videoCapturer = null;
  private TextureView previewView = null;
  private int defaultFacing = VideoCapturerInfo.FACING_FRONT;
	
  public void setVideoSendState(String convId, int state) {
	  switch (state) {
	  case VIDEO_SEND_NONE:
		  setVideoSendStateNative(convId, state);	  
		  break;
		  
	  case VIDEO_PREVIEW:
	  case VIDEO_SEND:
		  if (this.videoCapturer == null) {
			  createCapturer();
		  }
		  if (state == VIDEO_SEND) {
			  setVideoSendStateNative(convId, state);	  
		  }
		  break;

	  default:
		  break;
	  }
  }
  public native void setVideoSendStateNative(String convId, int state);


  private static String TAG = "FlowManager";
      
  public void setVideoPreview(String convId, View view) {
	  final TextureView tv = (TextureView)view;
	  
	  Log.d(TAG, "setVideoPreview: " + view + " vcap=" + videoCapturer); 

	  this.previewView = tv;
	  if (view == null) {
		  if (videoCapturer != null) {
			  releaseCapturer();
		  }
	  }
	  else if (videoCapturer == null) {
		  // Create and start the capturer
		  createCapturer();
	  }
	  else {
		  videoCapturer.startCapture(tv);
	  }
  }

	
  public native void setVideoPreviewNative(String convId, View view);
  public native void setVideoView(String convId, String partId, View view);

  public void setVideoCaptureDevice(String convId, String dev) {
	  int facing = VideoCapturerInfo.FACING_UNKNOWN;
	  if (dev.equals("front")) {
		  facing = VideoCapturerInfo.FACING_FRONT;
	  }
	  else if (dev.equals("back")) {
		  facing = VideoCapturerInfo.FACING_BACK;
	  }

	  if (facing == defaultFacing)
		  return;

	  defaultFacing = facing;
	  createCapturer();
  }
	
  public CaptureDevice[] getVideoCaptureDevices() {
	  VideoCapturerInfo[] cs = VideoCapturer.getCapturers();
	  
	  int n = cs.length;

	  Log.d(TAG, "getVideoCaptureDevices: " + n);
	  CaptureDevice[] devs = new CaptureDevice[cs.length];

	  int i = 0;
	  for (VideoCapturerInfo c: cs) {
		  switch (c.facing) {			  
		  case VideoCapturerInfo.FACING_FRONT:
			  devs[i] = new CaptureDevice("front", "front");
			  break;

		  case VideoCapturerInfo.FACING_BACK:
			  devs[i] = new CaptureDevice("back", "back");
			  break;

		  default:
			  devs[i] = new CaptureDevice("unknown", "unknown");
			  break;
		  }
		  ++i;
	  }

	  return devs;
  }

  private native void setFilePath(String path);
    
  public native int setAudioEffect(int effect_type);
               
  final String logTag = "avs FlowManager";
    
  private void DoLog(String msg) {
     Log.d(logTag, msg);
  }

  public void setBackground(boolean bg) {
	  Log.d(TAG, "NOOP setBackground");
  }

	// VideoCapturerCallback
	@Override
	public void onSurfaceDestroyed(VideoCapturer cap) {
		Log.d(TAG, "onSurfaceDestroyed: cap=" + cap);
		//this.videoCapturer = null;
		//this.previewView = null;
	}

	private void createCapturer() {

		releaseCapturer();
		
		Log.d(TAG, "createCapturer: creating preview="
		      + this.previewView);
		
		this.videoCapturer = new VideoCapturer(defaultFacing,
						       640, 480, 15);
		this.videoCapturer.setCallback(this);

		if (this.previewView != null)
			this.videoCapturer.startCapture(this.previewView);
		
		Log.d(TAG, "createCapturer: created");
	}

	private void releaseCapturer() {
		if (videoCapturer != null) {
			videoCapturer.destroy();
			videoCapturer = null;
		}
	}
}
