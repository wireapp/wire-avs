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

import android.content.Context;
import android.media.MediaFormat;
import android.util.Log;

import java.io.File;

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


  private static String TAG = "AudioEffect";
  private Context context;

  public AudioEffect ( ) {
  }

  public AudioEffect (Context context) {
	  this.context = context;
  }

  public int applyEffectM4A (String file_name_in, String file_name_out, int effect_type, boolean reduce_noise) {

	  MediaFormat media_format = null;
	  File pcm_file = null;
	  File eff_file = null;
	  Context ctx = null;
	  int ret = -1;

	  if (this.context == null) {
		  ctx = AVSystem.context;
	  }
	  else {
		  ctx = this.context;
	  }

	  if (ctx == null) {
		  Log.e(TAG, "AudioEffect: no context for temporary file");
		  return -1;
	  }

	  Log.d(TAG, "applyEffectM4A: in=" + file_name_in + " out=" + file_name_out
		+ " effect=" + effect_type + " denoise=" + reduce_noise);

	  try {
		  pcm_file = File.createTempFile("raw", ".pcm", context.getCacheDir());
		  eff_file = File.createTempFile("aueff", ".pcm", context.getCacheDir());

		  MediaConverter converter = new MediaConverter(file_name_in, file_name_out);

		  Log.d(TAG, "attempting decode: " + pcm_file.getAbsolutePath() + " -> " + eff_file.getAbsolutePath());

		  ret = converter.decode(pcm_file.getAbsolutePath());
		  if (ret != 0)
			  return ret;

		  MediaFormat format = converter.getMediaFormat();
		  int sample_rate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE);

		  ret = applyEffectPCM(pcm_file.getAbsolutePath(),
				       eff_file.getAbsolutePath(),
				       sample_rate, effect_type, reduce_noise);
		  if (ret != 0) {
			  Log.e(TAG, "applyEffect: failed");
			  return ret;
		  }

		  ret = converter.encode(eff_file.getAbsolutePath());
	  }
	  catch (Exception e) {
		  Log.e(TAG, "applyEffectM4A failed: " + e);
		  ret = -1;
	  }
	  finally {
		  if (pcm_file != null)
			  pcm_file.delete();
		  if (eff_file != null)
			  eff_file.delete();
	  }

	  return ret;
  }

  public int[] amplitudeGenerate(String path, int max_value, int max_size) {
      MediaFormat media_format = null;
      File pcm_file = null;
      Context ctx = null;
      int[] amps = null;
      int ret = -1;

      if (this.context == null) {
	  ctx = AVSystem.context;
      }
      else {
	  ctx = this.context;
      }

      if (ctx == null) {
	  Log.e(TAG, "amplitudeGenerate: no context for temporary file");
	  return null;
      }

      Log.d(TAG, "amplitudeGenerate: in=" + path + " max_value=" + max_value
	    + " max_size=" + max_size);

      try {
	  pcm_file = File.createTempFile("raw", ".pcm", context.getCacheDir());

	  MediaConverter converter = new MediaConverter(path, null);

	  Log.d(TAG, "amplitude decode: " + path + " -> " + pcm_file.getAbsolutePath());

	  ret = converter.decode(pcm_file.getAbsolutePath());
	  if (ret != 0) {
	      Log.w(TAG, "amplitude decode failed");
	      return null;
	  }
	  Log.d(TAG, "amplitude generate samples");
	  amps = amplitudeGenerateSamples(pcm_file.getAbsolutePath(), max_value, max_size);
	  Log.d(TAG, "amplitude generate samples done");
      }
      catch (Exception e) {
	  Log.e(TAG, "amplitudeGenerate failed: " + e);
	  return null;
      }
      finally {
	  if (pcm_file != null)
	      pcm_file.delete();
      }

      Log.d(TAG, "amplitude done");
      return amps;
  }
    
  public native int applyEffectWav (String file_name_in, String file_name_out, int effect_type, boolean reduce_noise);
    
  public native int applyEffectPCM (String file_name_in, String file_name_out, int fs_hz, int effect_type, boolean reduce_noise);

  public native int[] amplitudeGenerateSamples(String path, int max_value, int max_size);
    
  public void destroy ( ) {
  }
}
