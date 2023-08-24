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


import com.waz.call.FlowManager;

import com.waz.media.manager.MediaManager;
import com.waz.media.manager.MediaManagerListener;

import com.waz.media.manager.player.MediaSource;
import com.waz.media.manager.player.MediaSourceListener;

import android.util.Log;

public class FlowSource implements MediaSource {
  private String id = "";

  private float volume = 1;
  private boolean shouldLoop = false;

  private FlowManager flowManager = null;
  private MediaManager mediaManager = null;

  private MediaSourceListener listener = null;


  public FlowSource ( String id, FlowManager flowManager, MediaManager mediaManager ) {
    this.id = id;

    this.flowManager = flowManager;
    this.mediaManager = mediaManager;
  }


  public void play ( ) {
    if ( this.flowManager != null ) {
      DoLog("Change catagory to FlowManager.MCAT_CALL");
      this.flowManager.mediaCategoryChanged(this.id, FlowManager.MCAT_CALL);
    }
  }

  public void stop ( ) {
    if ( this.flowManager != null ) {
      DoLog("Change catagory to FlowManager.MCAT_NORMAL");
      this.flowManager.mediaCategoryChanged(this.id, FlowManager.MCAT_NORMAL);
    }
  }


  public MediaSourceListener getListener ( ) {
    return this.listener;
  }

  public void setListener ( MediaSourceListener listener ) {
    this.listener = listener;
  }


  public float getVolume ( ) {
    // TODO: this should do something

    return this.volume;
  }

  public void setVolume ( float volume ) {
    // TODO: this should do something

    this.volume = volume;
  }


  public boolean getShouldLoop ( ) {
    return this.shouldLoop;
  }

  public void setShouldLoop ( boolean shouldLoop ) {
  }

  final String logTag = "AVS FlowSource";
    
  private void DoLog(String msg) {
    Log.d(logTag, msg);
  }
    
}
