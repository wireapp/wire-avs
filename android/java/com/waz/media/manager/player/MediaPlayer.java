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


import java.util.Date;

import java.util.HashSet;
import java.util.HashMap;

import java.util.Iterator;

import com.waz.media.manager.player.MediaSource;

import com.waz.media.manager.player.MediaSourceListener;
import com.waz.media.manager.player.MediaPlayerListener;

import android.util.Log;


public class MediaPlayer implements MediaSourceListener {
  private MediaSource _source = null;
  private MediaPlayerListener _listener = null;

  private String _name = null;
  private long _timestamp = 0;
  private boolean _is_playing = false;

  public MediaPlayerListener getListener ( ) {
    return this._listener;
  }

  public void setListener ( MediaPlayerListener listener ) {
    this._listener = listener;
  }


  public String getName ( ) {
    return this._name;
  }

  public void setName ( String name ) {
    this._name = name;
  }


  public long getTimestamp ( ) {
    return this._timestamp;
  }

  public void setTimestamp ( long timestamp ) {
    this._timestamp = timestamp;
  }

  public float getVolume ( ) {
    return this._source.getVolume();
  }

  public void setVolume ( float volume ) {
    if ( this._source.getVolume() != volume ) {
      this._source.setVolume(volume);
    }
  }


  public boolean getShouldLoop ( ) {
    return this._source.getShouldLoop();
  }

  public void setShouldLoop ( boolean shouldLoop ) {
    if ( this._source.getShouldLoop() != shouldLoop ) {
      this._source.setShouldLoop(shouldLoop);
    }
  }

  public MediaPlayer ( MediaSource source ) {
    this._source = source;

    this._source.setListener(this);
  }


  public void play (boolean sync) {
	  long ts = new Date().getTime();

	  this.setTimestamp(ts);

	  if ( this._source != null ) {
		  _is_playing = true;
		  this._source.play();
	  }
	  if (sync) {
		  while (_is_playing)	{
			  try {
				  Thread.sleep(40);
				  // Never wait more than 2s
				  long now = new Date().getTime();
				  if (now - ts > 2000) {
					  this.stop();
					  return;
				  }
			  }
			  catch (Exception e) {
				  return;
			  }
	    }
    }
  }

  public void stop ( ) {
    this.setTimestamp(new Date().getTime());

    // this.setVolume(0);

    if ( this._source != null ) {
      this._source.stop();
        
      _is_playing = false;
    }
  }

  public void onFinishedPlaying ( MediaSource source ) {

	  //Log.d("avs", "MediaPlayer:onFinishedPlaying");
    this.setTimestamp(new Date().getTime());

    
    // this.setVolume(0);
    if(this._listener != null){
      this._listener.onFinishedPlaying(this);
    }
    _is_playing = false;
  }
    
  public boolean getIsPlaying() {
    return _is_playing;
  }
}
