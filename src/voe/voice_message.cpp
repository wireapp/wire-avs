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

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <re.h>

#include "contrib/ogg/include/ogg/ogg.h"
#include "contrib/opus/include/opus.h"

#include "webrtc/common_types.h"
#include "webrtc/common.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"
#include "webrtc/voice_engine/include/voe_codec.h"
#include "webrtc/voice_engine/include/voe_file.h"
#include "webrtc/voice_engine/include/voe_audio_processing.h"
#include "webrtc/voice_engine/include/voe_volume_control.h"
#include "webrtc/voice_engine/include/voe_rtp_rtcp.h"
#include "webrtc/voice_engine/include/voe_neteq_stats.h"
#include "webrtc/voice_engine/include/voe_errors.h"
#include "webrtc/voice_engine/include/voe_hardware.h"
#include "voe_settings.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"

extern "C" {
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"
#include "avs_conf_pos.h"
}

#include "avs_audio_effect.h"

#include "voe.h"

#define RTP_HEADER_IN_BYTES 12
#define MAX_PACKET_SIZE_BYTES 512
#define PACKET_SIZE_MS 40

static int MakeRTPheader( uint8_t* rtpHeader,
                         const uint8_t payloadType,
                         const uint16_t seqNum,
                         const uint32_t timeStamp,
                         const uint32_t ssrc){
    
    rtpHeader[0] = (uint8_t) 0x80;
    rtpHeader[1] = (uint8_t) (payloadType & 0xFF);
    rtpHeader[2] = (uint8_t) ((seqNum >> 8) & 0xFF);
    rtpHeader[3] = (uint8_t) (seqNum & 0xFF);
    rtpHeader[4] = (uint8_t) ((timeStamp >> 24) & 0xFF);
    rtpHeader[5] = (uint8_t) ((timeStamp >> 16) & 0xFF);
    rtpHeader[6] = (uint8_t) ((timeStamp >> 8) & 0xFF);
    rtpHeader[7] = (uint8_t) (timeStamp & 0xFF);
    rtpHeader[8] = (uint8_t) ((ssrc >> 24) & 0xFF);
    rtpHeader[9] = (uint8_t) ((ssrc >> 16) & 0xFF);
    rtpHeader[10] = (uint8_t) ((ssrc >> 8) & 0xFF);
    rtpHeader[11] = (uint8_t) (ssrc & 0xFF);
    
    return(RTP_HEADER_IN_BYTES);
}

/* From libopus, src/opus_decode.c */
static int packet_get_samples_per_frame(const unsigned char *data, ogg_int32_t Fs)
{
    int audiosize;
    if (data[0]&0x80)
    {
        audiosize = ((data[0]>>3)&0x3);
        audiosize = (Fs<<audiosize)/400;
    } else if ((data[0]&0x60) == 0x60)
    {
        audiosize = (data[0]&0x08) ? Fs/50 : Fs/100;
    } else {
        audiosize = ((data[0]>>3)&0x3);
        if (audiosize == 3)
        audiosize = Fs*60/1000;
        else
        audiosize = (Fs<<audiosize)/100;
    }
    return audiosize;
}

/* From libopus, src/opus_decode.c */
static int packet_get_nb_frames(const unsigned char packet[], ogg_int32_t len)
{
    int count;
    if (len<1)
    return -1;
    count = packet[0]&0x3;
    if (count==0)
    return 1;
    else if (count!=3)
    return 2;
    else if (len<2)
    return -4;
    else
    return packet[1]&0x3F;
}

/* Header contents:
 - "OpusHead" (64 bits)
 - version number (8 bits)
 - Channels C (8 bits)
 - Pre-skip (16 bits)
 - Sampling rate (32 bits)
 - Gain in dB (16 bits, S7.8)
 - Mapping (8 bits, 0=single stream (mono/stereo) 1=Vorbis mapping,
 2..254: reserved, 255: multistream with no mapping)
 
 - if (mapping != 0)
 - N = totel number of streams (8 bits)
 - M = number of paired streams (8 bits)
 - C times channel origin
 - if (C<2*M)
 - stream = byte/2
 - if (byte&0x1 == 0)
 - left
 else
 - right
 - else
 - stream = byte-M
 */

typedef struct {
    int version;
    int channels; /* Number of channels: 1..255 */
    int preskip;
    ogg_uint32_t input_sample_rate;
    int gain; /* in dB S7.8 should be zero whenever possible */
    int channel_mapping;
    /* The rest is only used if channel_mapping != 0 */
    int nb_streams;
    int nb_coupled;
    unsigned char stream_map[255];
} OpusHeader;

typedef struct {
    unsigned char *data;
    int maxlen;
    int pos;
} Packet;

