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
#import "AVSMediaManager+Client.h"
#import "AVSFlowManager.h"

#import "AVSSound.h"

#define AVSMediaManagerDidChangeNotification @"AVS Media Manager Did Change Notification"

@interface AVSMediaManagerChangeNotification ( )

+ (instancetype)notificationWithMediaManager:(AVSMediaManager *)avsMediaManager;

- (instancetype)initWithMediaManager:(AVSMediaManager *)avsMediaManager;

@property (nonatomic, weak) AVSMediaManager *mm;

@property (nonatomic, readonly) BOOL shouldBeSent;

@property (nonatomic, assign) BOOL interruptDidChange;

@property (nonatomic, assign) BOOL intensityDidChange;

@property (nonatomic, assign) BOOL playbackMuteDidChange;
@property (nonatomic, assign) BOOL recordingMuteDidChange;

@property (nonatomic, assign) BOOL playbackModeDidChange;
@property (nonatomic, assign) BOOL recordingModeDidChange;

@property (nonatomic, assign) BOOL playbackRouteDidChange;
@property (nonatomic, assign) BOOL recordingRouteDidChange;

@property (nonatomic, assign) BOOL preferredPlaybackRouteDidChange;
@property (nonatomic, assign) BOOL preferredRecordingRouteDidChange;

@end


@implementation AVSMediaManagerChangeNotification

+ (instancetype)notificationWithMediaManager:(AVSMediaManager *)avsMediaManager
{
    return [[AVSMediaManagerChangeNotification alloc] initWithMediaManager:avsMediaManager];
}

+ (void)addObserver:(id<AVSMediaManagerObserver>)observer
{
}

+ (void)removeObserver:(id<AVSMediaManagerObserver>)observer
{
}

- (instancetype)initWithMediaManager:(AVSMediaManager *)avsMediaManager
{
    self.mm = avsMediaManager;
    
    return self;
}

- (BOOL)shouldBeSent
{
    return NO;
}

- (NSString *)name
{
    return AVSMediaManagerDidChangeNotification;
}

- (id)object
{
    return self.mm;
}

@end


@interface AVSMediaManager()
{
	NSString *_convId;
	AVSIntensityLevel _intensity;
};

@end

@implementation AVSMediaManager

static AVSMediaManager *_defaultMediaManager;

+ (AVSMediaManager *)defaultMediaManager
{
    if ( !_defaultMediaManager ) {
        _defaultMediaManager = [[AVSMediaManager alloc] init];
    }
    
    return _defaultMediaManager;
}

- (instancetype) init
{
	self = [super init];
	if (self) {
		_convId = nil;
		_intensity = AVSIntensityLevelFull;
	}

	return self;
}

- (void)playMediaByName:(NSString *)name
{
}

- (void)stopMediaByName:(NSString *)name
{
}

- (void)internalRegisterMediaFromConfiguration:(NSDictionary *)configuration inDirectory:(NSString *)directory
{
}

- (void)registerMedia:(id<AVSMedia>)media withOptions:(NSDictionary *)options
{
}

- (void)registerUrl:(NSURL*)url forMedia:(NSString*)name
{
}

- (void)unregisterAllMedia
{
}

- (void)unregisterMedia:(id<AVSMedia>)media
{
}


- (BOOL)isInterrupted
{
	return NO;
}

- (void)setInterrupted:(BOOL)interrupted
{
}


- (AVSIntensityLevel)intensity
{
	return _intensity;
}

- (void)setIntensity:(AVSIntensityLevel)intensity
{
	_intensity = intensity;
}


- (BOOL)isPlaybackMuted
{
	return NO;
}

- (void)setPlaybackMuted:(BOOL)muted
{
}

- (void)enableSpeaker:(BOOL)speakerEnabled
{
}

- (AVSRecordingRoute)recordingRoute
{
	return AVSRecordingRouteUnknown;
}

- (AVSRecordingRoute)preferredRecordingRoute
{
	return AVSRecordingRouteUnknown;
}

- (void)setPreferredRecordingRoute:(AVSRecordingRoute)route
{
}

- (float)intensityForMediaName:(NSString *)name
{
	return 0.0f;
}

- (void)setIntensity:(float)intensity forMediaName:(NSString *)name
{
}

- (BOOL)isPlaybackMutedForMediaName:(NSString *)name
{
	return NO;
}

- (void)setPlaybackMuted:(BOOL)muted forMediaName:(NSString *)name
{
}


- (BOOL)isRecordingMutedForMediaName:(NSString *)name
{
	return NO;
}


- (void)setRecordingMuted:(BOOL)muted forMediaName:(NSString *)name
{
}


- (void)setCallState:(BOOL)inCall forConversation:(NSString *)convId
{
	_convId = convId;
}

