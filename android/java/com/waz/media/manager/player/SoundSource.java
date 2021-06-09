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
package com.waz.media.manager.player;


import android.content.Context;
import android.net.Uri;
import android.util.Log;

import android.media.MediaPlayer;
import android.media.MediaPlayer.OnPreparedListener;
import android.media.MediaPlayer.OnSeekCompleteListener;
import android.media.MediaPlayer.OnCompletionListener;
import android.media.MediaPlayer.OnErrorListener;

import com.waz.media.manager.player.MPState;
import com.waz.media.manager.player.MediaSource;
import com.waz.media.manager.player.MediaSourceListener;

import com.waz.media.manager.MediaManager;

import android.media.AudioManager;

public class SoundSource implements MediaSource, OnPreparedListener, OnSeekCompleteListener, OnCompletionListener, OnErrorListener {

  // Set to true to enable debug logs.
  private static final boolean DEBUG = false;
    
  private String _name = null;
  private Context _context = null;
  private Uri _uri = null;
  private int _stream = android.media.AudioManager.USE_DEFAULT_STREAM_TYPE;

  private MediaPlayer _player = null;
  private MediaSourceListener _listener = null;

  private boolean _requestPlay = false;
  private boolean _requestStop = false;

  private MPState _currentState = MPState.NULL;

  private float _volume = 1;
  private boolean _shouldLoop = false;

  private boolean isUpdating = false;
    
  //private boolean _shouldMuteIncomingSound = false;
  private boolean _shouldMuteOutgoingSound = false;

  private AudioManager _audioManager = null;
    
  public MediaSourceListener getListener ( ) {
    return this._listener;
  }

  public void setListener ( MediaSourceListener listener ) {
    this._listener = listener;
  }


  public float getVolume ( ) {
    return this._volume;
  }

  public void setVolume ( float volume ) {
    this._volume = volume;

    if ( this._player != null ) {
      float level = this._shouldMuteOutgoingSound == true ? 0 : this._volume;

      this._player.setVolume(level, level);
    }
  }


  public boolean getShouldLoop ( ) {
    return this._shouldLoop;
  }

  public void setShouldLoop ( boolean shouldLoop ) {
    this._shouldLoop = shouldLoop;

    if ( this._player != null ) {
      this._player.setLooping(shouldLoop);
    }
  }

  public boolean getShouldMuteOutgoingSound ( ) {
    return this._shouldMuteOutgoingSound;
  }

  public void setShouldMuteOutgoingSound ( boolean shouldMute ) {
    this._shouldMuteOutgoingSound = shouldMute;

    if ( this._player != null ) {
      float level = this._shouldMuteOutgoingSound == true ? 0 : this._volume;

      this._player.setVolume(level, level);
    }
  }


  private void update ( ) {
    if(isUpdating){
        DoLogErr("Sound Source update() allready running ! \n");
        //return;
    }
    isUpdating = true;
      
    boolean playChanged = true;
    boolean stopChanged = true;
    boolean stateChanged = true;

    while ( playChanged || stopChanged || stateChanged ) {
      boolean requestPlay = this._requestPlay;
      boolean requestStop = this._requestStop;
      MPState currentState = this._currentState;

      switch ( this._currentState ) {
        case FAIL: this.onFailState(); break;
        case NULL: this.onNullState(); break;
        case INIT: this.onInitState(); break;
        case PREP: this.onPrepState(); break;
        case IDLE: this.onIdleState(); break;
        case PLAY: this.onPlayState(); break;
        case REST: this.onRestState(); break;
        case DONE: this.onDoneState(); break;
        case STOP: this.onStopState(); break;
      }

      playChanged = this._requestPlay != requestPlay;
      stopChanged = this._requestStop != requestStop;
      stateChanged = this._currentState != currentState;
    }
    isUpdating = false;
  }

