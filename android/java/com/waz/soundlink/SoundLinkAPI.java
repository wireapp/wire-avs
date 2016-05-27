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
package com.waz.soundlink;
import com.waz.soundlink.SoundLinkListener;
import com.waz.soundlink.SoundLinkTestListener;

import android.content.Context;
import android.media.AudioManager;

import android.util.Log;

public class SoundLinkAPI {
  
  private AudioManager _audioManager;
  private int _Fs;
  private int _bufSize;
    
  private SoundLinkListener _listener = null;
  private SoundLinkTestListener _testListener = null;
    
  private boolean _isListening = false;
  private boolean _isSending = false;
    
  private boolean _isSpeakerOn = false;
    
  public SoundLinkAPI ( Context context ) {
      // Taken from https://www.youtube.com/watch?v=d3kfEeMZ65c @ 30  minutes
      _audioManager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
      
      String rate = _audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
      String size = _audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
      
      _Fs = Integer.parseInt(rate);
      _bufSize = Integer.parseInt(size);
      
      createEngine( context, _Fs, _bufSize );
      
      attach( context );
  }

  public int startSend( byte[] userID ) {
      DoLog("startSend() _isSending " + _isSending);
      if(_isSending){
        stopPlayout();
          
        _isSending = false;
      }
      
      int ret = initSoundLink(userID);
      if(ret >= 0){
          startPlayout();
          
          _isSending = true;
      }
      
      // Save routing setting
      _isSpeakerOn = _audioManager.isSpeakerphoneOn();
      
      // Turn on Speaker
      _audioManager.setSpeakerphoneOn(true);
      
      return(ret);
  }
    
  public void stopSend() {
      DoLog("stopSend() _isSending " + _isSending);
      if(_isSending){
          stopPlayout();
          
          // turn on speaker if was on before
          _audioManager.setSpeakerphoneOn(_isSpeakerOn);
          
          _isSending = false;
      }
  }
    
  public void startListen() {
      DoLog("startListen() _isListening " + _isListening);
      if(!_isListening){
          if(!_isSending){
              // Need to initialize soundLink
              DoLog("initSoundLink to a random variable !! \n");
              int ret = initSoundLink(null);
          }
          
          startRecord();
       
          _isListening = true;
          
          DoLog("_audioManager.isSpeakerphoneOn() " + _audioManager.isSpeakerphoneOn());

      }
  }
    
  public void stopListen() {
      DoLog("stopListen() _isListening " + _isListening);
      if(_isListening){
          stopRecord();
       
          _isListening = false;
      }
  }
    
  public void setListener ( SoundLinkListener listener ) {
    if ( listener != null ) {
      _listener = listener;
    }
  }

  public void setTestListener ( SoundLinkTestListener testListener ) {
    if ( testListener != null ) {
        _testListener = testListener;
    }
  }
    
  public void callBackCaller( String time, byte[] msg, int DeviceLatencyMs ){
    if(_listener != null){
        _listener.onMessage( msg );
    }
    if(_testListener != null){
        _testListener.onMessage( time, msg, DeviceLatencyMs);
    }
    
    if(_listener == null && _testListener == null) {
        DoLog("No SoundLinkListener registered ");
        DoLog("time: " + time);
        DoLog("message length " + msg.length);
        DoLog("msg: " + msg);
        DoLog("deviceLatencyMs: " + DeviceLatencyMs);
    }
  }
    
  public void destroy() {
      detach();
      
      shutdown();
  }
   
  // Below is test functionality
  public int getSampleRate(){
      return _Fs;
  }
    
  public int getBufSize(){
      return _bufSize;
  }
    
  public void startDebugRecording(){
      soundLinkDumpStart();
  }
    
  public void stopDebugRecording(){
      soundLinkDumpStop();
  }

  public void InitSoundLink( byte[] userID ) {
      int ret = initSoundLink(userID);
  }
    
  public void InitDelayEstimator(){
      initDelayEstimator();
  }
    
  /** Native methods, implemented in jni folder */
  public static native void createEngine(Context Ctx, int Fs, int bufSize);
  public static native void startRecord();
  public static native void stopRecord();
  public static native void startPlayout();
  public static native void stopPlayout();
  public static native int initSoundLink(byte[] message);
  public static native int initDelayEstimator();
  public static native void soundLinkDumpStart();
  public static native void soundLinkDumpStop();
  public static native void shutdown();
    
  private native void attach ( Context context );
  private native void detach ( );
    
  final String logTag = "SoundLink java";
    
  private void DoLog(String msg) {
      Log.d(logTag, msg);
  }
}
