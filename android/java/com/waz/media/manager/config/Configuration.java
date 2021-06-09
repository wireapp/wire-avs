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

import com.waz.media.manager.config.MediaConfiguration;


public class Configuration {
  private static final String SOUNDS_KEY = "sounds";


  private JSONObject _json = null;

  private HashMap<String, MediaConfiguration> _soundMap  = null;


  public HashMap<String, MediaConfiguration> getSoundMap ( ) {
    if ( this._soundMap == null ) {
      this._soundMap = new HashMap<String, MediaConfiguration>();
    }

    return this._soundMap;
  }

  private void setSoundMap ( HashMap<String, MediaConfiguration> soundMap ) {
    this._soundMap = soundMap;
  }


  public Configuration ( JSONObject json ) {
    this._json = json;

    this.parse();
  }


  private void parse ( ) {
    if ( this._json != null ) {
      this.parseSounds(this._json.optJSONObject(Configuration.SOUNDS_KEY));
    }
  }

  private void parseSounds ( JSONObject json ) {
    HashMap<String, MediaConfiguration> soundMap = new HashMap<String, MediaConfiguration>();

    JSONArray keys = json.names();

    for ( int i = 0; i < keys.length(); i += 1 ) {
      String key = keys.optString(i, null);
      JSONObject value = json.optJSONObject(key);

      MediaConfiguration sound = new MediaConfiguration(key, value);

      soundMap.put(sound.getName(), sound);
    }

    this.setSoundMap(soundMap);
  }
}