  private void onFailState ( ) {
    DoLog("Sound Source on Fail State: " + this._name);

    if ( this._requestPlay ) {
      DoLog("Sound Source -> reset(): " + this._name);

      float vol = getVolume ( );
      boolean looping = getShouldLoop ( );
      this._player.reset();
      setVolume(vol);
      setShouldLoop(looping);
        
      this._currentState = MPState.NULL;
    }
  }

  private void onNullState ( ) {
    DoLog("Sound Source on Null State: " + this._name);

    if ( this._requestPlay ) {
      try {
        DoLog("Sound Source -> setting up: " + this._name);
          
        this._player.setDataSource(this._context, this._uri);

        this._player.setAudioStreamType(this._stream);
          
        this._currentState = MPState.INIT;
      }
      catch ( Exception e ) {
        DoLog("Sound Source -> setting failed: " + this._name);

        this._requestPlay = false;
        this._requestStop = false;

        this._currentState = MPState.FAIL;
      }
    }
  }

  private void onInitState ( ) {
    DoLog("Sound Source on Init State: " + this._name);

    if ( this._requestPlay ) {
      DoLog("Sound Source -> prepareAsync(): " + this._name);

      if(this._stream == android.media.AudioManager.STREAM_RING){
        float vol = getVolume ( );
        boolean looping = getShouldLoop ( );
        
        this._player.reset();


	/*
        if(MediaManager.getInstance(this._context).isWiredHsOn()){
          this._player.setAudioStreamType(AudioManager.STREAM_VOICE_CALL);
          DoLog("Reconfigure to STREAM_VOICE_CALL");
        } else {
          this._player.setAudioStreamType(AudioManager.STREAM_RING);
          DoLog("Reconfigure to STREAM_RING");
        }
	*/

	/* -- Use the stream as indicated by the stream property */	
	this._player.setAudioStreamType(this._stream);
	
        try {
          this._player.setDataSource(this._context, this._uri);
        }
        catch( Exception e){
          DoLogErr("setDataSource failed");
        }
        setVolume(vol);
        setShouldLoop(looping);
      }
        
      this._player.prepareAsync();

      this._currentState = MPState.PREP;
    }
  }

  private void onPrepState ( ) {
    DoLog("Sound Source on Prep State: " + this._name);
  }

  private void onIdleState ( ) {
    DoLog("Sound Source on Idle State: " + this._name);

    if ( this._requestPlay ) {
      DoLog("Sound Source -> start(): " + this._name);

      this._requestPlay = false;

      this._player.start();
      this._player.seekTo(0);

      this._currentState = MPState.PLAY;
    }

    if ( this._requestStop ) {
      DoLog("Sound Source -> stop(): " + this._name);
      this._requestStop = false;

      this._player.stop();

      this._currentState = MPState.STOP;
    }
  }

  private void onPlayState ( ) {
    DoLog("Sound Source on Play State: " + this._name);

    if ( this._requestPlay ) {
      DoLog("Sound Source -> seekTo(0): " + this._name);

      this._requestPlay = false;

      this._player.seekTo(0);

      this._currentState = MPState.PLAY;
    }

    if ( this._requestStop ) {
      DoLog("Sound Source -> stop(): " + this._name);

      this._requestStop = false;

      this._player.stop();

      this._currentState = MPState.STOP;
    }
  }

  private void onRestState ( ) {
    DoLog("Sound Source on Rest State: " + this._name);

    if ( this._requestPlay ) {
      DoLog("Sound Source -> start(): " + this._name);

      this._requestPlay = false;

      this._player.start();

      this._currentState = MPState.PLAY;
    }

    if ( this._requestStop ) {
      DoLog("Sound Source -> stop(): " + this._name);

      this._requestStop = false;

      this._player.stop();

      this._currentState = MPState.STOP;
    }
  }

