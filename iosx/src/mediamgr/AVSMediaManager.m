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

#include "TargetConditionals.h"
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#import "AVSMediaManager.h"
#import "AVSMediaManager+Client.h"
#import "AVSFlowManager.h"

#include "re.h"
#include "avs.h"

#import "AVSSound.h"

#define AVSMediaManagerDidChangeNotification @"AVS Media Manager Did Change Notification"

void avsmm_on_route_changed(enum mediamgr_auplay new_route, void *arg);

void AVSOut ( NSInteger level, NSString *message, va_list arguments )
{
    // TODO add log level stuff again
    //if ( level <= [[NSUserDefaults standardUserDefaults] integerForKey:AVSMediaManagerLogLevel] ) {
        NSLog(@"[AVS MEDIA MANAGER]: %@", [[NSString alloc] initWithFormat:message arguments:arguments]);
    //}
}

void AVSLog ( NSInteger level, NSString *message, ... )
{
    va_list arguments;
    va_start(arguments, message);
    
    AVSOut(level, message, arguments);
    
    va_end(arguments);
}

void AVSDebug ( NSString *message, ... )
{
    va_list arguments;
    va_start(arguments, message);
    
    AVSOut(3, message, arguments);
    
    va_end(arguments);
}

void AVSWarn ( NSString *message, ... )
{
    va_list arguments;
    va_start(arguments, message);
    
    AVSOut(2, message, arguments);
    
    va_end(arguments);
}

void AVSError ( NSString *message, ... )
{
    va_list arguments;
    va_start(arguments, message);
    
    AVSOut(1, message, arguments);
    
    va_end(arguments);
}

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
    [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(avsMediaManagerDidChange:) name:AVSMediaManagerDidChangeNotification object:nil];
}

+ (void)removeObserver:(id<AVSMediaManagerObserver>)observer
{
    [[NSNotificationCenter defaultCenter] removeObserver:observer];
}

- (instancetype)initWithMediaManager:(AVSMediaManager *)avsMediaManager
{
    self.mm = avsMediaManager;
    
    return self;
}

- (BOOL)shouldBeSent
{
    if ( self.interruptDidChange ) return YES;
    if ( self.intensityDidChange ) return YES;
    if ( self.playbackMuteDidChange ) return YES;
    if ( self.recordingMuteDidChange ) return YES;
    if ( self.playbackModeDidChange ) return YES;
    if ( self.recordingModeDidChange ) return YES;
    if ( self.playbackRouteDidChange ) return YES;
    if ( self.recordingRouteDidChange ) return YES;
    if ( self.preferredPlaybackRouteDidChange ) return YES;
    if ( self.preferredRecordingRouteDidChange ) return YES;
    
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
	struct mediamgr *_mm;
	NSString *_convId;
	AVSIntensityLevel _intensity;
	NSDictionary *_sounds;
};

- (void)mcatChanged:(enum mediamgr_state)mmState;
- (void)registerMedia:(NSString*)name withUrl:(NSURL*)url;

@end



static void on_mcat_changed(enum mediamgr_state new_mcat, void *arg)
{
    AVSMediaManager *mm = (__bridge AVSMediaManager*)arg;

    [mm mcatChanged:new_mcat];
}

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
		mediamgr_alloc(&_mm, on_mcat_changed, (__bridge void *)(self));
		_convId = nil;
		_intensity = AVSIntensityLevelFull;
		self.playbackRoute = AVSPlaybackRouteBuiltIn;
		self.sysUpdated = NO;
		mediamgr_register_route_change_h(_mm, avsmm_on_route_changed, (__bridge void *)(self));
	}

	return self;
}

- (void)playMediaByName:(NSString *)name
{
	mediamgr_play_media(_mm, [name UTF8String]);
}

- (void)stopMediaByName:(NSString *)name
{
	mediamgr_stop_media(_mm, [name UTF8String]);
}

- (void)internalRegisterMediaFromConfiguration:(NSDictionary *)configuration inDirectory:(NSString *)directory
{

	NSDictionary *sounds = [configuration valueForKey:@"sounds"];
	_sounds = sounds;
		
	for ( NSString *name in sounds.allKeys ) {
		NSDictionary *snd = [sounds objectForKey:name];
		NSString *path = [snd objectForKey:@"path"];
		NSString *format = [snd objectForKey:@"format"];

		NSString *fullPath = [[NSBundle mainBundle] pathForResource:path ofType:format inDirectory:directory];
		if (!fullPath) {
			continue;
		}

		NSURL *url = [NSURL fileURLWithPath:fullPath];
		if (!url) {
			continue;
		}

		[self registerMedia:name withUrl:url];
	}

}

