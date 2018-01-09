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

#import "AVSMediaManager+Client.h"

#include "re.h"
#include "avs.h"

#define AVSMediaManagerClientDidChangeNotification @"AVS Media Manager Client Did Change Notification"

void avsmm_on_route_changed(enum mediamgr_auplay new_route, void *arg);

void avsmm_on_route_changed(enum mediamgr_auplay new_route, void *arg)
{
    AVSMediaManager *mm = (__bridge AVSMediaManager*)arg;
    AVSPlaybackRoute route;

    
    switch(new_route) {
        case MEDIAMGR_AUPLAY_EARPIECE:
        case MEDIAMGR_AUPLAY_LINEOUT:
        case MEDIAMGR_AUPLAY_SPDIF:
        case MEDIAMGR_AUPLAY_UNKNOWN:
            route = AVSPlaybackRouteBuiltIn;
            break;

        case MEDIAMGR_AUPLAY_SPEAKER:
            route = AVSPlaybackRouteSpeaker;
            break;

        case MEDIAMGR_AUPLAY_HEADSET:
            route = AVSPlaybackRouteHeadset;
            break;
        case MEDIAMGR_AUPLAY_BT:
            route = AVSPlaybackRouteBluetooth;
            break;
    }

    info("avsmm_on_route_changed: tid=%p route=%d mapped=%d\n",
	 pthread_self(), new_route, (int)route);
    
    [mm routeChanged:route];
}

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
/*
    AVSMediaManagerClientChangeNotification *clientNotification = [[AVSMediaManagerClientChangeNotification alloc] initWithMediaManager:self];
    
    if ( notification.intensityChanged ) {
        clientNotification.intensityLevelDidChange = YES;
    }

    if ( notification.preferredPlaybackRouteChanged ) {
        clientNotification.speakerEnableDidChange = YES;
    }
    
    if ( notification.playbackMuteChanged ) {
        clientNotification.speakerMuteDidChange = YES;
    }
    
    if ( notification.recordingMuteChanged ) {
        clientNotification.microphoneMuteDidChange = YES;
    }
    
    if ( notification.interruptChanged ) {
        clientNotification.audioControlDidChange = YES;
    }
  
    if ( clientNotification.shouldBeSent ) {
        [[NSNotificationCenter defaultCenter] postNotification:clientNotification];
    }
*/
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

/*
- (void)playExternalMedia:(id<AVSMedia>)media
{
    NSMutableDictionary *options = [[NSMutableDictionary alloc] init];
    
    [options setValue:@"external" forKey:@"media"];
    [options setValue:@"" forKey:@"path"];
    [options setValue:@"" forKey:@"name"];
    [options setValue:@"" forKey:@"type"];
    [options setValue:@"" forKey:@"eventId"];
    [options setValue:@"" forKey:@"format"];
    [options setValue:@0 forKey:@"mixingAllowed"];
    [options setValue:@0 forKey:@"incallAllowed"];
    [options setValue:@0 forKey:@"loopAllowed"];
    [options setValue:@1 forKey:@"requirePlayback"];
    [options setValue:@0 forKey:@"requireRecording"];
    
    [self registerMedia:media withOptions:options];
    [self playMedia:media];
}

- (void)pauseExternalMedia:(id<AVSMedia>)media
{
    [self pauseMedia:media];
}

- (void)resumeExternalMedia:(id<AVSMedia>)media
{
    [self resumeMedia:media];
}

- (void)stopExternalMedia:(id<AVSMedia>)media
{
    [self stopMedia:media];
    [self unregisterMedia:media];
}
*/

- (void)registerMediaFromConfiguration:(NSDictionary *)configuration inDirectory:(NSString *)directory
{
	[self internalRegisterMediaFromConfiguration:configuration inDirectory:directory];
}

/*
- (BOOL)isInControlOfAudio
{
    return [self isInterrupted];
}
*/

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
	BOOL enabled;

	enabled = self.playbackRoute == AVSPlaybackRouteSpeaker;

	info("AVSMediaManager:isSpeakerEnabled: route=%d speaker=%d "
	     "enabled=%s\n",
	     (int)self.playbackRoute, (int)AVSPlaybackRouteSpeaker,
	     enabled?"yes":"no");
	
	return self.sysUpdated && enabled;
}

- (void)setSpeakerEnabled:(BOOL)speakerEnabled
{
	info("AVSMediaManager:spaekerEnabled=%s\n", speakerEnabled?"yes":"no");

	self.playbackRoute = speakerEnabled ? AVSPlaybackRouteSpeaker
		                            : AVSPlaybackRouteBuiltIn;

    	[self enableSpeaker:speakerEnabled];
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
	bool muted;

	info("AVSMediaManager: isMicrophoneMuted\n");
	voe_get_mute(&muted);
	info("AVSMediaManager: isMicrophoneMuted: muted=%d\n", muted);

	return muted ? YES : NO;
}

- (void)setMicrophoneMuted:(BOOL)microphoneMuted
{
	info("AVSMediaManager: setMicrophoneMuted: muted=%s\n",
	     microphoneMuted ? "yes" : "no");

	voe_set_mute(microphoneMuted ? true : false);
	dispatch_async(dispatch_get_main_queue(),^{
			AVSMediaManagerClientChangeNotification *notification =
				[AVSMediaManagerClientChangeNotification notificationWithMediaManager:self];

			notification.microphoneMuteDidChange = YES;
			[[NSNotificationCenter defaultCenter] postNotification:notification];
		});
}

- (void)routeChanged:(AVSPlaybackRoute)route
{
	info("AVSMediaManage: route_changed: %d->%d\n",
	     (int)self.playbackRoute, (int)route);
	self.playbackRoute = route;
	self.sysUpdated = YES;
	dispatch_sync(dispatch_get_main_queue(),^{	
			AVSMediaManagerClientChangeNotification *notification =
				[AVSMediaManagerClientChangeNotification notificationWithMediaManager:self];

			notification.speakerEnableDidChange = YES;
			[[NSNotificationCenter defaultCenter] postNotification:notification];
		});
}

@end

