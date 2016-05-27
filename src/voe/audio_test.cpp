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

#define FS 16000
#define L  320 /* 20 ms */
#define THRES 100.0
#define STARTUP_SAMPLES 1*FS
#define NUM_BINS 100
#define POW_Q 2

void voe_init_audio_test(struct audio_test_state *autest)
{
    memset(autest, 0, sizeof(struct audio_test_state));
    autest->is_running = false;
    autest->test_score = -1;
}

void voe_start_audio_test(struct voe *voe)
{
#if TARGET_OS_IPHONE
	std::string prefix = "/Ios_";
#elif defined(ANDROID)
	std::string prefix = "/";
#else
	std::string prefix = "Osx_";
#endif
    char buf[30];
    sprintf(buf,"%p",&voe);
	voe->autest.file_in.clear();
	voe->autest.file_in.insert(0, voe->path_to_files);
	voe->autest.file_in.insert(voe->autest.file_in.size(), prefix);
    voe->autest.file_in.insert(voe->autest.file_in.size(),"sine");
    voe->autest.file_in.insert(voe->autest.file_in.size(),buf);
    voe->autest.file_in.insert(voe->autest.file_in.size(),".pcm");
	voe->autest.file_out.clear();
	voe->autest.file_out.insert(0, voe->path_to_files);
	voe->autest.file_out.insert(voe->autest.file_out.size(), prefix);
    voe->autest.file_out.insert(voe->autest.file_out.size(),"out");
    voe->autest.file_out.insert(voe->autest.file_out.size(),buf);
    voe->autest.file_out.insert(voe->autest.file_out.size(),".pcm");
	//voe->autest.file_out.insert(voe->autest.file_out.size(),"out.pcm");

	FILE *fid = fopen(voe->autest.file_in.c_str(),"wb");
	if (!fid) {
		warning("voe: could not open file '%s' (%m)\n",
			voe->autest.file_in.c_str(), errno);
		return;
	}

	int fs = 16000;
	int fsine = 400;
	int N = fs/fsine;
	int16_t tmp;
	float omega = 0;
	float delta_omega = (2*3.14f*fsine)/fs;
	for( int i = 0; i < 10*N; i++){
		tmp = (int16_t)(sinf(omega) * 8000.0f);
		omega += delta_omega;
		fwrite(&tmp, 1, sizeof(int16_t), fid);
	}
	fclose(fid);
    
	voe_start_playing_PCM_file_as_microphone(voe->autest.file_in.c_str(), fs);
	voe_start_recording_playout_PCM_file(voe->autest.file_out.c_str());
    
    voe->autest.is_running = true;
}

