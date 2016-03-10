

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
static struct mediamgr *_mm = NULL;

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
	[_playingDict removeObjectForKey:media.name];
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

int mm_platform_init(struct mediamgr *mm, struct dict *sounds)
{
	_mm = mm;

	_playingTracker = [[PlayingTracker alloc] init];
#if TARGET_OS_IPHONE

	mm_platform_exit_call();

	[[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock: ^(NSNotification *notification) {


		NSDictionary *interuptionDict = notification.userInfo;
		NSInteger routeChangeReason = [[interuptionDict valueForKey:AVAudioSessionRouteChangeReasonKey] integerValue];

		switch (routeChangeReason) {
				case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
				mediamgr_headset_connected(_mm, true);
				break;

			case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
				mediamgr_headset_connected(_mm, false);
				break;

			default:
				break;
		}
		}];
    
    
    // Make sure that the headset connected state is correct
    NSArray *outputs = [[AVAudioSession sharedInstance] currentRoute].outputs;
    AVAudioSessionPortDescription *outPortDesc = [outputs objectAtIndex:0];
    
    if ([outPortDesc.portType isEqualToString:AVAudioSessionPortHeadphones]){
        mediamgr_headset_connected(_mm, true);
    } else {
        mediamgr_headset_connected(_mm, false);
    }
    
#endif
    
	return 0;
}

int mm_platform_free(struct mediamgr *mm)
{
	return 0;
}

void mm_platform_play_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	NSString *cat;
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth;

	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	if (snd->is_call_media) {
		cat = AVAudioSessionCategoryPlayAndRecord;
	}
	else if (snd->mixing) {
		cat = AVAudioSessionCategoryAmbient;
		options |= AVAudioSessionCategoryOptionMixWithOthers;
	}
	else {
		cat = AVAudioSessionCategorySoloAmbient;
	}

	[[AVAudioSession sharedInstance] setCategory: cat withOptions: options error:nil];
	dispatch_sync(dispatch_get_main_queue(),^{
		[media play];
	});
}

void mm_platform_pause_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	dispatch_sync(dispatch_get_main_queue(),^{
		[media pause];
	});
}

void mm_platform_resume_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	dispatch_sync(dispatch_get_main_queue(),^{
		[media play];
	});
}

void mm_platform_stop_sound(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth |
		AVAudioSessionCategoryOptionMixWithOthers;

	info("%s: %s %s\n", __FUNCTION__, snd->path, snd->is_call_media ? "(call)" : "");
	dispatch_sync(dispatch_get_main_queue(),^{
		[media stop];
	});
	if (!in_call) {
		[[AVAudioSession sharedInstance] setCategory: AVAudioSessionCategoryAmbient withOptions:options error:nil];
	}
}

bool mm_platform_is_sound_playing(struct sound *snd)
{
	id<AVSMedia> media = snd->arg;

	return [_playingTracker isPlaying: media];
}

int mm_platform_enable_speaker(void)
{
#if TARGET_OS_IPHONE
	AVAudioSession* session = [AVAudioSession sharedInstance];
	NSString* category = session.category;
	AVAudioSessionCategoryOptions options = session.categoryOptions;

	if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
		options |= AVAudioSessionCategoryOptionDefaultToSpeaker;
	} else {
		options = AVAudioSessionCategoryOptionDefaultToSpeaker;
	}
	[session setCategory:AVAudioSessionCategoryPlayAndRecord
					withOptions:options
					error:nil];
#else
	current_route = MEDIAMGR_AUPLAY_SPEAKER;
#endif
	return 0;
}

int mm_platform_enable_bt_sco(void)
{
#if TARGET_OS_IPHONE
	AVAudioSession* session = [AVAudioSession sharedInstance];
	NSString* category = session.category;
	AVAudioSessionCategoryOptions options = session.categoryOptions;
    
	if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
		options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
	} else {
		options = AVAudioSessionCategoryOptionDefaultToSpeaker;
	}
	[session setCategory:AVAudioSessionCategoryPlayAndRecord
					withOptions:options
					error:nil];
#else
	current_route = MEDIAMGR_AUPLAY_BT;
#endif
	return 0;
}