typedef struct {
    const unsigned char *data;
    int maxlen;
    int pos;
} ROPacket;

static int write_uint32(Packet *p, ogg_uint32_t val)
{
    if (p->pos>p->maxlen-4)
    return 0;
    p->data[p->pos  ] = (val    ) & 0xFF;
    p->data[p->pos+1] = (val>> 8) & 0xFF;
    p->data[p->pos+2] = (val>>16) & 0xFF;
    p->data[p->pos+3] = (val>>24) & 0xFF;
    p->pos += 4;
    return 1;
}

static int write_uint16(Packet *p, ogg_uint16_t val)
{
    if (p->pos>p->maxlen-2)
    return 0;
    p->data[p->pos  ] = (val    ) & 0xFF;
    p->data[p->pos+1] = (val>> 8) & 0xFF;
    p->pos += 2;
    return 1;
}

static int write_chars(Packet *p, const unsigned char *str, int nb_chars)
{
    int i;
    if (p->pos>p->maxlen-nb_chars)
    return 0;
    for (i=0;i<nb_chars;i++)
    p->data[p->pos++] = str[i];
    return 1;
}

static int read_uint32(ROPacket *p, ogg_uint32_t *val)
{
    if (p->pos>p->maxlen-4)
    return 0;
    *val =  (ogg_uint32_t)p->data[p->pos  ];
    *val |= (ogg_uint32_t)p->data[p->pos+1]<< 8;
    *val |= (ogg_uint32_t)p->data[p->pos+2]<<16;
    *val |= (ogg_uint32_t)p->data[p->pos+3]<<24;
    p->pos += 4;
    return 1;
}

static int read_uint16(ROPacket *p, ogg_uint16_t *val)
{
    if (p->pos>p->maxlen-2)
    return 0;
    *val =  (ogg_uint16_t)p->data[p->pos  ];
    *val |= (ogg_uint16_t)p->data[p->pos+1]<<8;
    p->pos += 2;
    return 1;
}

static int read_chars(ROPacket *p, unsigned char *str, int nb_chars)
{
    int i;
    if (p->pos>p->maxlen-nb_chars)
    return 0;
    for (i=0;i<nb_chars;i++)
    str[i] = p->data[p->pos++];
    return 1;
}

int opus_header_parse(const unsigned char *packet, int len, OpusHeader *h)
{
    int i;
    char str[9];
    ROPacket p;
    unsigned char ch;
    ogg_uint16_t shortval;
    
    p.data = packet;
    p.maxlen = len;
    p.pos = 0;
    str[8] = 0;
    if (len<19)return 0;
    read_chars(&p, (unsigned char*)str, 8);
    if (memcmp(str, "OpusHead", 8)!=0)
    return 0;
    
    if (!read_chars(&p, &ch, 1))
    return 0;
    h->version = ch;
    if((h->version&240) != 0) /* Only major version 0 supported. */
    return 0;
    
    if (!read_chars(&p, &ch, 1))
    return 0;
    h->channels = ch;
    if (h->channels == 0)
    return 0;
    
    if (!read_uint16(&p, &shortval))
    return 0;
    h->preskip = shortval;
    
    if (!read_uint32(&p, &h->input_sample_rate))
    return 0;
    
    if (!read_uint16(&p, &shortval))
    return 0;
    h->gain = (short)shortval;
    
    if (!read_chars(&p, &ch, 1))
    return 0;
    h->channel_mapping = ch;
    
    if (h->channel_mapping != 0)
    return 0;
    else {
        if(h->channels>2)
        return 0;
        h->nb_streams = 1;
        h->nb_coupled = h->channels>1;
        h->stream_map[0]=0;
        h->stream_map[1]=1;
    }
    /*For version 0/1 we know there won't be any more data
     so reject any that have data past the end.*/
    if ((h->version==0 || h->version==1) && p.pos != len)
    return 0;
    return 1;
}

int opus_header_to_packet(const OpusHeader *h, unsigned char *packet, int len)
{
    int i;
    Packet p;
    unsigned char ch;
    
    p.data = packet;
    p.maxlen = len;
    p.pos = 0;
    if (len<19)return 0;
    if (!write_chars(&p, (const unsigned char*)"OpusHead", 8))
    return 0;
    /* Version is 1 */
    ch = 1;
    if (!write_chars(&p, &ch, 1))
    return 0;
    
    ch = h->channels;
    if (!write_chars(&p, &ch, 1))
    return 0;
    
    if (!write_uint16(&p, h->preskip))
    return 0;
    
    if (!write_uint32(&p, h->input_sample_rate))
    return 0;
    
    if (!write_uint16(&p, h->gain))
    return 0;
    
    ch = h->channel_mapping;
    if (!write_chars(&p, &ch, 1))
    return 0;
    
    if (h->channel_mapping != 0)
    return 0;
    
    return p.pos;
}

