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

#import "AVSSound.h"
#include <re.h>
#include <avs.h>
@interface AVSSound ( )

- (void)updateVolume;

@property (nonatomic, assign) BOOL muted;
@property (nonatomic, assign) float level;

@property (nonatomic, strong) AVAudioPlayer *player;

@end


@implementation AVSSound

@synthesize sound;

- (float)volume
{
    return self.level;
}

- (void)setVolume:(float)volume
{
    self.level = volume;
    
    [self updateVolume];
}


- (BOOL)looping
{
	return self.player.numberOfLoops < 0;	
}

- (void)setLooping:(BOOL)looping
{
	self.player.numberOfLoops = looping ? -1 : 0;
}


- (BOOL)playbackMuted
{
    return self.muted;
}

- (void)setPlaybackMuted:(BOOL)playbackMuted
{
    self.muted = playbackMuted;
    
    [self updateVolume];
}


- (instancetype)init
{
    return nil;
}

- (instancetype)initWithName:(NSString *)name andAudioPlayer:(AVAudioPlayer *)player
{
    self = [super init];

    if ( self ) {
        self.name = name;
        self.player = player;
 
        player.delegate = self;
	[player prepareToPlay];
    }

    return name && player ? self : nil;
}


- (void)play
{
	info("AVSSound: play %s\n", [self.name UTF8String]);
	
	if (![self.delegate canStartPlayingMedia:self])
		return;
	
	dispatch_sync(dispatch_get_main_queue(), ^{
		[self.player setCurrentTime:0];
		[self.player play];
	});

	int n = 100;
	while (!self.player.playing && n-- > 0) {
		usleep(20000);
	}
	if (n <= 0)
		info("AVSSound playing did not start\n");
	else
		[self.delegate didStartPlayingMedia:self];
     
	info("AVSSound: %s playing=%s\n",
	     [self.name UTF8String],
	     self.player.playing ? "yes" : "no");
}

- (void)stop
{
	int n;
	
	info("AVSSound: stop: %s\n", [_name UTF8String]);
			
	dispatch_sync(dispatch_get_main_queue(), ^{
		[self.player stop];
		[self.player setCurrentTime:0];
	});

	n = 10;
	while (self.player.playing && n-- > 0) {
		usleep(50000);
		info("AVSSound: stop: %s playing=%s\n",
		     [_name UTF8String], self.player.playing ? "yes" : "no");
	}

	[self.delegate didFinishPlayingMedia:self];
}


- (void)pause
{
	[self.player pause];
	[self.delegate didPausePlayingMedia:self];
}

- (void)resume
{
        [self.player play];
	[self.delegate didResumePlayingMedia:self];
}


- (void)reset
{
	AVAudioPlayer *player;
	player = [[AVAudioPlayer alloc]
				 initWithContentsOfURL:self.player.url
						 error:nil];
	player.delegate = self;
	player.numberOfLoops = self.player.numberOfLoops;
	[player prepareToPlay];

	self.player = player;
}


- (void)updateVolume
{
    self.player.volume = self.muted ? 0 : self.level;
}


- (void)audioPlayerDecodeErrorDidOccur:(AVAudioPlayer *)player 
                                 error:(NSError *)err
{
	info("AVSSound: %s audioPlayerDecodeErrorDidOccur: error=%s\n",
	     [_name UTF8String],
	     [err.localizedDescription UTF8String]);
}


- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player 
                       successfully:(BOOL)flag
{
	info("AVSSound: %s audioPlayerDidFinishPlaying\n",
	     [_name UTF8String]);

	[self.delegate didFinishPlayingMedia:self];
}

@end
