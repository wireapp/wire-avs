

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "re.h"
#include "avs.h"
#include "mm_platform.h"
#include "avs_mediamgr.h"
#include "../../iosx/include/AVSMedia.h"


#if ! TARGET_OS_IPHONE
static enum mediamgr_auplay current_route = MEDIAMGR_AUPLAY_EARPIECE;
#endif

static bool in_call = false;
static struct mm *_mm = NULL;
static bool headset_connected = false;
static bool bt_device_connected = false;

// TODO: remove the need for the playing tracker
@interface PlayingTracker : NSObject <AVSMediaDelegate>
{
	NSMutableDictionary *_playingDict;
	NSLock *_lock;
};

- (instancetype) init;

- (void)didStartPlayingMedia:(id<AVSMedia>)media;
- (void)didPausePlayingMedia:(id<AVSMedia>)media;
- (void)didResumePlayingMedia:(id<AVSMedia>)media;
- (void)didFinishPlayingMedia:(id<AVSMedia>)media;

- (BOOL)isPlaying:(id<AVSMedia>)media;

@end

@implementation PlayingTracker : NSObject

- (instancetype) init
{
	_playingDict = [[NSMutableDictionary alloc] init];
	_lock = [[NSLock alloc] init];

	return self;
}

- (void)didStartPlayingMedia:(id<AVSMedia>)media
{
	[_lock lock];
	[_playingDict setObject:media forKey:media.name];
	bool change_cat = false;
    
    
	struct sound *snd = NULL;
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth;

	if ([media respondsToSelector:@selector(sound)]) {
		snd = (struct sound*)[media sound];
	}

	NSString *cat = [AVAudioSession sharedInstance].category;
	if (snd) {
		info("mm_platform_ios: didStartPlayingMedia: name=%s "
		     "is_call_media=%d mixing=%d\n",
		     snd->path, snd->is_call_media, snd->mixing);
		
		if (snd->is_call_media) {
			if (cat != AVAudioSessionCategoryPlayAndRecord){
				cat = AVAudioSessionCategoryPlayAndRecord;
				change_cat = true;
            }
		}
		else if (snd->mixing) {
			if (cat != AVAudioSessionCategoryPlayback && !in_call) {
				cat = AVAudioSessionCategoryAmbient;
				options |= AVAudioSessionCategoryOptionMixWithOthers;
				change_cat = true;
			}
		}
		else {
			if (cat != AVAudioSessionCategoryPlayback && !in_call) {
				cat = AVAudioSessionCategorySoloAmbient;
				change_cat = true;
			}
		}
	}
	else {
		info("mm_platform_ios: didStartPlayingMedia: "
		     "no snd for: %s\n",
		     [media.name UTF8String]);
		cat = AVAudioSessionCategoryPlayback;
	}
	if(change_cat){
		info("setCategory called from: %s:%d\n", __FILE__, __LINE__);
		[[AVAudioSession sharedInstance] setCategory: cat withOptions: options error:nil];
	}
        
	[_lock unlock];
}

- (void)didPausePlayingMedia:(id<AVSMedia>)media
{
	[_lock lock];
	[_playingDict removeObjectForKey:media.name];
	[_lock unlock];
}

- (void)didResumePlayingMedia:(id<AVSMedia>)media
{
	[_lock lock];
	[_playingDict setObject:media forKey:media.name];
	[_lock unlock];
}

- (void)didFinishPlayingMedia:(id<AVSMedia>)media
{
	[_lock lock];

	BOOL playing = ([_playingDict objectForKey:media.name] != nil);
	if (!playing) {
		[_lock unlock];
		return;
	}
	[_playingDict removeObjectForKey:media.name];

	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth |
		AVAudioSessionCategoryOptionMixWithOthers;

	if (!in_call && [_playingDict count] == 0) {
		info("setCategory called from: %s:%d\n", __FILE__, __LINE__);
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient
			withOptions:options error:nil];
	}
	[_lock unlock];
}

- (BOOL)isPlaying:(id<AVSMedia>)media
{
	[_lock lock];
	BOOL playing = ([_playingDict objectForKey:media.name] != nil);
	[_lock unlock];
	return  playing;
}

@end

PlayingTracker *_playingTracker = NULL;