/*Write an Ogg page to a file pointer*/
static inline int oe_write_page(ogg_page *page, FILE *fp)
{
    int written;
    written=fwrite(page->header,1,page->header_len, fp);
    written+=fwrite(page->body,1,page->body_len, fp);
    return written;
}

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
((buf[base+2]<<16)&0xff0000)| \
((buf[base+1]<<8)&0xff00)| \
(buf[base]&0xff))
#define writeint(buf, base, val) do{ buf[base+3]=((val)>>24)&0xff; \
buf[base+2]=((val)>>16)&0xff; \
buf[base+1]=((val)>>8)&0xff; \
buf[base]=(val)&0xff; \
}while(0)

static void comment_init(char **comments, int* length, const char *vendor_string)
{
    /*The 'vendor' field should be the actual encoding library used.*/
    int vendor_length=strlen(vendor_string);
    int user_comment_list_length=0;
    int len=8+4+vendor_length+4;
    char *p=(char*)malloc(len);
    if(p==NULL){
        info("Ogg malloc failed in comment_init()\n");
    }
    memcpy(p, "OpusTags", 8);
    writeint(p, 8, vendor_length);
    memcpy(p+12, vendor_string, vendor_length);
    writeint(p, 12+vendor_length, user_comment_list_length);
    *length=len;
    *comments=p;
}

void comment_add(char **comments, int* length, const char *tag, const char *val)
{
    char* p=*comments;
    int vendor_length=readint(p, 8);
    int user_comment_list_length=readint(p, 8+4+vendor_length);
    int tag_len=(tag?strlen(tag)+1:0);
    int val_len=strlen(val);
    int len=(*length)+4+tag_len+val_len;
    
    p=(char*)realloc(p, len);
    if(p==NULL){
        info("Ogg realloc failed in comment_add()\n");
    }
    
    writeint(p, *length, tag_len+val_len);      /* length of comment */
    if(tag){
        memcpy(p+*length+4, tag, tag_len);        /* comment tag */
        (p+*length+4)[tag_len-1] = '=';           /* separator */
    }
    memcpy(p+*length+4+tag_len, val, val_len);  /* comment */
    writeint(p, 8+4+vendor_length, user_comment_list_length+1);
    *comments=p;
    *length=len;
}

static void comment_pad(char **comments, int* length, int amount)
{
    if(amount>0){
        int i;
        int newlen;
        char* p=*comments;
        /*Make sure there is at least amount worth of padding free, and
         round up to the maximum that fits in the current ogg segments.*/
        newlen=(*length+amount+255)/255*255-1;
        p=(char *)realloc(p,newlen);
        if(p==NULL){
            info("Ogg realloc failed in comment_pad()\n");
        }
        for(i=*length;i<newlen;i++)p[i]=0;
        *comments=p;
        *length=newlen;
    }
}

static void init_ogg_stream(ogg_packet* op, ogg_stream_state* os, FILE **fpp)
{
    /* Initialize Ogg stream struct */
    int serialno = 12345;
    if(ogg_stream_init(os, serialno)==-1){
        info("Ogg stream init failed\n");
    }
    
    /* Write headers */
    int ret;
    ogg_page og;
    OpusHeader header;
    header.version = 0;
    header.channels = 2;
    header.preskip = 120;
    header.input_sample_rate = 48000;
    header.gain = 0;
    header.channel_mapping = 0;
    unsigned char header_data[100];
    op->bytes  = opus_header_to_packet(&header, header_data, 100);
    op->packet = header_data;
    op->b_o_s = 1;
    op->e_o_s = 0;
    op->granulepos = 0;
    op->packetno = 0;
    ogg_stream_packetin(os, op);
    while((ret=ogg_stream_flush(os, &og))){
        if(!ret)break;
        ret=oe_write_page(&og, *fpp);
        if(ret!=og.header_len+og.body_len){
            info("Ogg failed writing header to output stream\n");
        }
    }
    
    /* Vendor string should just be the encoder library, the ENCODER comment specifies the tool used.*/
    char *comments;
    int comments_length;
    comment_init(&comments, &comments_length, opus_get_version_string());
    const char *tag = "ENCODER";
    const char *vendor = "Wire";
    comment_add(&comments, &comments_length, tag, vendor);
    comment_pad(&comments, &comments_length, 0);
    op->packet = (unsigned char *)comments;
    op->bytes = comments_length;
    op->b_o_s = 0;
    op->e_o_s = 0;
    op->granulepos = 0;
    op->packetno = 1;
    ogg_stream_packetin(os, op);
    while((ret=ogg_stream_flush(os, &og))){
        if(!ret)break;
        ret=oe_write_page(&og, *fpp);
        if(ret!=og.header_len + og.body_len){
            info("Ogg failed writing header to output stream\n");
        }
    }
    free(comments);
    
    op->bytes = 0;
}