- (void)setVideoCallState:(NSString *)convId
{
}

- (void)setupAudioDevice
{
}

- (void)resetAudioDevice
{
}

- (void)startAudio
{
}

- (void)stopAudio
{
}

- (void)audioActivated
{
}

- (void)audioDeActivated
{
}

- (void)setUiStartsAudio:(BOOL)ui_starts_audio
{
}


- (void)startRecordingWhenReady:(dispatch_block_t)block
{
}


- (void)stopRecording
{
}



@end

#import "AVSMediaManager+Client.h"

#define AVSMediaManagerClientDidChangeNotification @"AVS Media Manager Client Did Change Notification"


@interface AVSMediaManagerClientChangeNotification ( )

+ (instancetype)notificationWithMediaManager:(AVSMediaManager *)mediaManager;

- (instancetype)initWithMediaManager:(AVSMediaManager *)mediaManager;

@property (nonatomic, weak) AVSMediaManager *instance;
@property (nonatomic, readonly) BOOL shouldBeSent;
@property (nonatomic, assign) BOOL speakerEnableDidChange;
@property (nonatomic, assign) BOOL microphoneMuteDidChange;

@end


@implementation AVSMediaManagerClientChangeNotification

+ (instancetype)notificationWithMediaManager:(AVSMediaManager *)mediaManager
{
    return [[AVSMediaManagerClientChangeNotification alloc] initWithMediaManager:mediaManager];
}


+ (void)addObserver:(id<AVSMediaManagerClientObserver>)observer
{
    [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(mediaManagerDidChange:) name:AVSMediaManagerClientDidChangeNotification object:nil];
}

+ (void)removeObserver:(id<AVSMediaManagerClientObserver>)observer
{
    [[NSNotificationCenter defaultCenter] removeObserver:observer];
}


- (instancetype)initWithMediaManager:(AVSMediaManager *)mediaManager
{
    self.instance = mediaManager;
    
    return self;
}


- (NSString *)name
{
    return AVSMediaManagerClientDidChangeNotification;
}

- (id)object
{
    return self.instance;
}

- (NSDictionary *)userInfo
{
    return nil;
}


- (AVSMediaManager *)manager
{
    return self.instance;
}

- (BOOL)speakerEnableChanged
{
    return self.speakerEnableDidChange;
}

- (BOOL)microphoneMuteChanged
{
    return self.microphoneMuteDidChange;
}

@end


@interface AVSMediaManager (Observer) <AVSMediaManagerObserver>

@end


@implementation AVSMediaManager (Observer)

- (void)avsMediaManagerDidChange:(AVSMediaManagerChangeNotification *)notification
{
}

@end


@implementation AVSMediaManager (Client)


+ (AVSMediaManager *)sharedInstance
{
    return [AVSMediaManager defaultMediaManager];
}

- (void)playSound:(NSString *)name
{
    [self playMediaByName:name];
}

- (void)stopSound:(NSString *)name
{
    [self stopMediaByName:name];
}

- (void)registerMediaFromConfiguration:(NSDictionary *)configuration inDirectory:(NSString *)directory
{
	[self internalRegisterMediaFromConfiguration:configuration inDirectory:directory];
}

- (AVSIntensityLevel)intensityLevel
{
    float intensity = [self intensity];
    
    if ( intensity == 0 ) {
        return AVSIntensityLevelNone;
    }
    
    if ( intensity == 100 ) {
        return AVSIntensityLevelFull;
    }
    
    return AVSIntensityLevelSome;
}

- (void)setIntensityLevel:(AVSIntensityLevel)intensityLevel
{
    switch ( intensityLevel ) {
        case AVSIntensityLevelNone:
            [self setIntensity:0];
            break;
            
        case AVSIntensityLevelSome:
            [self setIntensity:50];
            break;
            
        case AVSIntensityLevelFull:
            [self setIntensity:100];
            break;
    }
}


- (BOOL)isSpeakerEnabled
{
    return self.playbackRoute == AVSPlaybackRouteSpeaker;
}

- (void)setSpeakerEnabled:(BOOL)speakerEnabled
{
    if(speakerEnabled != self.isSpeakerEnabled) {
    	[self enableSpeaker:speakerEnabled];
    }
}


- (BOOL)isSpeakerMuted
{
    return [self isPlaybackMuted];
}

- (void)setSpeakerMuted:(BOOL)speakerMuted
{
    [self setPlaybackMuted:speakerMuted];
}


- (BOOL)isMicrophoneMuted
{
	return NO;
}

- (void)setMicrophoneMuted:(BOOL)microphoneMuted
{
}

- (void)routeChanged:(AVSPlaybackRoute)route
{
}


@end


