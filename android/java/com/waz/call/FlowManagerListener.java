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

public interface FlowManagerListener {
  public void handleError ( String convId, int error );

  public void mediaEstablished ( String convId );

  public void volumeChanged ( String convId, String partId, float volume );

  public void conferenceParticipants( String convId, String[] participants );

  /* Video callbacks */
  public void createVideoPreview();
  public void releaseVideoPreview();
  public void createVideoView(String convId, String partId);
  public void releaseVideoView(String convId, String partId);
  public void changeVideoState(int state, int reason);
  public void changeVideoSize(int width, int height);
    
  /* Audio callback(s) */
  public void changeAudioState(int state);

  /* Camera callback(s) */
  public void cameraFailed();
}