class VmTransport : public webrtc::Transport {
public:
    VmTransport(FILE **fpp, pthread_mutex_t* mutex){
        _fpp = fpp;
        _mutex = mutex;
        
        if(!fpp || !mutex)
        return;
     
        init_ogg_stream(&_op, &_os, fpp);
    };
    
    virtual ~VmTransport() {
    };
    
    virtual bool SendRtp(const uint8_t* packet, size_t length, const webrtc::PacketOptions& options) {
        //printf("SendPacket channel = %d len = %zu \n", channel, len);
        
        /* First write previous payload to file */
        write_to_opus_file();
        
        /* Then copy new payload. By not yet writing to file we can set the end-of-stream bit for the last packet */
        uint32_t len32 = length - RTP_HEADER_IN_BYTES; // use a type of known length
        if(len32 > 0 && len32 < sizeof(payload)) {
            memcpy(payload, packet + RTP_HEADER_IN_BYTES, len32);
            _op.bytes = len32;
        }
        
        return true;
    };

    virtual bool SendRtcp(const uint8_t* packet, size_t length) {
        return true;
    };
    
    void deregister()
    {
        _op.b_o_s=0;
        /* Set end-of-stream flag */
        _op.e_o_s=1;
        write_to_opus_file();
        ogg_stream_clear(&_os);
    }
    
private:
    void write_to_opus_file() {
        if(!_fpp || !_mutex)
        return;
        
        pthread_mutex_lock(_mutex); // use mutex here to make sure we dont close the file in another thread while writing
        if(*_fpp && _op.bytes){
            _op.packet = payload;
            _op.packetno++;
            _op.granulepos += PACKET_SIZE_MS * 48;
            ogg_stream_packetin(&_os, &_op);
            ogg_page og;
            ogg_stream_flush_fill(&_os, &og, 255*255);
            int ret=oe_write_page(&og, *_fpp);
            if(ret!=og.header_len+og.body_len){
                info("Ogg failed writing data to output stream\n");
            }
            _op.bytes = 0;
        }
        pthread_mutex_unlock(_mutex);
    }
    
    FILE** _fpp;
    // Add mutex pointer for mutex to protect _fpp
    pthread_mutex_t* _mutex;
    struct timeval _startTime;
    uint8_t payload[MAX_PACKET_SIZE_BYTES];
    ogg_packet _op;
    ogg_stream_state _os;
};

void voe_vm_init(struct vm_state *vm)
{
    vm->ch = -1;
    vm->transport = NULL;
    vm->fp = NULL;
    pthread_mutex_init(&vm->mutex,NULL);
    vm->play_statush = NULL;
    vm->play_statush_arg = NULL;
}

int voe_vm_start_record(const char fileNameUTF8[1024])
{
    int err = 0;
    
    if (gvoe.nch > 0) {
        error("voe_vm_start_record: cannot start when in a call \n");
        return -1;
    }
    if(gvoe.vm.fp){
        error("voe_vm_start_record: A file is allready open this is not supposed to happen \n");
        return -1;
    }
    gvoe.vm.fp = fopen(fileNameUTF8,"wb");
    if (gvoe.vm.fp == NULL) {
        error("voe_vm_start_record: Could not open file: %s\n", fileNameUTF8);
        return -1;
    }
    
    gvoe.base->Init();
    
    gvoe.vm.ch = gvoe.base->CreateChannel();
    if (gvoe.vm.ch == -1) {
        err = ENOMEM;
    }
    
    pthread_mutex_init(&gvoe.vm.mutex,NULL); // mutex to make sure we dont close the file while the callback is writing to it
    
    gvoe.vm.transport = new VmTransport(&gvoe.vm.fp, &gvoe.vm.mutex);
    gvoe.nw->RegisterExternalTransport(gvoe.vm.ch, *gvoe.vm.transport);
    
    webrtc::CodecInst c;
    int numberOfCodecs = gvoe.codec->NumOfCodecs();
    bool codec_found = false;
    for( int i = 0; i < numberOfCodecs; i++ ){
        gvoe.codec->GetCodec( i, c);
        if(strcmp(c.plname,"opus") == 0){
            codec_found = true;
            break;
        }
    }
    c.rate = 32000;
    c.channels = 1;
    c.pacsize = (c.plfreq * PACKET_SIZE_MS) / 1000;
    
    gvoe.codec->SetSendCodec(gvoe.vm.ch, c);
    
    /* SetUp HP filter */
    int ret = gvoe.processing->EnableHighPassFilter( ZETA_USE_HP );
    
    /* SetUp AGC */
    ret = gvoe.processing->SetAgcStatus( ZETA_USE_AGC_SPEAKER, ZETA_AGC_MODE_SPEAKER);
    
    /* Set AGC according to routing */
    voe_update_agc_settings(&gvoe);
    
    /* Setup Noise Supression */
    ret = gvoe.processing->SetNsStatus( ZETA_USE_NS, ZETA_NS_MODE_SPEAKER );
    
    gvoe.base->StartSend(gvoe.vm.ch);
    
    debug("voe_vm_start_record \n");
    
    return err ? ENOSYS : 0;
}

