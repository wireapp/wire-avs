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
package com.waz.media.manager.config;


import java.util.HashSet;
import java.util.HashMap;

import java.util.Iterator;

import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONException;


public class MediaConfiguration {
  private static final String NAME_KEY = "eventId";
  private static final String PATH_KEY = "path";
  private static final String FORMAT_KEY = "format";

  private static final String MIXING_KEY = "mixingAllowed";
  private static final String INCALL_KEY = "incallAllowed";

  private static final String INTENSITY_KEY = "intensity";

  private static final String LOOPING_KEY = "loopAllowed";

  private static final String REQUIRE_P_KEY = "requirePlayback";
  private static final String REQUIRE_R_KEY = "requireRecording";


  private String _key = null;
  private JSONObject _json = null;

  private String _name = null;
  private String _path = null;
  private String _format = null;

  private int _mixing = 0;
  private int _incall = 0;

  private int _intensity = 0;

  private boolean _looping = false;

  private boolean _requirePlaybackMode = false;
  private boolean _requireRecordingMode = false;


  public String getName ( ) {
    return this._name;
  }

  public String getPath ( ) {
    return this._path;
  }

  public String getFormat ( ) {
    return this._format;
  }


  public boolean getMixing ( ) {
    if(this._mixing > 0){
      return true;
    } else {
      return false;
    }
  }

  public boolean getIncall ( ) {
    if(this._incall > 0){
      return true;
    } else {
      return false;
    }
  }

  public int getIntensity ( ) {
    return this._intensity;
  }
  

  public boolean getLooping ( ) {
    return this._looping;
  }


  public boolean getRequirePlaybackMode ( ) {
    return this._requirePlaybackMode;
  }

  public void setRequirePlaybackMode ( boolean require ) {
    this._requirePlaybackMode = require;
  }


  public boolean getRequireRecordingMode ( ) {
    return this._requireRecordingMode;
  }

  public void setRequireRecordingMode ( boolean require ) {
    this._requireRecordingMode = require;
  }


  public MediaConfiguration ( String key, JSONObject json ) {
    this._key = key;
    this._json = json;

    this.parse();
  }


  private void parse ( ) {
    this._name = this._key;

    if ( this._json != null ) {
      this._path = this._json.optString(PATH_KEY, null);
      this._format = this._json.optString(FORMAT_KEY, null);
  
      this._mixing = this._json.optInt(MIXING_KEY, 0);
      this._incall = this._json.optInt(INCALL_KEY, 0);

      this._intensity = this._json.optInt(INTENSITY_KEY, 0);
  
      this._looping = this._json.optInt(LOOPING_KEY, 0) == 1;

      int requirePlaybackMode = this._json.optInt(REQUIRE_P_KEY, -1);
      int requireRecordingMode = this._json.optInt(REQUIRE_R_KEY, -1);

      if ( requirePlaybackMode == -1 ) {
        this._requirePlaybackMode = this._incall > 0;
      }
      else {
        this._requirePlaybackMode = requirePlaybackMode > 0;
      }
  
      if ( requireRecordingMode == -1 ) {
        this._requireRecordingMode = false;
      }
      else {
        this._requireRecordingMode = requireRecordingMode > 0;
      }
    }
  }
}