int mm_platform_enable_earpiece(void)
{
#if TARGET_OS_IPHONE
	AVAudioSession* session = [AVAudioSession sharedInstance];
	NSString* category = session.category;
	AVAudioSessionCategoryOptions options = session.categoryOptions;
    
	if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
		options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
	} else {
		options = AVAudioSessionCategoryOptionDefaultToSpeaker;
	}
	[session setCategory:AVAudioSessionCategoryPlayAndRecord
					withOptions:options
					error:nil];
#else
	current_route = MEDIAMGR_AUPLAY_EARPIECE;
#endif
	return 0;
}

int mm_platform_enable_headset(void)
{
#if TARGET_OS_IPHONE
	AVAudioSession* session = [AVAudioSession sharedInstance];
	NSString* category = session.category;
	AVAudioSessionCategoryOptions options = session.categoryOptions;
    
	if ([category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
		options &= ~AVAudioSessionCategoryOptionDefaultToSpeaker;
	} else {
		options = AVAudioSessionCategoryOptionDefaultToSpeaker;
	}
	[session setCategory:AVAudioSessionCategoryPlayAndRecord
					withOptions:options
					error:nil];
#else
	current_route = MEDIAMGR_AUPLAY_HEADSET;
#endif
	return 0;
}

enum mediamgr_auplay mm_platform_get_route(void)
{
#if TARGET_OS_IPHONE
	AVAudioSession *audioSession = [AVAudioSession sharedInstance];
	if ( ![audioSession isKindOfClass:[AVAudioSession class]] ) {
		// AVSError(@"Audio Session Object Is Invalid: %@, %@", audioSession, audioSession.class);
		return MEDIAMGR_AUPLAY_EARPIECE;
	}

	AVAudioSessionRouteDescription *currentRoute = [audioSession currentRoute];
	if ( ![currentRoute isKindOfClass:[AVAudioSessionRouteDescription class]] ) {
		// AVSError(@"Current Route Object Is Invalid: %@, %@", currentRoute, currentRoute.class);
		return MEDIAMGR_AUPLAY_EARPIECE;
	}

	NSArray *outputs = [currentRoute outputs];
	if ( ![outputs isKindOfClass:[NSArray class]] ) {
		// AVSError(@"Outputs Object Is Invalid: %@, %@", outputs, outputs.class);
		return MEDIAMGR_AUPLAY_EARPIECE;
	}
	
	for ( AVAudioSessionPortDescription *output in outputs ) {
	NSString *portType = output.portType;
	if (portType == nil)
		continue;

		if ( [portType isEqualToString:AVAudioSessionPortBuiltInReceiver] ) {
			return MEDIAMGR_AUPLAY_EARPIECE;
		}
		
		if ( [portType isEqualToString:AVAudioSessionPortHeadphones] ) {
			return MEDIAMGR_AUPLAY_HEADSET;
		}
		
		if ( [portType isEqualToString:AVAudioSessionPortBuiltInSpeaker] ) {
			return MEDIAMGR_AUPLAY_SPEAKER;
		}
	}
	
	return MEDIAMGR_AUPLAY_EARPIECE;

#elif TARGET_OS_MAC
	return current_route;
#endif
}

void mm_platform_enter_call(void){
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth;

	info("mm_platform_enter_call() \n");

	if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryPlayAndRecord) {
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord
			withOptions:options error:nil];
	}
	in_call = true;
}

void mm_platform_exit_call(void){
	AVAudioSessionCategoryOptions options =
		AVAudioSessionCategoryOptionAllowBluetooth |
		AVAudioSessionCategoryOptionMixWithOthers;

	info("mm_platform_exit_call() \n");

	if ([[AVAudioSession sharedInstance] category] != AVAudioSessionCategoryAmbient) {
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient
			withOptions:options error:nil];
	}
	in_call = false;
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
	
	snd->path = strdup(name);
	snd->arg = mediaObj;
	snd->mixing = mixing;
	snd->incall = incall;
	snd->intensity = intensity;
	snd->priority = priority;
	snd->is_call_media = is_call_media;
	
	dict_add(sounds, name, (void*)snd);
	mem_deref(snd); // to get the ref count to 1

	id<AVSMedia> avs_media = mediaObj;

	avs_media.delegate = _playingTracker;
}

void mm_platform_unregisterMedia(struct dict *sounds, const char *name){
	struct sound *snd = dict_lookup(sounds, name);
	if (snd) {
		id media = (id)snd->arg;
		CFRelease(media);
	}
	dict_remove(sounds, name);
}


