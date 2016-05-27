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


import android.util.Log;
import android.content.Context;

import com.waz.avs.AVSystem;

import com.waz.soundlink.SoundLinkAPI;
import com.waz.soundlink.SoundLinkMedia;

import com.waz.media.manager.MediaManager;
import com.waz.media.manager.MediaManagerListener;

import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONException;


public class SoundLink {
  static {
	  AVSystem.load();
  }
    
  private String _id = "SoundLink";

  private SoundLinkAPI _api = null;
  private SoundLinkMedia _media = null;

  private MediaManager _mediamanager = null;


  public SoundLink ( Context context ) {
    JSONObject options = new JSONObject();

    try {
      options.put("eventId", _id);
      options.put("path", "");
      options.put("format", "");
      options.put("mixingAllowed", 1);
      options.put("incallAllowed", 0);
      options.put("loopAllowed", 0);
      options.put("requirePlayback", 1);
      options.put("requireRecording", 1);
    }
    catch ( JSONException e ) {
      e.printStackTrace();
    }

    _api = new SoundLinkAPI(context);

    _media = new SoundLinkMedia(_api);
    _mediamanager = MediaManager.getInstance();

    _mediamanager.registerMedia(_id, options, _media);

    _media.setShouldMuteIncomingSound(true);
    _media.setShouldMuteOutgoingSound(true);

    _mediamanager.playMedia(_id);
  }

  public int startSend ( byte[] userID ) {
    Log.d("DVA:", "SoundLink->startSend(" + userID + ")");

    _media.updateUserID(userID);

    _media.setShouldMuteOutgoingSound(false);
      
    return 0;
  }

  public void stopSend ( ) {
    Log.d("DVA:", "SoundLink->stopSend()");

    _media.setShouldMuteOutgoingSound(true);
  }

  public void startListen ( ) {
    Log.d("DVA:", "SoundLink->startListen()");

    _media.setShouldMuteIncomingSound(false);
  }

  public void stopListen ( ) {
    Log.d("DVA:", "SoundLink->stopListen()");

    _media.setShouldMuteIncomingSound(true);
  }

  public void setListener ( SoundLinkListener listener ) {
    _api.setListener(listener);
  }

  public void setTestListener ( SoundLinkTestListener testListener ) {
    _api.setTestListener(testListener);
  }

  public void callBackCaller ( String time, byte[] msg, int DeviceLatencyMs ) {
    _api.callBackCaller(time, msg, DeviceLatencyMs);
  }

  public void destroy ( ) {
    _mediamanager.stopMedia(_id);
    _mediamanager.unregisterMedia(_id);

    _mediamanager = null;
    _media = null;

    _api.destroy();
  }
}
