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

#import <UIKit/UIKit.h>

#include <re/re.h>
#include <avs.h>

#import "AVSAudioEffect.h"

@implementation AVSAudioEffect

static void progress_status_h(int progress, void *arg)
{
    AVSAudioEffect *ae = (__bridge AVSAudioEffect *)arg;
    
    if ([ae.delegate respondsToSelector:@selector(updateProgress:)]) {
        double prog = (double)progress;
        
        [ae.delegate updateProgress:prog];
    }
}

- (int)applyEffectWav:(id<AVSAudioEffectProgressDelegate>)delegate inFile: (NSString *)inWavFileName outFile: (NSString *)outWavFileName effect: (AVSAudioEffectType) effect nr_flag:(bool)reduce_noise;
{
    const char *in_file_name = [inWavFileName UTF8String];
    const char *out_file_name = [outWavFileName UTF8String];
  
    self.delegate = delegate;
    
    enum audio_effect effect_type = AUDIO_EFFECT_CHORUS_MIN;
    if (effect == AVSAudioEffectTypeChorusMin) {
        effect_type = AUDIO_EFFECT_CHORUS_MIN;
    } else if(effect == AVSAudioEffectTypeChorusMed){
        effect_type = AUDIO_EFFECT_CHORUS_MED;
    } else if(effect == AVSAudioEffectTypeChorusMax){
        effect_type = AUDIO_EFFECT_CHORUS_MAX;
    }else if(effect == AVSAudioEffectTypeReverbMin){
        effect_type = AUDIO_EFFECT_REVERB_MIN;
    }else if(effect == AVSAudioEffectTypeReverbMed){
        effect_type = AUDIO_EFFECT_REVERB_MID;
    }else if(effect == AVSAudioEffectTypeReverbMax){
        effect_type = AUDIO_EFFECT_REVERB_MAX;
    }else if(effect == AVSAudioEffectTypePitchupMin){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePitchupMed){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePitchupMax){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypePitchupInsane){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE;
    }else if(effect == AVSAudioEffectTypePitchdownMin){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePitchdownMed){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePitchdownMax){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypePitchdownInsane){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE;
    }else if(effect == AVSAudioEffectTypePaceupMin){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePaceupMed){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePaceupMax){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypePacedownMin){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePacedownMed){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePacedownMax){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypeReverse){
        effect_type = AUDIO_EFFECT_REVERSE;
    }else if(effect == AVSAudioEffectTypeVocoderMin){
        effect_type = AUDIO_EFFECT_VOCODER_MIN;
    }else if(effect == AVSAudioEffectTypeVocoderMed){
        effect_type = AUDIO_EFFECT_VOCODER_MED;
    }else if(effect == AVSAudioEffectTypeAutoTuneMin){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MIN;
    }else if(effect == AVSAudioEffectTypeAutoTuneMed){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MED;
    }else if(effect == AVSAudioEffectTypeAutoTuneMax){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MAX;
    }else if(effect == AVSAudioEffectTypePitchUpDownMin){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MIN;
    }else if(effect == AVSAudioEffectTypePitchUpDownMed){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MED;
    }else if(effect == AVSAudioEffectTypePitchUpDownMax){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MAX;
    }else if(effect == AVSAudioEffectTypeNone){
        effect_type = AUDIO_EFFECT_NONE;
    }
    
    int ret = apply_effect_to_wav(in_file_name, out_file_name, effect_type, reduce_noise, progress_status_h, (__bridge void *)(self));
    
    return ret;
}

@end