- (void)registerMedia:(id<AVSMedia>)media withOptions:(NSDictionary *)options
{
	bool mixing = true;
	bool incall = false;
	int intensity = 100;
	int priority = 0;

	mediamgr_register_media(_mm, [media.name UTF8String], (__bridge_retained void *)(media),
		mixing, incall, intensity, priority, false);
}

- (void)registerUrl:(NSURL*)url forMedia:(NSString*)name
{
	mediamgr_unregister_media(_mm, [name UTF8String]);
	[self registerMedia:name withUrl:url];
}

- (void)registerMedia:(NSString*)name withUrl:(NSURL*)url 
{
	NSDictionary *snd = [_sounds objectForKey:name];
	if (!snd) {
		return;
	}

	BOOL loop = [[snd objectForKey:@"loopAllowed"] intValue] > 0;
	bool mixing = [[snd objectForKey:@"mixingAllowed"] intValue] > 0;
	bool incall = [[snd objectForKey:@"incallAllowed"] intValue] > 0;
	int intensity = [[snd objectForKey:@"intensity"] intValue];
	int priority = [name hasPrefix:@"ringing"] ? 1 : 0; // TODO: get this from the file

	NSLog(@"registering media %@ for file %@", name, url);

	AVSSound *sound = [[AVSSound alloc] initWithName:name
						  andUrl:url
						 looping:loop];

	bool is_call_media = false;

	if ([name isEqualToString: @"ringing_from_me"])
		is_call_media = true;
	else if ([name isEqualToString: @"ready_to_talk"])
		is_call_media = true;
		
	mediamgr_register_media(_mm, [name UTF8String], (__bridge_retained void *)(sound),
		mixing, incall, intensity, priority, is_call_media);
}

- (void)unregisterAllMedia
{
}

- (void)unregisterMedia:(id<AVSMedia>)media
{
	mediamgr_unregister_media(_mm, [media.name UTF8String]);
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
	enum mediamgr_sound_mode smode = MEDIAMGR_SOUND_MODE_ALL;

	switch(intensity) {
		default:
		case AVSIntensityLevelFull:
			smode = MEDIAMGR_SOUND_MODE_ALL;
			break;

		case AVSIntensityLevelSome:
			smode = MEDIAMGR_SOUND_MODE_SOME;
			break;

		case AVSIntensityLevelNone:
			smode = MEDIAMGR_SOUND_MODE_NONE;
			break;

	}
	mediamgr_set_sound_mode(_mm, smode);
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
    mediamgr_enable_speaker(_mm, speakerEnabled ? true : false);
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
}

- (void)setVideoCallState:(NSString *)convId;
{
}

- (void)setupAudioDevice
{
	info("AVSMediaManager: setupAudioDevice\n");
	mediamgr_enter_call(_mm);
}

- (void)resetAudioDevice
{
	info("AVSMediaManager: resetAudioDevice\n");
	//mediamgr_audio_reset(_mm);
}

- (void)startAudio
{
	info("AVSMediaManager: startAudio\n");	
	mediamgr_audio_reset(_mm);
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


static void start_rec_handler(void *arg)
{
	dispatch_block_t blk = (__bridge dispatch_block_t)arg;

	dispatch_async(dispatch_get_main_queue(), blk);

	CFRelease((__bridge void *)blk);
}


- (void)startRecordingWhenReady:(dispatch_block_t)blk
{
	mediamgr_start_recording(_mm, start_rec_handler,
				 (void*)CFRetain((__bridge void *)blk));
}

- (void)stopRecording
{
	mediamgr_stop_recording(_mm);
}

- (void)mcatChanged:(enum mediamgr_state)state
{
	AVSFlowManagerCategory fcat = FLOWMANAGER_CATEGORY_NORMAL;

	switch(state) {
		case MEDIAMGR_STATE_INVIDEOCALL:
		case MEDIAMGR_STATE_INCALL:
		case MEDIAMGR_STATE_RESUME:
			fcat = FLOWMANAGER_CATEGORY_CALL;
			break;

		case MEDIAMGR_STATE_HOLD:
			fcat = FLOWMANAGER_CATEGORY_HOLD;
			break;

		default:
			fcat = FLOWMANAGER_CATEGORY_NORMAL;
			break;
	}

	if (_convId) {
		[[AVSFlowManager getInstance] mediaCategoryChanged: _convId category: fcat];
	}
}

@end

