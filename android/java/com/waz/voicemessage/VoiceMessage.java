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
package com.waz.voicemessage;


import android.util.Log;
import android.content.Context;

import com.waz.call.FlowManager;
import com.waz.voicemessage.VoiceMessageStatusHandler;

public class VoiceMessage {
  private String _id = "VoiceMessage";

  private FlowManager _fm = null;

  public VoiceMessage ( FlowManager fm ) {
    _fm = fm;
  }

  public void vmStartRecord(String fileName){
    if(_fm != null){
      _fm.vmStartRecord(fileName);
    }
  }
  
  public void vmStopRecord(){
    if(_fm != null){
      _fm.vmStopRecord();
    }
  }
    
  public int vmGetLength(String fileName){
    int ret = 0;
    if(_fm != null){
      ret = _fm.vmGetLength(fileName);
    }
    return ret;
  }
    
  public void vmStartPlay(String fileName, int startpos){
    if(_fm != null){
      _fm.vmStartPlay(fileName, startpos);
    }
  }
  
  public void vmStopPlay(){
    if(_fm != null){
      _fm.vmStopPlay();
    }
  }

  public void vmApplyChorus(String fileNameIn, String fileNameOut){
    if(_fm != null){
        _fm.vmApplyChorus(fileNameIn, fileNameOut);
    }
  }

  public void vmApplyReverb(String fileNameIn, String fileNameOut){
    if(_fm != null){
      _fm.vmApplyReverb(fileNameIn, fileNameOut);
    }
  }
    
  public void vmRegisterHandler(VoiceMessageStatusHandler handler){
    if(_fm != null){
      _fm.vmRegisterHandler(handler);
    }
  }
    
  public void destroy ( ) {
  }
}
