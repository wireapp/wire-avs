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


import java.util.Arrays;
import android.util.Log;

import com.waz.soundlink.SoundLinkAPI;

import com.waz.media.manager.MediaManager;
import com.waz.media.manager.MediaManagerListener;

import com.waz.media.manager.player.MediaSource;
import com.waz.media.manager.player.MediaSourceListener;


public class SoundLinkMedia implements MediaSource {
  private byte[] _id = { };

  private boolean _isPlaying = false;

  private boolean _isSending = false;
  private boolean _isListening = false;

  private float _volume = 1;
  private boolean _shouldLoop = false;

  private boolean _shouldMuteIncomingSound = false;
  private boolean _shouldMuteOutgoingSound = false;

  private SoundLinkAPI _api = null;

  private MediaSourceListener _listener = null;


  public SoundLinkMedia ( SoundLinkAPI api) {
    _api = api;
  }


  private void updateSend ( ) {
    if ( _isSending ) {
      if ( _shouldMuteOutgoingSound || !_isPlaying || _volume <= 0 ) {
        _isSending = false;

        _api.stopSend();

        Log.d("DVA:", "SoundLink Send STOP");
      }
    }
    else {
      if ( !_shouldMuteOutgoingSound && _isPlaying && _volume > 0 ) {
        _isSending = true;

        _api.startSend(_id);

        Log.d("DVA:", "SoundLink Send START");
      }
    }
  }

  private void updateListen ( ) {
    if ( _isListening ) {
      if ( _shouldMuteIncomingSound || !_isPlaying || _volume <= 0 ) {
        _isListening = false;

        _api.stopListen();
        Log.d("DVA:", "SoundLink Listen STOP");
      }
    }
    else {
      if ( !_shouldMuteIncomingSound && _isPlaying && _volume > 0 ) {
        _isListening = true;

        _api.startListen();

        Log.d("DVA:", "SoundLink Listen START");
      }
    }
  }


  public void updateUserID ( byte[] id ) {
    _id = Arrays.copyOf(id, id.length);
  }


  public void play ( ) {
    _isPlaying = true;

    updateSend();
    updateListen();
  }

  public void stop ( ) {
    _isPlaying = false;

    updateSend();
    updateListen();
  }


  public MediaSourceListener getListener ( ) {
    return _listener;
  }

  public void setListener ( MediaSourceListener listener ) {
    _listener = listener;
  }


  public float getVolume ( ) {
    return _volume;
  }

  public void setVolume ( float volume ) {
    if ( _volume != volume ) {
      _volume = volume;

      updateSend();
      updateListen();
    }
  }


  public boolean getShouldLoop ( ) {
    return _shouldLoop;
  }

  public void setShouldLoop ( boolean shouldLoop ) {
    if ( _shouldLoop != shouldLoop ) {
      _shouldLoop = shouldLoop;

      updateSend();
      updateListen();
    }
  }


  public boolean getShouldMuteIncomingSound ( ) {
    return _shouldMuteIncomingSound;
  }

  public void setShouldMuteIncomingSound ( boolean shouldMute ) {
    if ( _shouldMuteIncomingSound != shouldMute ) {
      _shouldMuteIncomingSound = shouldMute;

      updateListen();
    }
  }

  public boolean getShouldMuteOutgoingSound ( ) {
    return _shouldMuteOutgoingSound;
  }

  public void setShouldMuteOutgoingSound ( boolean shouldMute ) {
    if ( _shouldMuteOutgoingSound != shouldMute ) {
      _shouldMuteOutgoingSound = shouldMute;

      updateSend();
    }
  }
}
