
#import <sdk/objc/components/audio/RTCAudioSessionConfiguration.h>

#include <re.h>
#include <avs.h>
#include "peerflow.h"


int pc_platform_init(void)
{
	NSLog(@"pc_ios_init");

	[RTCAudioSessionConfiguration webRTCConfiguration];
	
#if 0	
	RTCAudioSessionConfiguration *configuration =
		RTCAudioSessionConfiguration setWebRTCConfiguration:configuration];
		
	[RTCAudioSessionConfiguration setWebRTCConfiguration:configuration];
#endif

	return 0
}