void voe_stop_audio_test(struct voe *voe)
{
    voe_stop_playing_PCM_file_as_microphone();
    voe_stop_recording_playout_PCM_file();
    
    if(!voe->autest.is_running){
        voe->autest.test_score = -1;
        return;
    }
    voe->autest.is_running = false;
    
    FILE *in_file;
    int num_samples, tot_samples = 0, n_low_pow, n = 0;
    short buf[ L ];
    float energy, pow, powdB, maxdsdB;
    bool startup = true;
    short ds, maxds, prev_s;
    int32_t powHistogram[ NUM_BINS * POW_Q ], dsHistogram[ NUM_BINS ];
    
    /* Clear Histograms */
    memset( powHistogram, 0, sizeof(powHistogram));
    memset( dsHistogram, 0, sizeof(dsHistogram));
    
    in_file = fopen(voe->autest.file_out.c_str(),"rb");
    
    if( in_file == NULL ){
        error("Error cannot open file \n");
        voe->autest.test_score = -1;
        return;
    }
    
    prev_s = 0;
    while(1) {
        num_samples = fread(buf, sizeof(short), L, in_file);
        tot_samples += num_samples;
        if(num_samples < L){
            break;
        }
        energy = 1.0;
        maxds = 1;
        for( int i = 0; i < L; i++ ){
            energy += ((float)buf[i] * (float)buf[i]);
            ds = abs(buf[i] - prev_s);
            if( ds > maxds ){
                maxds = ds;
            }
            prev_s = buf[i];
        }
        pow = energy / L;
        if( startup ){
            if( pow > THRES && tot_samples > STARTUP_SAMPLES ){
                startup = false;
            }
        } else {
            powdB = 10.0*log10(pow);
            powdB = fmax( powdB, 0.0);
            
            powHistogram[(int)((float)POW_Q*powdB + 0.5)]++;
            
            maxdsdB = 20.0*log10((float)maxds);
            maxdsdB = fmax( maxdsdB, 0.0);
            dsHistogram[(int)(maxdsdB + 0.5)]++;
            
            n++;
        }
    }
    fclose(in_file);
    if(n < 1){
        voe->autest.test_score = -1;
        int ret = remove(voe->autest.file_in.c_str());
        if(ret){
            error("cannot remove %s \n", voe->autest.file_in.c_str());
        }
        ret = remove(voe->autest.file_out.c_str());
        if(ret){
            error("cannot remove %s \n", voe->autest.file_out.c_str());
        }
        return;
    }
    
#if 0 // Dump Histograms
    FILE *powHistF;
    powHistF = fopen("powerHist.txt","wt");
    for( int n = 1 ; n < POW_Q*NUM_BINS; n++){
        fprintf(powHistF,"%d ", powHistogram[n]);
    }
    fclose(powHistF);
    FILE *dsHistF;
    dsHistF = fopen("dsHist.txt","wt");
    for( int n = 1 ; n < NUM_BINS; n++){
        fprintf(dsHistF,"%d ", dsHistogram[n]);
    }
    fclose(dsHistF);
#endif
    
    /* Calculate Median Power */
    int16_t medianPow = 0;
    int32_t maxCount = 0;
    for( int j = 0; j < NUM_BINS*POW_Q; j++ ){
        if( powHistogram[j] > maxCount ){
            maxCount = powHistogram[j];
            medianPow = j;
        }
    }
    
    /* Count number of frames with power below median */
    int16_t count = 0;
    for( int j = 0; j < (medianPow - (POW_Q-1)); j++ ){
        count += powHistogram[j];
    }
    
    /* Calculate Median delta sample value */
    int16_t medianDS = 0;
    maxCount = 0;
    for( int j = 0; j < NUM_BINS; j++ ){
        if( dsHistogram[j] > maxCount ){
            maxCount = dsHistogram[j];
            medianDS = j;
        }
    }
    
    /* Count number of frames with delta sample above median */
    int16_t glitchCount = 0;
    for( int j = medianDS + 2; j < NUM_BINS; j++ ){
        glitchCount += dsHistogram[j];
    }
    
    info("----------------------------------------------\n");
    info("Input File: %s                                \n", voe->autest.file_out.c_str());
    info("Calaulating power in %.0f ms frames \n", (float)L*1000.0/(float)FS);
    info("Median Power                       = %.1f dB \n", (float)medianPow/(float)POW_Q);
    info("Frames with less power than median = %.1f %% \n", 100.0*(float)count/(float)n);
    info("Frames with with glitched detected = %.1f %% \n", 100.0*(float)glitchCount/(float)n);
    
    voe->autest.test_score = 10 - (((((30*count << 4))/n) + (1 << 3)) >> 4);
    voe->autest.test_score = std::max(voe->autest.test_score,0);
    
    info("Audio test score = %d (0-10 where 10 is best) \n", voe->autest.test_score);
    int ret = remove(voe->autest.file_in.c_str());
    if(ret){
        error("cannot remove %s \n", voe->autest.file_in.c_str());
    }
    ret = remove(voe->autest.file_out.c_str());
    if(ret){
        error("cannot remove %s \n", voe->autest.file_out.c_str());
    }
}
