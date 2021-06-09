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

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#import "AVSMediaManager.h"


@interface AVSSound : NSObject <AVAudioPlayerDelegate, AVSMedia>
- (instancetype)initWithName:(NSString *)name
	              andUrl:(NSURL *)url
	             looping:(BOOL)loop;

- (void)play;
- (void)stop;

- (void)pause;
- (void)resume;

- (void)reset;

@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSURL *url;

@property (nonatomic, weak) id<AVSMediaDelegate> delegate;

@property (nonatomic, assign) float volume;
@property (nonatomic, assign) BOOL looping;

@property (nonatomic, assign) BOOL playbackMuted;
@property (nonatomic, assign) BOOL recordingMuted;

@property (nonatomic, assign) void *sound;

@end
