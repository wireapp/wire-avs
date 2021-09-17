
#import <sdk/objc/components/audio/RTCAudioSessionConfiguration.h>

#include <re.h>
#include <avs.h>
#include "peerflow.h"


int pc_platform_init(void)
{
	NSLog(@"pc_ios_init");
	
	RTCAudioSessionConfiguration *configuration =
		[[[RTCAudioSessionConfiguration alloc] init] autorelease];

	[RTCAudioSessionConfiguration setWebRTCConfiguration:configuration];

	return 0;
}