int voe_vm_stop_record()
{
    int err = 0;
    
    if(!gvoe.vm.fp){
        debug("voe_vm_stop_record(): allready stopped !! \n");
        return 0;
    }
    
    gvoe.base->StopSend(gvoe.vm.ch);
    gvoe.nw->DeRegisterExternalTransport(gvoe.vm.ch);
    gvoe.base->DeleteChannel(gvoe.vm.ch);
    gvoe.base->Terminate();
    gvoe.vm.transport->deregister();
    
    pthread_mutex_lock(&gvoe.vm.mutex);
    fclose(gvoe.vm.fp);
    gvoe.vm.fp = NULL;
    pthread_mutex_unlock(&gvoe.vm.mutex);
    
    debug("voe_vm_stop_record \n");
    
    return err;
}

int voe_me_init_stream(struct vm_state *vm, uint64_t target_pos) {
    FILE *fp;
    fp = vm->fp;
    if(!fp){
        info("File pointer is null: exit voe_me_init_stream \n");
        return -1;
    }
    rewind(fp);
    
    vm->stream_init = 0;
    ogg_sync_init(&vm->oy);
    
    /* Decode first two packets (=header and tags) */
    int packet_count = 0;
    int nb_read;
    ogg_packet op;
    do {
        char *data;
        int i, nb_read;
        /* Get the ogg buffer for writing */
        data = ogg_sync_buffer(&vm->oy, 200);
        /* Read bitstream from input file */
        nb_read = fread(data, sizeof(char), 200, fp);
        ogg_sync_wrote(&vm->oy, nb_read);
        /* Loop for all complete pages we got (most likely only one) */
        while (ogg_sync_pageout(&vm->oy, &vm->og)==1 && packet_count < 2)
        {
            if (vm->stream_init == 0) {
                ogg_stream_init(&vm->os, ogg_page_serialno(&vm->og));
                vm->stream_init = 1;
            }
            /* Add page to the bitstream */
            ogg_stream_pagein(&vm->os, &vm->og);
            /* Extract all available packets */
            while (ogg_stream_packetout(&vm->os, &op) == 1 && packet_count < 2)
            {
                /* If first packet in a logical stream, process the Opus header */
                if (packet_count==0)
                {
                    if(!op.b_o_s) {
                        info("First Ogg packet not beginning of stream\n");
                        return -1;
                    }
                    OpusHeader header;
                    if (opus_header_parse(op.packet, op.bytes, &header)==0) {
                        info("Invalid Ogg/Opus header\n");
                        return -1;
                    }
                }
                packet_count++;
            }
        }
    } while (packet_count < 2 && nb_read > 0);
    
    if (packet_count < 2 || op.e_o_s) {
        return -1;
    }
    
    /* At this point we're at the start of the codec bitstream */
    vm->samplepos = 0;
    
    if (target_pos == 0) {
        /* No seeking necessary */
        return 0;
    }
    
    /* Process stream until we reach the right position */
    nb_read = 1;
    while(nb_read) {
        /* Extract all available packets */
        while (ogg_stream_packetout(&vm->os, &op) == 1) {
            int nSamples = packet_get_samples_per_frame(op.packet, 48000) * packet_get_nb_frames(op.packet, op.bytes);
            gvoe.vm.samplepos += nSamples;
            if (op.e_o_s) {
                vm->samplestot = vm->samplepos;
                return 0;
            }
            if (vm->samplepos >= target_pos) {
                return 0;
            }
        }
        
        /* Extract page */
        if(ogg_sync_pageout(&vm->oy, &vm->og)==1)
        {
            /* Add page to the bitstream */
            ogg_stream_pagein(&vm->os, &vm->og);
        } else {
            /* Read more data */
            char *data;
            /*Get the ogg buffer for writing*/
            data = ogg_sync_buffer(&vm->oy, 1000);
            /* Read bitstream from input file */
            nb_read = fread(data, sizeof(char), 1000, fp);
            ogg_sync_wrote(&vm->oy, nb_read);
        }
    }
    vm->samplestot = vm->samplepos;
    return 0;
}