  private void onDoneState ( ) {
    DoLog("Sound Source on Done State: " + this._name);

    if ( this._requestPlay ) {
      DoLog("Sound Source -> start(): " + this._name);

      this._requestPlay = false;

      this._player.start();

      this._currentState = MPState.PLAY;
    }

    if ( this._requestStop ) {
      DoLog("Sound Source -> stop(): " + this._name);

      this._requestStop = false;

      this._player.stop();

      this._currentState = MPState.STOP;
    }

    if ( this._currentState == MPState.DONE ) {
      DoLog("Sound Source -> stop(): " + this._name);

      this._player.stop();

      this._currentState = MPState.STOP;
    }
  }

  private void onStopState ( ) {
    DoLog("Sound Source on Stop State: " + this._name);
      
    if ( this._requestPlay ) {
      DoLog("Sound Source -> prepareAsync(): " + this._name);

      if(this._stream == android.media.AudioManager.STREAM_RING){
        float vol = getVolume ( );
        boolean looping = getShouldLoop ( );
        
        this._player.reset();

	/*
        if(MediaManager.getInstance(this._context).isWiredHsOn()){
          this._player.setAudioStreamType(AudioManager.STREAM_VOICE_CALL);
          DoLog("Reconfigure to STREAM_VOICE_CALL");
        } else {
          this._player.setAudioStreamType(AudioManager.STREAM_RING);
          DoLog("Reconfigure to STREAM_RING");
        }
	*/
	
	this._player.setAudioStreamType(this._stream);
	
        try {
          this._player.setDataSource(this._context, this._uri);
        }
        catch( Exception e){
          DoLogErr("setDataSource failed");
        }
        setVolume(vol);
        setShouldLoop(looping);
      }
      this._currentState = MPState.PREP;
      try {
          this._player.prepareAsync();
      }
      catch( Exception e){
        DoLogErr("prepareAsync failed");
        this._requestPlay = false;
        this._requestStop = false;
          
        this._currentState = MPState.FAIL;
      }
    }
  }


  public SoundSource ( String name, Context context, Uri uri, int stream ) {
    this._name = name;
    this._context = context;
    this._uri = uri;
    this._stream = stream;

    this._player = new android.media.MediaPlayer();

    this._player.setOnPreparedListener(this);
    this._player.setOnSeekCompleteListener(this);
    this._player.setOnCompletionListener(this);
    this._player.setOnErrorListener(this);
      
    DoLog("Sound Source New: " + name + " " + this._uri + " " + stream);
  }


  public void play ( ) {
    DoLog("Sound Source Play: " + this._uri);

    this._requestPlay = true;
    this._requestStop = false;

    this.update();
  }

  public void stop ( ) {
    DoLog("Sound Source Stop: " + this._uri);

    this._requestStop = true;
    this._requestPlay = false;

    this.update();
  }


  @Override
  public void onPrepared ( MediaPlayer player ) {
    DoLog("Sound Source Prepared: " + this._uri);

    if(this._currentState != MPState.PREP){
        DoLogErr("Sound Source illegal state change " + this._currentState + " to prepared");
    }
      
    this._currentState = MPState.IDLE;

    this.update();
  }

  @Override
  public void onSeekComplete ( MediaPlayer player ) {
    DoLog("Sound Source Seek Completed: " + this._uri);

    this.update();
  }

  @Override
  public void onCompletion ( MediaPlayer player ) {
    DoLog("Sound Source Completion: " + this._uri);

    if ( player.isLooping() ) return;

    this._currentState = MPState.DONE;

    this.update();

    if ( this._listener != null ) {
      this._listener.onFinishedPlaying(this);
    }
  }

  @Override
  public boolean onError ( MediaPlayer player, int what, int extra ) {
    DoLogErr("Sound Source Error: " + this._uri + " " + what + " " + extra);

    this._requestPlay = false;
    this._requestStop = false;

    this._currentState = MPState.FAIL;

    this.update();

    return true;
  }

  final String logTag = "avs SoundSource";
    
  private void DoLog(String msg) {
    if (DEBUG) {
      Log.d(logTag, msg);
    }
  }
    
  private void DoLogErr(String msg) {
      Log.e(logTag, msg);
  }
}
