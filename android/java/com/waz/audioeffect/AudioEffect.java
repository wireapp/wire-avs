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
package com.waz.audioeffect;

import com.waz.avs.AVSystem;

import com.waz.audioeffect.AudioEffectStatusHandler;

public class AudioEffect {
  static {
      AVSystem.load();
  }
    
  public final static int AVS_AUDIO_EFFECT_CHORUS_MIN        = 0;
  public final static int AVS_AUDIO_EFFECT_CHORUS_MAX        = 1;
  public final static int AVS_AUDIO_EFFECT_REVERB_MIN        = 2;
  public final static int AVS_AUDIO_EFFECT_REVERB_MED        = 3;
  public final static int AVS_AUDIO_EFFECT_REVERB_MAX        = 4;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_MIN      = 5;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_MED      = 6;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_MAX      = 7;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_INSANE   = 8;
  public final static int AVS_AUDIO_EFFECT_PITCH_DOWN_MIN    = 9;
  public final static int AVS_AUDIO_EFFECT_PITCH_DOWN_MED    = 10;
  public final static int AVS_AUDIO_EFFECT_PITCH_DOWN_MAX    = 11;
  public final static int AVS_AUDIO_EFFECT_PITCH_DOWN_INSANE = 12;
  public final static int AVS_AUDIO_EFFECT_PACE_UP_MIN       = 13;
  public final static int AVS_AUDIO_EFFECT_PACE_UP_MED       = 14;
  public final static int AVS_AUDIO_EFFECT_PACE_UP_MAX       = 15;
  public final static int AVS_AUDIO_EFFECT_PACE_DOWN_MIN     = 16;
  public final static int AVS_AUDIO_EFFECT_PACE_DOWN_MED     = 17;
  public final static int AVS_AUDIO_EFFECT_PACE_DOWN_MAX     = 18;
  public final static int AVS_AUDIO_EFFECT_REVERSE           = 19;
  public final static int AVS_AUDIO_EFFECT_VOCODER_MED       = 20;
  public final static int AVS_AUDIO_EFFECT_AUTO_TUNE_MIN     = 21;
  public final static int AVS_AUDIO_EFFECT_AUTO_TUNE_MED     = 22;
  public final static int AVS_AUDIO_EFFECT_AUTO_TUNE_MAX     = 23;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MIN = 24;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MED = 25;
  public final static int AVS_AUDIO_EFFECT_PITCH_UP_DOWN_MAX = 26;
  public final static int AVS_AUDIO_EFFECT_NONE              = 27;
  public final static int AVS_AUDIO_EFFECT_CHORUS_MED        = 28;
  public final static int AVS_AUDIO_EFFECT_VOCODER_MIN       = 29;
    
  public AudioEffect ( ) {
  }
    
  public native int applyEffectWav (String file_name_in, String file_name_out, int effect_type, boolean reduce_noise);
    
  public native int applyEffectPCM (String file_name_in, String file_name_out, int fs_hz, int effect_type, boolean reduce_noise);
    
  public void destroy ( ) {
  }
}
