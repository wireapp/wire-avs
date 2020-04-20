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

#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include "view_internal.h"
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>

#import "../../iosx/include/AVSCapturer.h"
#import "../../iosx/include/AVSVideoViewOSX.h"

#include "cli.h"
#include "view.h"


#define WIN_W         1280
#define WIN_H          960

static struct {
	NSWindow *win;
	NSMutableArray  *views;
	NSView *preview;
	NSTimer *timer;

	AVSCapturer *capturer;
	bool preview_visible;
	bool view_visible;

	NSString *local_userid;
	NSString *local_clientid;
	struct tmr tmr;
} vidloop;

int osx_view_init(struct view** v);
static void osx_arrange_views(void);

@interface VideoDelegate : NSObject
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification;
@end


static void tmr_handler(void *arg)
{
	(void)arg;

	tmr_start(&vidloop.tmr, 100, tmr_handler, 0);

	/* Simulate the Run-Loop */
	(void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, YES);
}

static void osx_runloop_start(void)
{
	tmr_start(&vidloop.tmr, 10, tmr_handler, 0);
}


static void osx_runloop_stop(void)
{
	tmr_cancel(&vidloop.tmr);
}



static void osx_view_close(void)
{
	[vidloop.win close];

	vidloop.preview = nil;
	vidloop.win = nil;
}

static void osx_view_show(void)
{
	NSApplication *app = [NSApplication sharedApplication];
	[vidloop.win makeKeyAndOrderFront:app];
	[NSApp activateIgnoringOtherApps:YES];
	[vidloop.preview display];
	osx_arrange_views();

}


static void osx_view_hide(void)
{
	[vidloop.win orderOut:nil];
	osx_arrange_views();
}


static void osx_arrange_views(void)
{
	uint32_t vcount, i, v;
	uint32_t rows, cols, w, h, vh;
	uint32_t xp, yp;

	vcount = vidloop.views.count + (vidloop.preview_visible ? 1 : 0);

	if (vcount == 0)
		return;

	i = 1;
	while(i * (i + 1) < vcount) {
		i++;
	}

	rows = i;
	cols = ((vcount - 1) / rows) + 1;

	w = WIN_W / cols;
	h = WIN_H / rows;
	vh = w * 3 / 4;

	for (v = 0; v < vidloop.views.count; v++) {
		xp = (v % cols) * w;
		yp = WIN_H - h - ((v / cols) * h) + (h - vh) / 2; 

		NSRect rect = NSMakeRect(xp, yp, w, vh);
		AVSVideoViewOSX *oview = [vidloop.views objectAtIndex: v];
		oview.frame = rect;
	}

	vh = w * 3 / 4;
	xp = (cols - 1) * w;
	yp = (h - vh) / 2; 
	NSRect rect = NSMakeRect(xp, yp, w, vh);
	vidloop.preview.frame = rect;

	NSApplication *app = [NSApplication sharedApplication];
	[vidloop.win makeKeyAndOrderFront:app];
	[NSApp activateIgnoringOtherApps:YES];

	[[vidloop.win contentView] addSubview:vidloop.preview];
	[vidloop.preview display];
}

static void osx_vidstate_changed(const char *userid, const char *clientid, int state)
{
	NSString *uid = [NSString stringWithUTF8String: userid];
	NSString *cid = [NSString stringWithUTF8String: clientid];

	dispatch_async(dispatch_get_main_queue(), ^{
		bool found = false;
		if (![vidloop.local_userid isEqualToString: uid] ||
		    ![vidloop.local_clientid isEqualToString: cid]) {
			switch(state) {
			case WCALL_VIDEO_STATE_STARTED:
			case WCALL_VIDEO_STATE_SCREENSHARE:
				for (unsigned int v = 0; v < vidloop.views.count; v++) {
					AVSVideoViewOSX *view = [vidloop.views objectAtIndex: v];

					if ([view.userid isEqualToString: uid] &&
						[view.clientid isEqualToString: cid]) {
						found = true;
						break;
					}
				}

				if (!found) {
					NSRect rect = NSMakeRect(0, 0, WIN_W, WIN_W * 3 / 4);
					AVSVideoViewOSX *v = [[AVSVideoViewOSX alloc] initWithFrame:rect];
					v.userid = uid;
					v.clientid = cid;
					[vidloop.views addObject: v];
					[[vidloop.win contentView] addSubview:v];
					[v display];

					info("osx_view adding renderer for %s now %u\n",
						[uid UTF8String], vidloop.views.count);
				}
				break;

			default:
				{

					info("osx_view removing renderer for %s\n",
						[uid UTF8String]);
					for (unsigned int v = 0; v < vidloop.views.count; v++) {
						AVSVideoViewOSX *view = [vidloop.views objectAtIndex: v];

						if ([view.userid isEqualToString: uid] &&
							[view.clientid isEqualToString: cid]) {

							[view removeFromSuperview];
							[vidloop.views removeObject: view];
							info("osx_view removing renderer for %s now %u\n",
								userid, vidloop.views.count);
							break;
						}
					}
				}
				break;
			}
		}
		vidloop.view_visible = [vidloop.views count] > 0;


		if (vidloop.view_visible || vidloop.preview_visible) {
			view_show();
		}
		else {
			view_hide();
		}
		osx_arrange_views();
	}); 
}

