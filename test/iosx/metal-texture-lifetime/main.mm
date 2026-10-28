/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

/*
 * Texture lifetime test for the iOS Metal video renderer.
 *
 * Compiles the production AVSVideoView_Metal.mm against real Metal and a
 * real CAMetalLayer; only the UIKit pieces and AVSFlowManager are stubbed.
 *
 * The device's -setFragmentTexture:atIndex: is hooked so that, while
 * render: binds the U plane, a decoder thread delivers a frame with a new
 * resolution through handleFrame:. The hook runs after render: has picked
 * the texture and before Metal retains it. A weak reference then tells
 * whether the texture handed to Metal is still alive.
 */

#import "AVSVideoView.h"
#import "AVSFlowManager.h"
#include "avs.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <unistd.h>


@implementation UIView

- (instancetype)init
{
	self = [super init];
	if (self) {
		CAMetalLayer *layer = [CAMetalLayer layer];

		layer.device = MTLCreateSystemDefaultDevice();
		layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		_layer = layer;
		_bounds = CGRectMake(0, 0, 64, 48);
		_frame = _bounds;
	}
	return self;
}

@end


@implementation CADisplayLink

+ (instancetype)displayLinkWithTarget:(id)target selector:(SEL)selector
{
	return [self new];
}

- (void)addToRunLoop:(NSRunLoop *)loop forMode:(NSRunLoopMode)mode
{
}

- (void)invalidate
{
}

@end


@implementation UIScreen

+ (instancetype)mainScreen
{
	return [self new];
}

- (CGFloat)scale
{
	return 1;
}

@end


@implementation AVSFlowManager

+ (instancetype)getInstance
{
	return nil;
}

- (void)attachVideoView:(UIView *)view
{
}

- (void)detachVideoView:(UIView *)view
{
}

@end


static uint8_t small_y[64 * 48], small_u[32 * 24], small_v[32 * 24];
static uint8_t large_y[128 * 96], large_u[64 * 48], large_v[64 * 48];
static struct avs_vidframe small_frame, large_frame;

static AVSVideoView *race_view;
static BOOL race_armed;
static BOOL race_hit;
static BOOL race_texture_alive;
static dispatch_semaphore_t race_decoder_done;
static IMP orig_set_fragment_texture;

static unsigned failures;


static void check(BOOL ok, const char *name)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok)
		failures++;
}


static void init_frame(struct avs_vidframe *f, int w, int h,
		       uint8_t *y, uint8_t *u, uint8_t *v)
{
	f->type = AVS_VIDFRAME_I420;
	f->w = w;
	f->h = h;
	f->y = y;
	f->u = u;
	f->v = v;
	f->ys = w;
	f->us = w / 2;
	f->vs = w / 2;
}


static void *texture_ivar(AVSVideoView *view, const char *name)
{
	Ivar ivar = class_getInstanceVariable([AVSVideoView class], name);

	return *(void **)((char *)(__bridge void *)view + ivar_getOffset(ivar));
}


static AVSVideoView *running_view(void)
{
	AVSVideoView *view = [AVSVideoView new];

	[view layoutSubviews];
	[view startRunning];
	[view handleFrame:&small_frame];

	return view;
}


static void hooked_set_fragment_texture(id encoder, SEL cmd,
					__unsafe_unretained id texture,
					NSUInteger index)
{
	if (race_armed && index == 1) {
		__weak id bound = texture;

		race_armed = NO;
		race_hit = YES;

		race_decoder_done = dispatch_semaphore_create(0);
		dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
			[race_view handleFrame:&large_frame];
			dispatch_semaphore_signal(race_decoder_done);
		});
		/* A renderer holding _lock while binding blocks the decoder;
		 * don't wait for it forever.
		 */
		if (dispatch_semaphore_wait(race_decoder_done,
			dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC)) == 0)
			dispatch_semaphore_signal(race_decoder_done);

		race_texture_alive = bound != nil;

		/* Never hand Metal a freed object. */
		if (!race_texture_alive)
			texture = nil;
	}

	((void (*)(id, SEL, id, NSUInteger))orig_set_fragment_texture)
		(encoder, cmd, texture, index);
}


static void hook_encoder(id<MTLDevice> device)
{
	MTLTextureDescriptor *desc;
	MTLRenderPassDescriptor *pass;
	id<MTLCommandBuffer> buffer;
	id<MTLRenderCommandEncoder> encoder;
	Method method;

	desc = [MTLTextureDescriptor
		texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
					     width:4
					    height:4
					 mipmapped:NO];
	desc.usage = MTLTextureUsageRenderTarget;
	pass = [MTLRenderPassDescriptor new];
	pass.colorAttachments[0].texture = [device newTextureWithDescriptor:desc];

	buffer = [[device newCommandQueue] commandBuffer];
	encoder = [buffer renderCommandEncoderWithDescriptor:pass];
	method = class_getInstanceMethod(object_getClass(encoder),
					 @selector(setFragmentTexture:atIndex:));
	[encoder endEncoding];

	orig_set_fragment_texture =
		method_setImplementation(method, (IMP)hooked_set_fragment_texture);
}


static void test_bound_texture_survives_resize(id<MTLDevice> device)
{
	hook_encoder(device);

	race_view = running_view();
	race_armed = YES;
	[race_view render:nil];

	if (race_hit)
		dispatch_semaphore_wait(race_decoder_done, DISPATCH_TIME_FOREVER);

	check(race_hit, "render: binds the U texture");
	check(race_hit && race_texture_alive,
	      "texture bound by render: survives a concurrent resolution change");

	[race_view stopRunning];
	usleep(100 * 1000); /* let the submitted command buffer complete */
}


static void test_same_size_keeps_textures(void)
{
	AVSVideoView *view = running_view();
	void *y = texture_ivar(view, "_yTexture");
	void *u = texture_ivar(view, "_uTexture");
	void *v = texture_ivar(view, "_vTexture");
	BOOL same;

	view.shouldFill = !view.shouldFill;
	[view handleFrame:&small_frame];
	same = texture_ivar(view, "_yTexture") == y
	    && texture_ivar(view, "_uTexture") == u
	    && texture_ivar(view, "_vTexture") == v;
	check(same, "scaling change keeps the textures");

	[view layoutSubviews];
	[view handleFrame:&small_frame];
	same = texture_ivar(view, "_yTexture") == y
	    && texture_ivar(view, "_uTexture") == u
	    && texture_ivar(view, "_vTexture") == v;
	check(same, "layout change keeps the textures");

	[view handleFrame:&large_frame];
	check(texture_ivar(view, "_uTexture") != u,
	      "resolution change replaces the textures");

	[view stopRunning];
}


int main(void)
{
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();

		if (!device) {
			printf("SKIP no Metal device\n");
			return 0;
		}

		init_frame(&small_frame, 64, 48, small_y, small_u, small_v);
		init_frame(&large_frame, 128, 96, large_y, large_u, large_v);

		test_same_size_keeps_textures();
		test_bound_texture_survives_resize(device);
	}

	printf("RESULT %u failure(s)\n", failures);

	return failures ? 1 : 0;
}
