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
//
//  ViewController.m
//  AudioTest
//
//  Created by Vladimir Darmin on 11/23/14.
//  Copyright (c) 2014 Vladimir Darmin. All rights reserved.
//

#import "ViewController.h"

#import "AVSFlowManager.h"
#import "AVSMediaManager.h"

@interface ViewController (FlowManagerDelegate) <AVSFlowManagerDelegate>

- (void)main;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
    
    [self main];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (void)main
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *docs_dir = [paths objectAtIndex:0];
    
#if 0 // Test Media Manager Route Changes and mute / unmute
    NSString *file_name = [docs_dir stringByAppendingString:@"/voicemessage.ogg"];
    
    // Init mediamanager
    AVSMediaManager *_mediaManager;
    _mediaManager = [AVSMediaManager alloc];
    
    // Init flowmanager
    AVSFlowManager *_flowManager;
    _flowManager = [[AVSFlowManager alloc] initWithDelegate:self mediaManager:_mediaManager];

    timespec t;
    t.tv_sec = 0; // sleep 200 ms
    t.tv_nsec = 200*1000*1000;;
    
    [_flowManager vmStartRecord:file_name];
    for( int i = 0 ; i < 10; i++){
    
        [_mediaManager setPreferredPlaybackRoute:AVSPlaybackRouteSpeaker];
    
        nanosleep(&t, NULL);
    
        [_mediaManager setPreferredPlaybackRoute:AVSPlaybackRouteBuiltIn];

        nanosleep(&t, NULL);

        [_mediaManager setRecordingMuted:TRUE];
        
        nanosleep(&t, NULL);
        
        [_mediaManager setRecordingMuted:FALSE];

        nanosleep(&t, NULL);
        
        printf("Loop %d finished \n", i);
    }
    [_flowManager vmStopRecord];
    
    [_flowManager vmStartPlay:file_name toStart:0];
    for( int i = 0 ; i < 10; i++){
        
        [_mediaManager setPreferredPlaybackRoute:AVSPlaybackRouteSpeaker];
        
        nanosleep(&t, NULL);
        
        [_mediaManager setPreferredPlaybackRoute:AVSPlaybackRouteBuiltIn];
        
        nanosleep(&t, NULL);
        
        [_mediaManager setRecordingMuted:TRUE];
        
        nanosleep(&t, NULL);
        
        [_mediaManager setRecordingMuted:FALSE];
        
        nanosleep(&t, NULL);
        
        printf("Loop %d finished \n", i);
    }
    [_flowManager vmStopPlay];
#endif
    
#if 0 // Test Voice Messaging recording and playout
    NSString *file_name = [docs_dir stringByAppendingString:@"/voicemessage.ogg"];
    
    // Init mediamanager
    AVSMediaManager *_mediaManager;
    _mediaManager = [AVSMediaManager alloc];
    
    // Init flowmanager
    AVSFlowManager *_flowManager;
    _flowManager = [[AVSFlowManager alloc] initWithDelegate:self mediaManager:_mediaManager];
    
    printf("Voice Messaging Test using file %s \n", [file_name UTF8String]);

    [_flowManager vmStartRecord:file_name];
    
    timespec t;
    t.tv_sec = 10;
    t.tv_nsec = 0;
    nanosleep(&t, NULL);
    
    [_flowManager vmStopRecord];
    
    [_flowManager vmStartPlay:file_name toStart:0];

    t.tv_sec = 10;
    t.tv_nsec = 0;
    nanosleep(&t, NULL);
    
    [_flowManager vmStopPlay];
#endif
    

    const char *command = [docs_dir UTF8String];
    int platform_bits = 8*sizeof(void*);
    printf("Running test in %d bit mode \n", platform_bits);

    /*******************************************/
    /* Audio effect test                       */
    /*******************************************/
    {
        char *argv[] = {"-fs","44100","-in","testfile32kHz.pcm","-effect","pitch_up_down_3","-out","out.pcm"};
        int argc = sizeof(argv)/sizeof(char*);
        
        effect_test(argc, argv, command);
    }