void tmr_vm_player_handler(void *arg)
{
    uint8_t RTPpacketBuf[MAX_PACKET_SIZE_BYTES];
    
    if (gvoe.vm.finished) {
        voe_vm_stop_play();
        return;
    }
    
    FILE *fp = gvoe.vm.fp;
    if(!fp){
        info("File pointer is null: stop tmr_vm_player_handler \n");
        return;
    }
    
    if(gvoe.vm.start_time_ms >= 0) {
        /* Rewind to start of file, and scan through Ogg stream until the desired start time */
        if (voe_me_init_stream(&gvoe.vm, gvoe.vm.start_time_ms * 48)){
            voe_vm_stop_play();
            return;
        }
        gvoe.vm.start_time_ms = -1;
    }
    
    /* Extract payload from Ogg stream */
    int nSamples = 0;
    ogg_packet op;
    int nb_read = 1;
    while (nb_read > 0) {
        /* Extract all available packets */
        if (ogg_stream_packetout(&gvoe.vm.os, &op) == 1)
        {
            /* Add an RTP header before pushing to NetEQ */
            nSamples = packet_get_samples_per_frame(op.packet, 48000) * packet_get_nb_frames(op.packet, op.bytes);
            //printf("playing %d samples; granule position: %d\n", nSamples, (int)op.granulepos);
            gvoe.vm.seqNum++;
            gvoe.vm.timeStamp += nSamples;
            MakeRTPheader(RTPpacketBuf,
                          gvoe.vm.c.pltype,
                          gvoe.vm.seqNum,
                          gvoe.vm.timeStamp,
                          gvoe.vm.ssrc);
            
            memcpy(&RTPpacketBuf[RTP_HEADER_IN_BYTES], op.packet, op.bytes * sizeof(uint8_t));
            gvoe.nw->ReceivedRTPPacket(gvoe.vm.ch, (const void*)RTPpacketBuf, op.bytes + RTP_HEADER_IN_BYTES);
            gvoe.vm.samplepos += nSamples;
            break;
        }
        
        /* Loop for all complete pages we got (most likely only one) */
        if(ogg_sync_pageout(&gvoe.vm.oy, &gvoe.vm.og)==1)
        {
            /* Add page to the bitstream */
            ogg_stream_pagein(&gvoe.vm.os, &gvoe.vm.og);
        } else {
            /* Read more data */
            char *data;
            /* Get the ogg buffer for writing */
            data = ogg_sync_buffer(&gvoe.vm.oy, 1000);
            /* Read bitstream from input file */
            nb_read = fread(data, sizeof(char), 1000, fp);
            ogg_sync_wrote(&gvoe.vm.oy, nb_read);
        }
    }
    
    /* Update file length and current position */
    if (gvoe.vm.samplepos >= gvoe.vm.samplestot && op.e_o_s == 0 && nb_read > 0) {
        /* Rescan file */
        uint64_t pos = gvoe.vm.samplepos;
        if(voe_me_init_stream(&gvoe.vm, UINT32_MAX)){
            voe_vm_stop_play();
            return;
        }
        gvoe.vm.file_length_ms = (int)((gvoe.vm.samplestot + 24) / 48);
        info("new voice message length: %d ms\n", gvoe.vm.file_length_ms);
        if(voe_me_init_stream(&gvoe.vm, pos)){
            voe_vm_stop_play();
            return;
        }
    }
    
    // Callback with curent playout position
    if(gvoe.vm.play_statush) {
        gvoe.vm.play_statush( true, (unsigned int)(gvoe.vm.samplepos / 48), gvoe.vm.file_length_ms, gvoe.vm.play_statush_arg);
    }
    
    if (op.e_o_s || nb_read == 0) {
        info("End of voice message: end of %s \n", op.e_o_s ? "stream" : "file");
        gvoe.vm.finished = 1;
        /* Sleep for 250 ms before calling stop_play(), to empty buffers */
        nSamples = 48 * 250;
    }
    
    struct timeval played, now, res;
    played.tv_sec = 0;
    played.tv_usec = (nSamples * 1000) / 48;
    timeradd(&gvoe.vm.next_event, &played, &gvoe.vm.next_event);
    gettimeofday(&now, NULL);
    timersub(&gvoe.vm.next_event, &now, &res);
    int32_t sleep_ms = (int32_t)res.tv_sec*1000 + (int32_t)res.tv_usec/1000;
    if(sleep_ms < 0){
        debug("Warning sleep_ms = %d not reading fast enough !! \n", sleep_ms);
        sleep_ms = 0;
    }
    
    tmr_start(&gvoe.vm.tmr_vm_player, sleep_ms, tmr_vm_player_handler, NULL);
}