static void update_device_status(struct mm *mm)
{
	bool bt_routes_available = false;
	bool hs_route_available = false;
    
	NSArray *availableRoutes = [[AVAudioSession sharedInstance] availableInputs];
	NSArray *bluetoothRoutes = @[AVAudioSessionPortBluetoothA2DP, AVAudioSessionPortBluetoothHFP];
	for ( AVAudioSessionPortDescription *route in availableRoutes ) {
		if ( [bluetoothRoutes containsObject:route.portType] ) {
			bt_routes_available = true;
		}
	}
	NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
	for ( AVAudioSessionPortDescription *route in outputs ) {
		if ([route.portType isEqualToString:AVAudioSessionPortHeadphones]){
			hs_route_available = true;
		}
	}
    
	if(bt_routes_available && !bt_device_connected){
		bt_device_connected = true;
		mediamgr_bt_device_connected(mm, true);
	}
	if(!bt_routes_available && bt_device_connected){
		bt_device_connected = false;
		mediamgr_bt_device_connected(mm, false);
	}
	if(hs_route_available && !headset_connected){
		headset_connected = true;
		mediamgr_headset_connected(mm, true);
	}
	if(!hs_route_available && headset_connected){
		headset_connected = false;
		mediamgr_headset_connected(mm, false);
	}
}

int mm_platform_init(struct mm *mm, struct dict *sounds)
{
	_mm = mm;

	_playingTracker = [[PlayingTracker alloc] init];
#if TARGET_OS_IPHONE

	mm_platform_exit_call();

	[[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock: ^(NSNotification *notification) {


		NSDictionary *interuptionDict = notification.userInfo;
		NSInteger routeChangeReason = [[interuptionDict valueForKey:AVAudioSessionRouteChangeReasonKey] integerValue];

		enum mediamgr_auplay route = mm_platform_get_route();
        
		switch (routeChangeReason) {
			case AVAudioSessionRouteChangeReasonUnknown:
				info("mediamgr: AVAudioSessionRouteChangeReasonUnknown\n");
				break;
                
			case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
				info("mediamgr: AVAudioSessionRouteChangeReasonNewDeviceAvailable\n");
				break;

			case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
				info("mediamgr: AVAudioSessionRouteChangeReasonOldDeviceUnavailable\n");
				break;

            case AVAudioSessionRouteChangeReasonCategoryChange:
                info("mediamgr: AVAudioSessionRouteChangeReasonCategoryChange\n");
                break;
                
			case AVAudioSessionRouteChangeReasonOverride:
				info("mediamgr: AVAudioSessionRouteChangeReasonOverride\n");
				break;
                
			case AVAudioSessionRouteChangeReasonWakeFromSleep:
				info("mediamgr: AVAudioSessionRouteChangeReasonWakeFromSleep\n");
				break;
                
            case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory:
                info("mediamgr: AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory\n");
                break;
                
			case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
				info("mediamgr: AVAudioSessionRouteChangeReasonRouteConfigurationChange\n");
				break;
                
			default:
                warning("mediamgr: unknown routechange reason \n");
				break;
		}
        
		info("mediamgr: route = %s \n", MMroute2Str(route));
        
		AVAudioSession* session = [AVAudioSession sharedInstance];
		debug("mediamgr: sample rate: %f \n", session.sampleRate);
		debug("mediamgr: IO buffer duration: %f \n ", session.IOBufferDuration);
		debug("mediamgr: output channels: %ld \n", session.outputNumberOfChannels);
		debug("mediamgr: input channels: %ld \n",session.inputNumberOfChannels);
		debug("mediamgr: output latency: %f \n", session.outputLatency);
		debug("mediamgr: input latency: %f \n", session.inputLatency);
        
		if(_mm){
			mediamgr_device_changed(_mm);
            
			update_device_status(_mm);
		}
        
		}];
    
    [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionMediaServicesWereLostNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock: ^(NSNotification *notification) {
    
        info("mediamgr: AVAudioSessionMediaServicesWereLostNotification recieved \n");
    }];

    [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock: ^(NSNotification *notification) {
        
        info("mediamgr: AVAudioSessionMediaServicesWereResetNotification recieved \n");
    }];
        
#endif
    
	return 0;
}

int mm_platform_free(struct mm *mm)
{
	_mm = NULL;
	return 0;
}

void mm_platform_play_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	[media play];
}

void mm_platform_pause_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	[media pause];
}

void mm_platform_resume_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	[media resume];
}

void mm_platform_stop_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	[media stop];
}

bool mm_platform_is_sound_playing(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;

	return [_playingTracker isPlaying: media];
}

