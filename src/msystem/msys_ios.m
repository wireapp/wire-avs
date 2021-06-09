#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include "re.h"

#include "msystem.h"


bool msys_platform_is_active()
{
	UIApplication *app;

	app = [UIApplication sharedApplication];
	
	return app.applicationState == UIApplicationStateActive;
}