int voe_vm_get_length(const char fileNameUTF8[1024],
                      int* length_ms)
{
    int err = 0;
    struct vm_state vm;
    memset(&vm, 0, sizeof(struct vm_state));
    
    vm.fp = fopen(fileNameUTF8,"rb");
    if (vm.fp == NULL) {
        error("voe_vm_start_play: Could not open file: %s\n", fileNameUTF8);
        return -1;
    }
    /* Measure length of Ogg file */
    vm.samplestot = 0;
    if(voe_me_init_stream(&vm, UINT32_MAX)) {
        *length_ms = 0;
        err = 1;
    } else {
        *length_ms = vm.samplestot / 48;
    }
    fclose(vm.fp);
    return err;
}

int voe_vm_start_play(const char fileNameUTF8[1024],
                      int  start_time_ms,
                      vm_play_status_h *handler,
                      void *arg)
{
    if (gvoe.nch > 0 && gvoe.vm.fp == NULL) {
        error("voe_vm_start_play: cannot start when in a call \n");
        return -1;
    }
    
    debug("voe_vm_start_play \n");
    
    gvoe.vm.start_time_ms = start_time_ms;
    
    if(gvoe.vm.fp != NULL){
        /* Already playing a voice message; all we had to do is set the new start time */
        return 0;
    }
    
    gvoe.vm.fp = fopen(fileNameUTF8,"rb");
    if (gvoe.vm.fp == NULL) {
        error("voe_vm_start_play: Could not open file: %s\n", fileNameUTF8);
        return -1;
    }
    
    gvoe.vm.play_statush = handler;
    gvoe.vm.play_statush_arg = arg;
    
    gvoe.base->Init();
    
    gvoe.vm.ch = gvoe.base->CreateChannel();
    if (gvoe.vm.ch == -1) {
        return ENOMEM;
    }
    
    int numberOfCodecs = gvoe.codec->NumOfCodecs();
    bool codec_found = false;
    for( int i = 0; i < numberOfCodecs; i++ ){
        gvoe.codec->GetCodec( i, gvoe.vm.c);
        if(strcmp(gvoe.vm.c.plname,"opus") == 0){
            codec_found = true;
            break;
        }
    }
    
    gvoe.codec->SetRecPayloadType(gvoe.vm.ch, gvoe.vm.c);
    gvoe.vm.transport = new VmTransport(NULL,NULL);
    gvoe.nw->RegisterExternalTransport(gvoe.vm.ch, *gvoe.vm.transport);
    
    gvoe.base->StartReceive(gvoe.vm.ch);
    gvoe.base->StartPlayout(gvoe.vm.ch);
    
    gvoe.vm.seqNum = 0;
    gvoe.vm.timeStamp = 0;
    gvoe.vm.ssrc = 2345;
    
    /* Measure length of Ogg file */
    gvoe.vm.samplestot = 0;
    if(voe_me_init_stream(&gvoe.vm, UINT32_MAX)) {
        voe_vm_stop_play();
        return -1;
    }
    gvoe.vm.file_length_ms = gvoe.vm.samplestot / 48;
    info("Length of voice message: %d milliseconds\n", gvoe.vm.file_length_ms);
    
    gvoe.vm.finished = 0;
    gettimeofday(&gvoe.vm.next_event, NULL);
    tmr_start(&gvoe.vm.tmr_vm_player, 0, tmr_vm_player_handler, NULL);
    
    return 0;
}

int voe_vm_stop_play()
{
    int err = 0;
    
    if(!gvoe.vm.fp){
        debug("voe_vm_stop_play(): allready stopped !! \n");
        return 0;
    }
    info("voe_vm_stop_play()\n");
    
    tmr_cancel(&gvoe.vm.tmr_vm_player);
    
    gvoe.base->StopReceive(gvoe.vm.ch);
    gvoe.base->StopSend(gvoe.vm.ch);
    gvoe.base->DeleteChannel(gvoe.vm.ch);
    gvoe.base->Terminate();
    
    fclose(gvoe.vm.fp);
    gvoe.vm.fp = NULL;
    
    if(gvoe.vm.stream_init) {
        ogg_stream_clear(&gvoe.vm.os);
    }
    ogg_sync_clear(&gvoe.vm.oy);
    
    // Fire callback telling that we stopped playing
    if(gvoe.vm.play_statush){
        gvoe.vm.play_statush( false, gvoe.vm.file_length_ms, gvoe.vm.file_length_ms, gvoe.vm.play_statush_arg);
    }
    
    gvoe.vm.play_statush = NULL;
    gvoe.vm.play_statush_arg = NULL;
    
    return err;
}

