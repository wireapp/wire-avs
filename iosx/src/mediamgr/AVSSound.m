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


@interface AVSSound ( )

- (void)updateVolume;

@property (nonatomic, assign) BOOL muted;
@property (nonatomic, assign) float level;

@property (nonatomic, strong) AVAudioPlayer *player;

@end


@implementation AVSSound


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
    }
    
    return name && player ? self : nil;
}


- (void)play
{
    [self.player setCurrentTime:0];
    [self.player play];
    [self.delegate didStartPlayingMedia:self];
}

- (void)stop
{
    [self.player stop];
    [self.player setCurrentTime:0];
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


- (void)updateVolume
{
    self.player.volume = self.muted ? 0 : self.level;
}


- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)flag
{
    [self.delegate didFinishPlayingMedia:self];
}

@end
