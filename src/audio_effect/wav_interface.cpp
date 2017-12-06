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

#include <re.h>
#include "avs_audio_effect.h"

#include "webrtc/common_audio/resampler/include/push_resampler.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include "webrtc/modules/include/module_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

#define LOG2_CIRC_BUF_SZ 14
#define CIRC_BUF_MASK ((1 << LOG2_CIRC_BUF_SZ) -1)

struct wav_format {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    int32_t num_samples_in;
    int32_t num_samples_out;
    int32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

static int wav_format_debug(struct re_printf *pf, void *arg)
{
	struct wav_format *fmt = (struct wav_format *)arg;
	int err;

	err = re_hprintf(pf, "WAV_FORMAT: audio=0x%02x ch=%d samp=%u "
			 "samp_in=%d samp_out=%d byterate=%d "
			 "block_align=%d bits=%d",
			 fmt->audio_format,
			 (int)fmt->num_channels,
			 fmt->sample_rate,
			 fmt->num_samples_in,
			 fmt->num_samples_out,
			 fmt->byte_rate,
			 (int)fmt->block_align,
			 (int)fmt->bits_per_sample);
	
	return 0;
}


static int wav_converter_init(FILE *in_file, FILE *out_file, struct wav_format *format, int length_modification_q10)
{
    char chunkID[5] = "";
    if(fread(chunkID, sizeof(chunkID)-1, 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    chunkID[4] = '\0';
    if(strcmp(chunkID,"RIFF")!=0){
        error("audio_effect: chumkID = %s expected RIFF \n", chunkID);
        return -1;
    }

    if(fwrite(chunkID, sizeof(chunkID)-1, 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    uint32_t ChunkSize;
    if(fread(&ChunkSize, sizeof(uint32_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    };

    if(fwrite(&ChunkSize, sizeof(uint32_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    };
    char Format[5] = "";
    if(fread(Format, sizeof(Format)-1, 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    Format[4] = '\0';
    if(strcmp(Format,"WAVE")!=0){
        error("audio_effect: Format = %s expected WAVE \n", Format);
        return -1;
    }

    if(fwrite(Format, sizeof(Format)-1, 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }


    /* Search for fmt chunk */
    for(;;) {
	    char Subchunk1ID[5] = "";
	    if(fread(Subchunk1ID, sizeof(Subchunk1ID)-1, 1, in_file) != 1){
		    error("audio_effect: Cannot read file \n");
		    return -1;
	    };
	    Subchunk1ID[4] = '\0';
    
	    if(fwrite(Subchunk1ID, sizeof(Subchunk1ID)-1, 1, out_file) != 1){
		    error("audio_effect: Cannot write file \n");
		    return -1;
	    }
	    uint32_t Subchunk1Size;
	    if(fread(&Subchunk1Size, sizeof(uint32_t), 1, in_file) != 1){
		    error("audio_effect: Cannot read file \n");
		    return -1;
	    }

	    if(fwrite(&Subchunk1Size, sizeof(uint32_t), 1, out_file) != 1){    
		    error("audio_effect: Cannot write file \n");
		    return -1;
	    }

	    if (strcmp(Subchunk1ID, "fmt ") == 0) {
		    break;
	    }
	    for(uint32_t i = 0; i < Subchunk1Size; ++i) {
		    char tmp;
		    fread(&tmp, sizeof(char), 1, in_file);
		    fwrite(&tmp, sizeof(char), 1, out_file);
	    }
    }

    
    if(fread(&format->audio_format, sizeof(uint16_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }

    if(fwrite(&format->audio_format, sizeof(uint16_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    if(fread(&format->num_channels, sizeof(uint16_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    if(fwrite(&format->num_channels, sizeof(uint16_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    if(fread(&format->sample_rate, sizeof(uint32_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    if(fwrite(&format->sample_rate, sizeof(uint32_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    if(fread(&format->byte_rate, sizeof(uint32_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    if(fwrite(&format->byte_rate, sizeof(uint32_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    if(fread(&format->block_align, sizeof(uint16_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    if(fwrite(&format->block_align, sizeof(uint16_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    if(fread(&format->bits_per_sample, sizeof(uint16_t), 1, in_file) != 1){
        error("audio_effect: Cannot read file \n");
        return -1;
    }
    if(fwrite(&format->bits_per_sample, sizeof(uint16_t), 1, out_file) != 1){
        error("audio_effect: Cannot write file \n");
        return -1;
    }
    
    char Subchunk2ID[5] = "";
    uint32_t Subchunk2Size;
    format->num_samples_in = 0;
    format->num_samples_out = 0;
    while(1){
        if(fread(Subchunk2ID, sizeof(Subchunk2ID)-1, 1, in_file) != 1){
            error("audio_effect: Cannot read file \n");
            return -1;
        }
        if(fwrite(Subchunk2ID, sizeof(Subchunk2ID)-1, 1, out_file) != 1){
            error("audio_effect: Cannot write file \n");
            return -1;
        }
        
        if(fread(&Subchunk2Size, sizeof(uint32_t), 1, in_file) != 1){
            error("audio_effect: Cannot read file \n");
            return -1;
        }

	Subchunk2ID[4] = '\0';
        if(strcmp(Subchunk2ID,"data")==0){
            format->num_samples_in = Subchunk2Size/format->block_align;
            int64_t tmp = (int64_t)format->num_samples_in * (int64_t)length_modification_q10;
            tmp = tmp >> 10;
            format->num_samples_out = tmp;
            uint32_t new_Subchunk2Size = format->num_samples_out * format->block_align;
            if(fwrite(&new_Subchunk2Size, sizeof(uint32_t), 1, out_file) != 1){
                error("audio_effect: Cannot write file \n");
                return -1;
            }
            break;
        } else {
            char tmp;
            if(fwrite(&Subchunk2Size, sizeof(uint32_t), 1, out_file) != 1){
                error("audio_effect: Cannot write file \n");
                return -1;
            }
            for(int i = 0; i < Subchunk2Size; i++){
                fread(&tmp, sizeof(char), 1, in_file);
                fwrite(&tmp, sizeof(char), 1, out_file);
            }
        }
    }
    return 0;
}

#define FS_PROC 32000

static void reverse_stream(FILE *in_file,
                      FILE *out_file,
                      struct wav_format *format)
{
    
    int L = format->sample_rate/100;
    int L_proc = FS_PROC/100;
    int N = format->num_samples_in/L;
    int progress = 0;
    int rem = format->num_samples_in - N*L;
    int16_t bufIn[L], bufOut[L];
    FILE *tmp_file = in_file;
    
    size_t count;
    fseek(in_file, format->num_samples_in*sizeof(int16_t), SEEK_CUR);
    for( int i = 0; i < N; i++){
        fseek(in_file, -L*sizeof(int16_t), SEEK_CUR);
        
        count = fread(bufIn, sizeof(int16_t), L, in_file);

        for( int j = 0; j < L; j++){
            bufOut[j] = bufIn[L - j - 1];
        }
        
        count = fwrite(bufOut, sizeof(int16_t), L, out_file);
        
        fseek(in_file, -L*sizeof(int16_t), SEEK_CUR);
    }
    
    memset(bufIn, 0, rem*sizeof(int16_t));

    count = fwrite(bufIn, sizeof(int16_t), rem, out_file);
    
    for( int i = 0; i < N; i++){
        count = fread(bufIn, sizeof(int16_t), L, tmp_file);        
        count = fwrite(bufIn, sizeof(int16_t), L, out_file);        
    }
    
    memset(bufIn, 0, rem*sizeof(int16_t));
    
    count = fwrite(bufIn, sizeof(int16_t), rem, out_file);
}

int apply_effect_to_wav(const char* wavIn,
                        const char* wavOut,
                        enum audio_effect effect_type,
                        bool reduce_noise,
                        effect_progress_h* progress_h,
                        void *arg)
{
    FILE *in_file, *out_file;
    in_file = fopen(wavIn,"rb");
    if( in_file == NULL ){
	    error("Could not open file for reading \n");
        return -1;
    }
    out_file = fopen(wavOut,"wb");
    if( out_file == NULL ){
        error("Could not open file for writing \n");
        fclose(in_file);
        return -1;
    }
    
    webrtc::PushResampler<int16_t> input_resampler;
    webrtc::PushResampler<int16_t> output_resampler;
    std::unique_ptr<webrtc::AudioProcessing> apm(webrtc::AudioProcessing::Create());
    
    struct aueffect *aue;
    int ret = aueffect_alloc(&aue, effect_type, FS_PROC);
    if(ret != 0){
        error("aueffect_alloc failed \n");
        fclose(in_file);
        fclose(out_file);
        return ret;
    }
    
    int length_modification_q10 = 1024;
    aueffect_length_modification(aue, &length_modification_q10);
    
    if(effect_type == AUDIO_EFFECT_REVERSE){
        length_modification_q10 = 1024 * 2; // We append the original
    }
    
    struct wav_format format;
    ret = wav_converter_init(in_file, out_file, &format, length_modification_q10);
    if(ret != 0){
        fclose(in_file);
        fclose(out_file);
        return ret;
    }

    info("wav: %s -> %H\n", wavIn, wav_format_debug, &format);
    
    if(effect_type == AUDIO_EFFECT_REVERSE){
        /* Special handling for reverse effect */
        reverse_stream(in_file, out_file, &format);

        if(progress_h){
            progress_h(100, arg);
        }
        
        mem_deref(aue);
        
        fclose(in_file);
        fclose(out_file);
        
        return 0;
    }
    
    int L = format.sample_rate/100;
    int L_proc = FS_PROC/100;
    int N = format.num_samples_in/L;
    int progress = 0;
    int rem = 0;
    int n_samp_out = 0;
    
    input_resampler.InitializeIfNeeded(format.sample_rate, FS_PROC, 1);
    output_resampler.InitializeIfNeeded(FS_PROC, format.sample_rate, 1);
    
    // Setup Audio Buffer used by apm
    webrtc::AudioFrame near_frame;
    near_frame.samples_per_channel_ = L_proc;
    near_frame.num_channels_ = 1;
    near_frame.sample_rate_hz_ = FS_PROC;
    
    // Setup APM
    webrtc::AudioProcessing::ChannelLayout inLayout = webrtc::AudioProcessing::kMono;
    webrtc::AudioProcessing::ChannelLayout outLayout = webrtc::AudioProcessing::kMono;;
    webrtc::AudioProcessing::ChannelLayout reverseLayout = webrtc::AudioProcessing::kMono;;
    apm->Initialize( FS_PROC, FS_PROC, FS_PROC, inLayout, outLayout, reverseLayout );
    
    // Enable High Pass Filter
    apm->high_pass_filter()->Enable(true);
    
    // Enable Noise Supression
    if(reduce_noise){
        apm->noise_suppression()->Enable(true);
        if(effect_type == AUDIO_EFFECT_VOCODER_MED){
            apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kModerate);
        } else {
            apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kLow);
        }
    }
        
    int16_t circ_buf[(1 << LOG2_CIRC_BUF_SZ)];
    int write_idx = 0;
    int read_idx = 0;
    
    int16_t bufIn[L];
    int16_t procIn[L_proc], procOut[L_proc];
    size_t count;
    for(int i = 0; i < N; i++){
        count = fread(bufIn,
                      sizeof(int16_t),
                      L,
                      in_file);
        
        if(count < L){
            break;
        }
        
        if((i % 100) == 0){
            int progress = (i*100)/N;
            if(progress_h){
                progress_h(progress, arg);
            }
        }
        
        input_resampler.Resample( bufIn, L, near_frame.data_, L_proc);
        
        ret = apm->ProcessStream(&near_frame);
        if( ret < 0 ){
            error("apm->ProcessStream returned %d \n", ret);
        }
        
        size_t L_proc_out;
        aueffect_process(aue, near_frame.data_, procOut, L_proc, &L_proc_out);
        
        for(int j = 0; j < L_proc_out; j++){
            circ_buf[write_idx] = procOut[j];
            write_idx = (write_idx + 1) & CIRC_BUF_MASK;
        }
        // resampler needs 10 ms chunks
        int buf_smpls = (write_idx - read_idx) & CIRC_BUF_MASK;
        while(buf_smpls >= L_proc){
            for(int j = 0; j < L_proc; j++){
                procOut[j] = circ_buf[read_idx];
                read_idx = (read_idx + 1) & CIRC_BUF_MASK;
            }
            output_resampler.Resample( procOut, L_proc, bufIn, L);
            
            count = fwrite(bufIn,
                           sizeof(int16_t),
                           L,
                           out_file);
            n_samp_out+=L;
            
            buf_smpls = (write_idx - read_idx) & CIRC_BUF_MASK;
        }
        rem = format.num_samples_out - n_samp_out;
        if(rem < L*2){
            break;
        }
    }
    
    rem = format.num_samples_out - n_samp_out;
        
    if(rem > 0){
        int16_t fillbuf[rem];
        memset(fillbuf, 0, sizeof(fillbuf));
        count = fwrite(fillbuf,
                       sizeof(int16_t),
                       rem,
                       out_file);
    }
    
    if(progress_h){
        progress_h(100, arg);
    }
    
    mem_deref(aue);
    
    fclose(in_file);
    fclose(out_file);
    
    return 0;
}