#define BUF_SIZE  60*48
#define DATA_SIZE 200

int voe_vm_apply_effect(const char inFileNameUTF8[1024], const char outFileNameUTF8[1024], audio_effect effect)
{
    struct aueffect *aue;
    
    OpusEncoder *enc=NULL;
    OpusDecoder *dec=NULL;
    int err;
    struct vm_state vm;
    memset(&vm, 0, sizeof(struct vm_state));
    
    vm.fp = fopen(inFileNameUTF8,"rb");
    if (vm.fp == NULL) {
        error("voe_vm_start_play: Could not open file: %s\n", inFileNameUTF8);
        return -1;
    }
    
    if (voe_me_init_stream(&vm, 0)){
        return -1;
    }
    
    // Create Opus encoder and decoder
    dec = opus_decoder_create(24000, 1, &err);
    enc = opus_encoder_create(24000, 1, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK)
    {
        error("Cannot create opus encoder: %s\n", opus_strerror(err));
        return -1;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));
    
    FILE* out_file = fopen(outFileNameUTF8,"wb");
    
    aueffect_alloc(&aue, effect, 24000);
    if(err){
        error("aueffect_alloc failed \n");
        fclose(out_file);
        return -1;
    }
    
    /* Initialize packet writing */
    ogg_packet op_enc;
    ogg_stream_state os;
    
    init_ogg_stream(&op_enc, &os, &out_file);
    
    /* Extract payload from Ogg stream */
    int nSamples = 0;
    ogg_packet op_dec;
    int nb_read = 1;
    int16_t buf[BUF_SIZE];
    uint8_t data[DATA_SIZE];
    int output_samples, len;
    while(1){
        while (nb_read > 0) {
            /* Extract all available packets */
            if (ogg_stream_packetout(&vm.os, &op_dec) == 1)
            {
                if(op_enc.bytes){
                    op_enc.packet = data;
                    op_enc.packetno++;
                    op_enc.granulepos += output_samples;
                    ogg_stream_packetin(&os, &op_enc);
                    ogg_page og;
                    ogg_stream_flush_fill(&os, &og, 255*255);
                    int ret=oe_write_page(&og, out_file);
                    if(ret!=og.header_len+og.body_len){
                        info("Ogg failed writing data to output stream\n");
                    }
                    op_enc.bytes = 0;
                }
                
                output_samples = opus_decode(dec, op_dec.packet, op_dec.bytes * sizeof(uint8_t), buf, BUF_SIZE, 0);
                
                size_t proc_samples;
                aueffect_process(aue, (const int16_t*)buf, buf, output_samples, &proc_samples);                
                if(proc_samples == output_samples){
                    len = opus_encode(enc, buf, output_samples, data, DATA_SIZE);
                    op_enc.bytes = len;
                } else {
                    error("voe_vm_apply_effect: can only use real time effects \n");
                }
                
                break;
            }
        
            /* Loop for all complete pages we got (most likely only one) */
            if(ogg_sync_pageout(&vm.oy, &vm.og)==1)
            {
                /* Add page to the bitstream */
                ogg_stream_pagein(&vm.os, &vm.og);
            } else {
                /* Read more data */
                char *data;
                /* Get the ogg buffer for writing */
                data = ogg_sync_buffer(&vm.oy, 1000);
                /* Read bitstream from input file */
                nb_read = fread(data, sizeof(char), 1000, vm.fp);
                ogg_sync_wrote(&vm.oy, nb_read);
            }
        }
    
        if (op_dec.e_o_s || nb_read == 0) {
            info("End of voice message: end of %s \n", op_dec.e_o_s ? "stream" : "file");
            break;
        }
    }

    op_enc.b_o_s=0;
    /* Set end-of-stream flag */
    op_enc.e_o_s=1;
    if(op_enc.bytes){
        op_enc.packet = data;
        op_enc.packetno++;
        op_enc.granulepos += PACKET_SIZE_MS * 24;
        ogg_stream_packetin(&os, &op_enc);
        ogg_page og;
        ogg_stream_flush_fill(&os, &og, 255*255);
        int ret=oe_write_page(&og, out_file);
        if(ret!=og.header_len+og.body_len){
            info("Ogg failed writing data to output stream\n");
        }
        op_enc.bytes = 0;
    }
    ogg_stream_clear(&os);
    
    mem_deref(aue);
    
    fclose(out_file);
    
    return 0;
}


