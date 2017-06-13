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
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cmath>
#include <stdint.h>

#include <re.h>
extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
}
#include "voe.h"

#define THRES 100.0
#define STARTUP_LEN_SEC 1
#define NUM_BINS 100
#define POW_Q 2

class VoEAudioOutputAnalyzer : public webrtc::VoEMediaProcess {
public:
    VoEAudioOutputAnalyzer() {
        _n = 0;
        _tot_samples = 0;
        _startup = true;
        _prev_s = 0;;
        
        /* Clear Histograms */
        memset( _powHistogram, 0, sizeof(_powHistogram));
        memset( _dsHistogram, 0, sizeof(_dsHistogram));
    }
    virtual ~VoEAudioOutputAnalyzer() {
    }
    virtual void Process(int channel,
                         webrtc::ProcessingTypes type,
                         int16_t audio10ms[],
                         size_t length,
                         int samplingFreq,
                         bool isStereo)
    {
        float energy, pow, powdB, maxdsdB;
        short ds, maxds;
        energy = 1.0;
        maxds = 1;
        for( int i = 0; i < length; i++ ){
            energy += ((float)audio10ms[i] * (float)audio10ms[i]);
            ds = abs(audio10ms[i] - _prev_s);
            if( ds > maxds ){
                maxds = ds;
            }
            _prev_s = audio10ms[i];
        }
        _tot_samples += length;
        pow = energy / length;
        if( _startup ){
            if( pow > THRES && _tot_samples > STARTUP_LEN_SEC*samplingFreq ){
                _startup = false;
            }
        } else {
            powdB = 10.0*log10(pow);
            powdB = fmax( powdB, 0.0);
            
            _powHistogram[(int)((float)POW_Q*powdB + 0.5)]++;
            
            maxdsdB = 20.0*log10((float)maxds);
            maxdsdB = fmax( maxdsdB, 0.0);
            _dsHistogram[(int)(maxdsdB + 0.5)]++;
            
            _n++;
        }
    }
    
    int GetScore()
    {
        if(_n <= 0){
            return -1;
        }
        /* Calculate Median Power */
        int16_t medianPow = 0;
        int32_t maxCount = 0;
        for( int j = 0; j < NUM_BINS*POW_Q; j++ ){
            if( _powHistogram[j] > maxCount ){
                maxCount = _powHistogram[j];
                medianPow = j;
            }
        }
        
        /* Count number of frames with power below median */
        int16_t count = 0;
        for( int j = 0; j < (medianPow - (POW_Q-1)); j++ ){
            count += _powHistogram[j];
        }
    
        /* Calculate Median delta sample value */
        int16_t medianDS = 0;
        maxCount = 0;
        for( int j = 0; j < NUM_BINS; j++ ){
            if( _dsHistogram[j] > maxCount ){
                maxCount = _dsHistogram[j];
                medianDS = j;
            }
        }
    
        /* Count number of frames with delta sample above median */
        int16_t glitchCount = 0;
        for( int j = medianDS + 2; j < NUM_BINS; j++ ){
            glitchCount += _dsHistogram[j];
        }
    
        info("----------------------------------------------\n");
        info("Median Power                       = %.1f dB \n", (float)medianPow/(float)POW_Q);
        info("Frames with less power than median = %.1f %% \n", 100.0*(float)count/(float)_n);
        info("Frames with with glitched detected = %.1f %% \n", 100.0*(float)glitchCount/(float)_n);
    
        int score = 10 - (((((30*count << 4))/_n) + (1 << 3)) >> 4);
        score = std::max(score,0);
    
        info("Audio test score = %d (0-10 where 10 is best) \n", score);
        
        return score;
    }
    
private:
    int _n;
    int _tot_samples;
    bool _startup;
    short _prev_s;
    int32_t _powHistogram[ NUM_BINS * POW_Q ];
    int32_t _dsHistogram[ NUM_BINS ];
};

void voe_init_audio_test(struct audio_test_state *autest)
{
    memset(autest, 0, sizeof(struct audio_test_state));
    autest->is_running = false;
    autest->test_score = -1;
}

void voe_start_audio_test(struct voe *voe)
{
    voe->autest.output_analyzer = new VoEAudioOutputAnalyzer();
    if(voe->autest.output_analyzer){
        gvoe.external_media->RegisterExternalMediaProcessing(-1,
                            webrtc::kPlaybackAllChannelsMixed, *voe->autest.output_analyzer);
    }
    
    voe->autest.is_running = true;
}

void voe_stop_audio_test(struct voe *voe)
{
    if(voe->autest.aio){
        voe->autest.aio = (struct audio_io *)mem_deref(voe->autest.aio);
    }
    
    if(!voe->autest.is_running){
        voe->autest.test_score = -1;
        return;
    }
    voe->autest.is_running = false;

    if(voe->autest.output_analyzer){
        gvoe.external_media->DeRegisterExternalMediaProcessing(-1, webrtc::kPlaybackAllChannelsMixed);
    
        voe->autest.test_score = voe->autest.output_analyzer->GetScore();
    
        delete voe->autest.output_analyzer;
    }
}
