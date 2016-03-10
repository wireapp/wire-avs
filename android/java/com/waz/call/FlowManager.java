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

import com.waz.avs.AVSystem;
import com.waz.call.FlowSource;
import com.waz.call.RequestHandler;
import com.waz.voicemessage.VoiceMessageStatusHandler;

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



public class FlowManager implements MediaManagerListener {

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

  public final static int LOG_LEVEL_DEBUG = 0;
  public final static int LOG_LEVEL_INFO  = 1;
  public final static int LOG_LEVEL_WARN  = 2;
  public final static int LOG_LEVEL_ERROR = 3;

  private static FlowManager sharedFm = null;
	
  public long fmPointer;
	
  private final Context context;
  private final RequestHandler handler;

  private VoiceMessageStatusHandler vmHandler = null;
    
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
    return MediaManager.getInstance();
  }


  public FlowManager ( final Context context, RequestHandler handler ) {
    this.context = context;
    this.handler = handler;
    this.vmHandler = null;
    this.getMediaManager().addListener(this);

    sharedFm = this;

    attach(context);
  }

  public static FlowManager getInstance() {
    return sharedFm;
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


  private native void attach ( Context context );
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

  private void createVideoPreview () {

     Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

     while ( iterator.hasNext() ) {
	     FlowManagerListener listener = iterator.next();

	     listener.createVideoPreview();
     }
  }

  private void releaseVideoPreview () {

     Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

     while ( iterator.hasNext() ) {
	     FlowManagerListener listener = iterator.next();

	     listener.releaseVideoPreview();
     }
  }

  private void createVideoView ( String convId, String partId ) {
     Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

     while ( iterator.hasNext() ) {
	     FlowManagerListener listener = iterator.next();

	     listener.createVideoView(convId, partId);
     }
  }

     private void releaseVideoView (String convId, String partId) {

     Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

     while ( iterator.hasNext() ) {
	     FlowManagerListener listener = iterator.next();

	     listener.releaseVideoView(convId, partId);
     }
  }

  private void volumeChanged ( String convId, String partId, float volume ) {
    Iterator<FlowManagerListener> iterator = this.getListenerSet().iterator();

    while ( iterator.hasNext() ) {
      FlowManagerListener listener = iterator.next();

      listener.volumeChanged(convId, partId, volume);
    }
  }

  public void onPlaybackRouteChanged ( int route ) {
    if ( route == AudioRouter.ROUTE_EARPIECE ) {
      this.audioPlayDeviceChanged(FlowManager.AUPLAY_EARPIECE);
    }

    if ( route == AudioRouter.ROUTE_HEADSET ) {
      this.audioPlayDeviceChanged(FlowManager.AUPLAY_HEADSET);
    }

    if ( route == AudioRouter.ROUTE_SPEAKER ) {
      this.audioPlayDeviceChanged(FlowManager.AUPLAY_SPEAKER);
    }
      
    if ( route == AudioRouter.ROUTE_BT ) {
      this.audioPlayDeviceChanged(FlowManager.AUPLAY_BT);
    }
  }

  public native void refreshAccessToken(String token, String type);
  public native void setSelfUser(String userId);
    
  public native void response(int status, String ctype,
			      byte[] content, long ctx);
  // Return true if event was handled, false otherwise
  public native boolean event(String ctype, byte[] content);
  
  public native boolean acquireFlows(String convId, String sessionId);
  public native void releaseFlows(String convId);
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
  public native void setVideoSendState(String convId, int state);
  public native void setVideoPreview(String convId, View view);
  public native void setVideoView(String convId, String partId, View view);
  public native void setVideoCaptureDevice(String convId, String dev);
  public native CaptureDevice[] getVideoCaptureDevices();
  public native void setBackground(boolean backgrounded);
    
  public native void vmStartRecord(String fileName);
  public native void vmStopRecord();
  public native int vmGetLength(String fileName);
  public native void vmStartPlay(String fileName, int startpos);
  public native void vmStopPlay();
  public void vmRegisterHandler(VoiceMessageStatusHandler handler)
  {
      this.vmHandler = handler;
  }
    
  private void vmStatushandler(int is_playing, int cur_time_ms, int file_length_ms)
  {
      boolean playing = true;
      if(is_playing == 0){
          playing = false;
      }
      if(this.vmHandler != null){
          this.vmHandler.vmStatushandler(playing, cur_time_ms, file_length_ms);
      }
  }      
    
  final String logTag = "avs FlowManager";
    
  private void DoLog(String msg) {
     Log.d(logTag, msg);
  }
}