int mm_platform_enable_speaker(void)
{
	info("mm_platform_ios: enable_speaker\n");
#if TARGET_OS_IPHONE
    //if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayAndRecord) {
    if(true){
        [[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
    } else {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSString* category = session.category;
        AVAudioSessionCategoryOptions options = session.categoryOptions;
        
        if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
            options |= AVAudioSessionCategoryOptionDefaultToSpeaker;
        } else {
            options = AVAudioSessionCategoryOptionDefaultToSpeaker;
        }

        info("setCategory called from: %s:%d\n", __FILE__, __LINE__);
            [session setCategory:AVAudioSessionCategoryPlayAndRecord
                 withOptions:options
                       error:nil];
    }
#else
	current_route = MEDIAMGR_AUPLAY_SPEAKER;
#endif
	return 0;
}

int mm_platform_enable_bt_sco(void)
{
#if TARGET_OS_IPHONE
    //if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayAndRecord) {
    if(true){
        [[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
    } else {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSString* category = session.category;
        AVAudioSessionCategoryOptions cur_options = session.categoryOptions;
        AVAudioSessionCategoryOptions options = session.categoryOptions;
        
        if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
            options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
        } else {
            options = AVAudioSessionCategoryOptionDefaultToSpeaker;
        }
        if(cur_options != options){
            info("setCategory called from: %s:%d\n", __FILE__, __LINE__);
                [session setCategory:AVAudioSessionCategoryPlayAndRecord
                 withOptions:options
                       error:nil];
        }
    }
	NSArray *availableRoutes = [[AVAudioSession sharedInstance] availableInputs];
	for ( AVAudioSessionPortDescription *route in availableRoutes ) {
		if ( [route.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ) {
			[[AVAudioSession sharedInstance] setPreferredInput:route error:nil];
		}
	}
#else
	current_route = MEDIAMGR_AUPLAY_BT;
#endif
	return 0;
}

int mm_platform_enable_earpiece(void)
{
#if TARGET_OS_IPHONE
    //if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayAndRecord) {
    if(true){
        [[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
    } else {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSString* category = session.category;
        AVAudioSessionCategoryOptions cur_options = session.categoryOptions;
        AVAudioSessionCategoryOptions options = cur_options;
        
        if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
            options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
        } else {
            options = AVAudioSessionCategoryOptionDefaultToSpeaker;
        }
        if(options != cur_options){
            info("setCategory called from: %s:%d\n", __FILE__, __LINE__);
            [session setCategory:AVAudioSessionCategoryPlayAndRecord
                     withOptions:options
                           error:nil];
        }
    }
#else
	current_route = MEDIAMGR_AUPLAY_EARPIECE;
#endif
	return 0;
}

int mm_platform_enable_headset(void)
{
#if TARGET_OS_IPHONE
    //if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayAndRecord) {
    if(true){
        [[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
    } else {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSString* category = session.category;
        AVAudioSessionCategoryOptions cur_options = session.categoryOptions;
        AVAudioSessionCategoryOptions options = cur_options;
        
        if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
            options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
        } else {
            options = AVAudioSessionCategoryOptionDefaultToSpeaker;
        }
	    if(options != cur_options){
            info("setCategory called from: %s:%d\n", __FILE__, __LINE__);
                [session setCategory:AVAudioSessionCategoryPlayAndRecord
                         withOptions:options
                               error:nil];
        }
    }
#else
	current_route = MEDIAMGR_AUPLAY_HEADSET;
#endif
	return 0;
}

enum mediamgr_auplay mm_platform_get_route(void)
{
#if TARGET_OS_IPHONE
	enum mediamgr_auplay route = MEDIAMGR_AUPLAY_EARPIECE;
	AVAudioSession *audioSession = [AVAudioSession sharedInstance];
	NSArray *outputs = nil;
	AVAudioSessionRouteDescription *currentRoute = nil;
	if ( ![audioSession isKindOfClass:[AVAudioSession class]] ) {
		// AVSError(@"Audio Session Object Is Invalid: %@, %@", audioSession, audioSession.class);
		route = MEDIAMGR_AUPLAY_EARPIECE;
		goto out;
	}

	currentRoute = [[audioSession currentRoute] retain];
	if ( ![currentRoute isKindOfClass:[AVAudioSessionRouteDescription class]] ) {
		// AVSError(@"Current Route Object Is Invalid: %@, %@", currentRoute, currentRoute.class);
		route = MEDIAMGR_AUPLAY_EARPIECE;
		goto out;
	}

	outputs = [[currentRoute outputs] retain];
	if ( ![outputs isKindOfClass:[NSArray class]] ) {
		// AVSError(@"Outputs Object Is Invalid: %@, %@", outputs, outputs.class);
		route = MEDIAMGR_AUPLAY_EARPIECE;
		goto out;
	}
	
	for ( AVAudioSessionPortDescription *output in outputs ) {
		if (output.portType == nil) {
			continue;
		}

		NSString *portType = [NSString stringWithString:output.portType];

		if ( [portType isEqualToString:AVAudioSessionPortBuiltInReceiver] ) {
			route = MEDIAMGR_AUPLAY_EARPIECE;
			goto out;
		}
		
		if ( [portType isEqualToString:AVAudioSessionPortHeadphones] ) {
			route = MEDIAMGR_AUPLAY_HEADSET;
			goto out;
		}
		
		if ( [portType isEqualToString:AVAudioSessionPortBuiltInSpeaker] ) {
			route = MEDIAMGR_AUPLAY_SPEAKER;
			goto out;
		}
        
		if ( [portType isEqualToString:AVAudioSessionPortBluetoothHFP] ) {
			route = MEDIAMGR_AUPLAY_BT;
			goto out;
		}
        
		if ( [portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ) {
			route = MEDIAMGR_AUPLAY_BT;
			goto out;
		}
	}

out:
	[outputs release];
	[currentRoute release];
	
	return route;

#elif TARGET_OS_MAC
	return current_route;
#endif
}

void mm_platform_enter_call(void){
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth;
	NSError *err = nil;
 
	info("mm_platform_enter_call() \n");

	if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayAndRecord) {
		options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
		
		info("setCategory called from: %s:%d\n", __FILE__, __LINE__);	
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord
			withOptions:options error:nil];

		[[AVAudioSession sharedInstance] setMode:AVAudioSessionModeVoiceChat error:&err];
		if (err.code != 0) {
			error("%s: couldn't set session's audio mode: %ld",
				__FUNCTION__, (long)err.code);
		}
		//info("setActive called \n"); SSJ for callKit maybe Activate as sometimes it is not activated
		//[[AVAudioSession sharedInstance] setActive:YES error:&err];
        
		//[[AVAudioSession sharedInstance] setPreferredSampleRate:16000 error:&err];
		//if (err.code != 0) {
		//	error("%s: couldn't set session's preferred sample rate: %ld",
		//		__FUNCTION__, (long)err.code);
		//}
        
		usleep(1000000);
	}
	in_call = true;
}

void mm_platform_exit_call(void){
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth |
		AVAudioSessionCategoryOptionMixWithOthers;
	NSError *err = nil;

	info("mm_platform_exit_call() \n");

	if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryAmbient &&
		[[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayback) {
		info("setCategory called from: %s:%d\n", __FILE__, __LINE__);   
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient
			withOptions:options error:nil];
        
		[[AVAudioSession sharedInstance] setMode:AVAudioSessionModeDefault error:&err];
		if (err.code != 0) {
			error("%s: couldn't set session's audio mode: %ld",
				__FUNCTION__, (long)err.code);
		}
	}
	in_call = false;
}

void mm_platform_set_active(void){
	NSError *err = nil;
    
    info("mm_platform_set_active ! \n");
    [[AVAudioSession sharedInstance] setActive:YES error:&err];    
}

void mm_platform_registerMedia(struct dict *sounds,
				const char *name,
				void *mediaObj,
				bool mixing,
				bool incall,
				int intensity,
				int priority,
				bool is_call_media)
{
	debug("mm_platform_registerMedia name = %s obj = %p \n", name, mediaObj);
	
	struct sound *snd;
	
	snd = mem_zalloc(sizeof(struct sound), NULL);
	
	str_dup((char **)&snd->path, name);
	snd->arg = mediaObj;
	snd->mixing = mixing;
	snd->incall = incall;
	snd->intensity = intensity;
	snd->priority = priority;
	snd->is_call_media = is_call_media;

	info("mm_platform_ios: registerMedia: %s mixing=%d incall=%d int=%d "
	     "prio=%d is_call=%d\n",
	     name, mixing, incall, intensity, priority, is_call_media);
	
	dict_add(sounds, name, (void*)snd);
	mem_deref(snd); // to get the ref count to 1

	id<AVSMedia> avs_media = mediaObj;

	avs_media.delegate = _playingTracker;

	if ([avs_media respondsToSelector:@selector(setSound:)]) {
		[avs_media setSound:snd];
	}
}

void mm_platform_unregisterMedia(struct dict *sounds, const char *name){
	struct sound *snd = dict_lookup(sounds, name);
	if (snd) {
		mem_deref((void*)snd->path);
		id media = (id)snd->arg;
		CFRelease(media);
	}
	dict_remove(sounds, name);
}