static int osx_render_frame(struct avs_vidframe * frame,
			    const char *userid,
			    const char *clientid)
{
	NSString *uid = [NSString stringWithUTF8String: userid];
	NSString *cid = [NSString stringWithUTF8String: clientid];

	for (unsigned int v = 0; v < vidloop.views.count; v++) {
		AVSVideoViewOSX *view = [vidloop.views objectAtIndex: v];

		if ([view.userid isEqualToString: uid] &&
			[view.clientid isEqualToString: cid]) {
			[view handleFrame:frame];
			return 0;
		}
	}
/*
	NSRect rect = NSMakeRect(0, 0, WIN_W, WIN_W * 3 / 4);
	AVSVideoViewOSX *v = [[AVSVideoViewOSX alloc] initWithFrame:rect];
	v.userid = uid;
	v.clientid = cid;
	[vidloop.views addObject: v];
	[[vidloop.win contentView] addSubview:v];
	[v display];

	info("osx_view adding renderer for %s now %u\n",
		[uid UTF8String], vidloop.views.count);
	osx_arrange_views();
	[v handleFrame:frame];
*/
	return 0;
}

static void osx_preview_start(void)
{
	info("osx_preview_start\n");

	[vidloop.capturer attachPreview:vidloop.preview];
	[vidloop.capturer startWithWidth: 640 Height: 480 MaxFps: 15];

	vidloop.preview_visible = true;
	view_show();
	osx_arrange_views();
}

static void osx_preview_stop(void)
{
	[vidloop.capturer detachPreview:vidloop.preview];
	[vidloop.capturer stop];

	vidloop.preview_visible = false;
	if (!vidloop.view_visible) {
		view_hide();
	}
	else {
		osx_arrange_views();
	}
}

static void osx_view_set_local_user(const char *userid, const char *clientid)
{
	vidloop.local_userid = [NSString stringWithUTF8String: userid];
	vidloop.local_clientid = [NSString stringWithUTF8String: clientid];
}

static struct view _view = {
	.runloop_start = osx_runloop_start,
	.runloop_stop = osx_runloop_stop,
	.view_close = osx_view_close,
	.view_show = osx_view_show,
	.view_hide = osx_view_hide,
	.set_local_user = osx_view_set_local_user,
	.vidstate_changed = osx_vidstate_changed,
	.render_frame = osx_render_frame,
	.preview_start = osx_preview_start,
	.preview_stop = osx_preview_stop
};


int osx_view_init(struct view** v)
{
	NSApplication *app = [NSApplication sharedApplication];

	vidloop.win = [[NSWindow alloc]
		       initWithContentRect:NSMakeRect(0, 0, WIN_W, WIN_H)
		       styleMask:NSTitledWindowMask
		       | NSClosableWindowMask
		       | NSMiniaturizableWindowMask
		       backing:NSBackingStoreBuffered
		       defer:NO];

	vidloop.win.backgroundColor = [NSColor colorWithCalibratedRed:0.0f
		green:0.0f blue:0.0f alpha:1.0f];
	[vidloop.win orderOut:nil];

	vidloop.views = [[NSMutableArray alloc] init];

	NSRect previewRect = NSMakeRect(0, 0, WIN_W, WIN_H);
	vidloop.preview = [[NSView alloc] initWithFrame:previewRect];

	[[vidloop.win contentView] addSubview:vidloop.preview];
	[vidloop.preview display];

	VideoDelegate *videoDelegate = [VideoDelegate new];
	app.delegate = (id)videoDelegate;

	vidloop.preview_visible = vidloop.view_visible = false;

	osx_view_hide();
	vidloop.capturer = [[AVSCapturer alloc] init];

	osx_arrange_views();
	*v = &_view;
	return 0;
}


@implementation VideoDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	re_printf("*** didFinishLaunching\n");
}
@end