#if 0
    /*******************************************/
    /* Loopback test                           */
    /*******************************************/
    {
        char *argv[] = {"dummy"};
        int argc = sizeof(argv)/sizeof(char*);
        
        voe_loopback_test(argc, argv, command);
    }
    /*******************************************/
    /* Conferencing encoder optimization test  */
    /*******************************************/
    {    
        voe_conf_test_dec(command, false, -1);
    }
    /************************************/
    /* Acm test Opus and clean channel  */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"-codec","opus","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out1_32.pcm","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else if(platform_bits == 64){
            char *argv[] = {"-codec","opus","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out1_64.pcm","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
    /************************************/
    /* Acm test Opus and Wifi channel  */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"-codec","opus","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out2_32.pcm","-nw_type","wifi","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else if(platform_bits == 64){
            char *argv[] = {"-codec","opus","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out2_64.pcm","-nw_type","wifi","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
    /************************************/
    /* Acm test PCMU and clean channel  */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"-codec","PCMU","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out3_32.pcm","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else if(platform_bits == 64){
            char *argv[] = {"-codec","PCMU","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out3_64.pcm","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }

    /************************************/
    /* Acm test PCMU and Wifi channel  */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"-codec","PCMU","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out4_32.pcm","-nw_type","wifi","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else if(platform_bits == 64){
            char *argv[] = {"-codec","PCMU","-fs","16000","-in","far16.pcm","-rtp","rtp.dat",
                            "-out","acm_out4_64.pcm","-nw_type","wifi","-file_path","dummy"};
            int argc = sizeof(argv)/sizeof(char*);
            argv[argc-1] = (char *)command;
            acm_test(argc, argv);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
    /************************************/
    /* Apm test                         */
    /************************************/
    {
        char *argv[] = {"-near","near16.pcm","-far","far16.pcm","-out",""};
        int argc = sizeof(argv)/sizeof(char*);
        apm_test(argc, argv, command);
    }

    /************************************/
    /* Resampler test 44.1 -> 48 kHz    */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"-fs_in","44100","-in","near44100.pcm","-fs_out","48000","-out","out48000_32.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            resampler_test(argc, argv, command);
        } else if(platform_bits == 64){
            char *argv[] = {"-fs_in","44100","-in","near44100.pcm","-fs_out","48000","-out","out48000_64.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            resampler_test(argc, argv, command);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
    /************************************/
    /* Resampler test 48 -> 44.1 kHz    */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"-fs_in","48000","-in","near48000.pcm","-fs_out","44100","-out","out44100_32.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            resampler_test(argc, argv, command);
        } else if(platform_bits == 64){
            char *argv[] = {"-fs_in","48000","-in","near48000.pcm","-fs_out","44100","-out","out44100_64.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            resampler_test(argc, argv, command);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
    /************************************/
    /* Opus in complexity 10            */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"opus_demo","voip","16000","1","32000","-complexity","10","far16.pcm","opus_c10_32.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            opus_demo(argc, argv, command);
        } else if(platform_bits == 64){
            char *argv[] = {"opus_demo","voip","16000","1","32000","-complexity","10","far16.pcm","opus_c10_64.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            opus_demo(argc, argv, command);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
    
    /************************************/
    /* Opus in complexity 0             */
    /************************************/
    {
        if(platform_bits == 32){
            char *argv[] = {"opus_demo","voip","16000","1","32000","-complexity","0","far16.pcm","opus_c0_32.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            opus_demo(argc, argv, command);
        } else if(platform_bits == 64){
            char *argv[] = {"opus_demo","voip","16000","1","32000","-complexity","0","far16.pcm","opus_c0_64.pcm"};
            int argc = sizeof(argv)/sizeof(char*);
            opus_demo(argc, argv, command);
        } else {
            printf("Arcitecture not 32 or 64 bit ??? \n");
        }
    }
#endif
}

@end
@implementation ViewController (FlowManagerDelegate)

- (void)vmStatushandler:(BOOL)is_playing current_time:(int)cur_time_ms length:(int)file_length_ms;
{
    printf("vmStatushandler isPlaying = %d current_time = %d ms file_length_ms = %d ms \n", is_playing, cur_time_ms, file_length_ms);
}

@end
