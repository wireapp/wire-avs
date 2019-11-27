
#import <sdk/objc/Framework/Headers/WebRTC/RTCAudioSessionConfiguration.h>

#include <re.h>
#include <avs.h>
#include "peerflow.h"


int pc_platform_init(void)
{
	NSLog(@"pc_ios_init");
	
	RTCAudioSessionConfiguration *configuration =
		[[RTCAudioSessionConfiguration alloc] init];

	[RTCAudioSessionConfiguration setWebRTCConfiguration:configuration];

	return 0;
}
