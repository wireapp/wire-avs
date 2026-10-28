/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "AVSVideoView.h"
#import "AVSFlowManager.h"
#include "avs.h"
#include <stdio.h>
#import <dispatch/dispatch.h>
#import <objc/runtime.h>

static NSUInteger uploads, submitted, violations, allocatedTextures;
static BOOL missingDrawable, missingBuffer, missingEncoder;
static NSMutableArray *pending;
static NSMutableArray<NSNumber *> *boundWidths;
static void (^bindingHook)(void);

@interface FakeTexture : NSObject
@property NSUInteger width;
@property NSUInteger height;
@property MTLPixelFormat pixelFormat;
@property NSUInteger gpuReaders;
- (void)replaceRegion:(MTLRegion)region
          mipmapLevel:(NSUInteger)level
            withBytes:(const void *)bytes
          bytesPerRow:(NSUInteger)stride;
@end

@implementation FakeTexture
- (void)replaceRegion:(MTLRegion)region
          mipmapLevel:(NSUInteger)level
            withBytes:(const void *)bytes
          bytesPerRow:(NSUInteger)stride
{
    uploads++;
    if (self.gpuReaders > 0)
        violations++;
}
@end

@interface FakeBuffer : NSObject
{
    NSMutableData *_bytes;
}
@property NSUInteger gpuReaders;
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length;
- (void *)contents;
@end

@implementation FakeBuffer
- (instancetype)initWithBytes:(const void *)bytes length:(NSUInteger)length
{
    self = [super init];
    if (self)
        _bytes = [NSMutableData dataWithBytes:bytes length:length];
    return self;
}

- (void *)contents
{
    if (self.gpuReaders > 0)
        violations++;
    return _bytes.mutableBytes;
}
@end

@interface FakeEncoder : NSObject
@property (copy) NSString *label;
@property (strong) FakeBuffer *vertexBuffer;
@property (strong) NSMutableArray<FakeTexture *> *textures;
- (void)setRenderPipelineState:(id)state;
- (void)setVertexBuffer:(id)buffer
                 offset:(NSUInteger)offset
                atIndex:(NSUInteger)index;
- (void)setFragmentTexture:(id)texture atIndex:(NSUInteger)index;
- (void)drawPrimitives:(MTLPrimitiveType)type
           vertexStart:(NSUInteger)start
           vertexCount:(NSUInteger)count
         instanceCount:(NSUInteger)instances;
- (void)endEncoding;
@end

@implementation FakeEncoder
- (void)setRenderPipelineState:(id)state
{
}

- (void)setVertexBuffer:(id)buffer
                 offset:(NSUInteger)offset
                atIndex:(NSUInteger)index
{
    self.vertexBuffer = buffer;
}

- (void)setFragmentTexture:(id)texture atIndex:(NSUInteger)index
{
    if (index == 0)
    {
        boundWidths = [NSMutableArray new];
        self.textures = [NSMutableArray new];
    }
    [self.textures addObject:texture];
    [boundWidths addObject:@([(FakeTexture *)texture width])];
    if (index == 0 && bindingHook)
    {
        void (^hook)(void) = bindingHook;
        bindingHook = nil;
        hook();
    }
}

- (void)drawPrimitives:(MTLPrimitiveType)type
           vertexStart:(NSUInteger)start
           vertexCount:(NSUInteger)count
         instanceCount:(NSUInteger)instances
{
}

- (void)endEncoding
{
}
@end

@interface FakeCommandBuffer : NSObject
@property (copy) NSString *label;
@property (copy) void (^handler)(id);
@property (strong) FakeEncoder *encoder;
- (void)addCompletedHandler:(void (^)(id))handler;
- (id)renderCommandEncoderWithDescriptor:(id)descriptor;
- (void)presentDrawable:(id)drawable;
- (void)commit;
- (void)complete;
@end

@implementation FakeCommandBuffer
- (void)addCompletedHandler:(void (^)(id))handler
{
    self.handler = handler;
}

- (id)renderCommandEncoderWithDescriptor:(id)descriptor
{
    self.encoder = missingEncoder ? nil : [FakeEncoder new];
    return self.encoder;
}

- (void)presentDrawable:(id)drawable
{
}

- (void)commit
{
    submitted++;
    self.encoder.vertexBuffer.gpuReaders++;
    for (FakeTexture *texture in self.encoder.textures)
        texture.gpuReaders++;
    [pending addObject:self];
}

- (void)complete
{
    self.encoder.vertexBuffer.gpuReaders--;
    for (FakeTexture *texture in self.encoder.textures)
        texture.gpuReaders--;
    void (^handler)(id) = self.handler;
    self.handler = nil;
    if (handler)
        handler(self);
}
@end

@interface FakeQueue : NSObject
- (id)commandBuffer;
@end

@implementation FakeQueue
- (id)commandBuffer
{
    return missingBuffer ? nil : [FakeCommandBuffer new];
}
@end

@interface FakeFunction : NSObject
@property MTLFunctionType functionType;
@end

@implementation FakeFunction
@end

@interface FakeLibrary : NSObject
- (id)newFunctionWithName:(NSString *)name;
@end

@implementation FakeLibrary
- (id)newFunctionWithName:(NSString *)name
{
    FakeFunction *function = [FakeFunction new];
    function.functionType = [name hasPrefix:@"vertex"]
        ? MTLFunctionTypeVertex
        : MTLFunctionTypeFragment;
    return function;
}
@end

@interface FakeDevice : NSObject
- (id)newLibraryWithSource:(NSString *)source
                   options:(id)options
                     error:(NSError **)error;
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor
                                     error:(NSError **)error;
- (id)newCommandQueue;
- (id)newBufferWithBytes:(const void *)bytes
                  length:(NSUInteger)length
                 options:(MTLResourceOptions)options;
- (id)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
@end

@implementation FakeDevice
- (id)newLibraryWithSource:(NSString *)source
                   options:(id)options
                     error:(NSError **)error
{
    return [FakeLibrary new];
}

- (id)newRenderPipelineStateWithDescriptor:(id)descriptor
                                     error:(NSError **)error
{
    return [NSObject new];
}

- (id)newCommandQueue
{
    return [FakeQueue new];
}

- (id)newBufferWithBytes:(const void *)bytes
                  length:(NSUInteger)length
                 options:(MTLResourceOptions)options
{
    return [[FakeBuffer alloc] initWithBytes:bytes length:length];
}

- (id)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor
{
    allocatedTextures++;
    FakeTexture *texture = [FakeTexture new];
    texture.width = descriptor.width;
    texture.height = descriptor.height;
    texture.pixelFormat = descriptor.pixelFormat;
    return texture;
}
@end
id<MTLDevice> MTLCreateSystemDefaultDevice(void)
{
    return (id<MTLDevice>)[FakeDevice new];
}

@interface FakeDrawable : NSObject
@property (strong) id<MTLTexture> texture;
@end

@implementation FakeDrawable
@end

@interface FakeMetalLayer : CALayer
@property CGSize drawableSize;
- (id<CAMetalDrawable>)nextDrawable;
@end

@implementation FakeMetalLayer
- (id<CAMetalDrawable>)nextDrawable
{
    if (missingDrawable)
        return nil;
    FakeDrawable *drawable = [FakeDrawable new];
    FakeTexture *texture = [FakeTexture new];
    texture.width = 640;
    texture.height = 480;
    drawable.texture = (id<MTLTexture>)texture;
    return (id<CAMetalDrawable>)drawable;
}
@end

@implementation UIView
- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _layer = [FakeMetalLayer layer];
        _bounds = CGRectMake(0, 0, 640, 480);
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

static NSUInteger failures;
static void check(BOOL condition, const char *name)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        failures++;
}
static void completeAll(void)
{
    NSArray *commands = [pending copy];
    [pending removeAllObjects];
    for (FakeCommandBuffer *command in commands)
        [command complete];
}
static AVSVideoView *newView(void)
{
    completeAll();
    uploads = submitted = violations = allocatedTextures = 0;
    missingDrawable = missingBuffer = missingEncoder = NO;
    AVSVideoView *view = [AVSVideoView new];
    [view layoutSubviews];
    [view startRunning];
    return view;
}
int main(void)
{
    @autoreleasepool
    {
        pending = [NSMutableArray new];
        uint8_t y[64 * 48] = {}, u[32 * 24] = {}, v[32 * 24] = {};
        struct avs_vidframe frame = {};
        frame.type = AVS_VIDFRAME_I420;
        frame.w = 64;
        frame.h = 48;
        frame.y = y;
        frame.u = u;
        frame.v = v;
        frame.ys = 64;
        frame.us = 32;
        frame.vs = 32;
        AVSVideoView *view = newView();
        [view handleFrame:&frame];
        check(uploads == 3, "first frame uploads all three planes");
        NSUInteger initialTextures = allocatedTextures;
        view.shouldFill = view.shouldFill;
        [view handleFrame:&frame];
        check(allocatedTextures == initialTextures,
            "unchanged scaling does not replace textures");
        view.bounds = CGRectMake(0, 0, 800, 600);
        [view layoutSubviews];
        [view handleFrame:&frame];
        check(allocatedTextures == initialTextures,
            "layout-only changes do not replace textures");
        [view render:nil];
        check(submitted == 1, "first render submits GPU work");
        [view render:nil];
        check(submitted == 1,
            "no duplicate command submission while frame is in flight");
        [view handleFrame:&frame];
        check(violations == 0,
            "no CPU writes to textures or vertex data owned by GPU work");
        completeAll();
        NSUInteger before = uploads;
        NSUInteger submissionsBeforeNextFrame = submitted;
        [view handleFrame:&frame];
        [view render:nil];
        check(uploads == before + 3
                && submitted == submissionsBeforeNextFrame + 1,
            "next frame uploads and renders after GPU completion");
        completeAll();
        [view stopRunning];

        // A decoder attempts a resolution change after Y has been bound, before
        // U/V. The encoder hook is outside the implementation under test. It
        // controls scheduling only; both decoder and renderer execute the
        // actual source.
        view = newView();
        [view handleFrame:&frame];
        uint8_t bigY[128 * 96] = {}, bigU[64 * 48] = {}, bigV[64 * 48] = {};
        __block struct avs_vidframe resized = {};
        resized.type = AVS_VIDFRAME_I420;
        resized.w = 128;
        resized.h = 96;
        resized.y = bigY;
        resized.u = bigU;
        resized.v = bigV;
        resized.ys = 128;
        resized.us = 64;
        resized.vs = 64;
        dispatch_semaphore_t started = dispatch_semaphore_create(0);
        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        // Probe the real frame mutex only to choose a legal interleaving,
        // without timing assumptions. The decoder still executes the real
        // handleFrame:.
        Ivar frameLockIvar
            = class_getInstanceVariable(AVSVideoView.class, "_lock");
        NSLock *frameLock = object_getIvar(view, frameLockIvar);
        __block BOOL writerAttempted = NO;
        __block BOOL writerCanFinishDuringBinding = NO;
        __block BOOL writerFinishedDuringBinding = NO;
        AVSVideoView *interleavedView = view;
        bindingHook = ^{
            dispatch_async(
                dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                    writerCanFinishDuringBinding = [frameLock tryLock];
                    if (writerCanFinishDuringBinding)
                        [frameLock unlock];
                    dispatch_semaphore_signal(started);
                    [interleavedView handleFrame:&resized];
                    dispatch_semaphore_signal(finished);
                });
            writerAttempted
                = dispatch_semaphore_wait(
                      started, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC))
                == 0;
            // Original source permits the writer here. The fixed renderer holds
            // its lock until submission; the blocked writer then sees GPU
            // ownership.
            if (writerAttempted && writerCanFinishDuringBinding)
            {
                writerFinishedDuringBinding
                    = dispatch_semaphore_wait(finished,
                          dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC))
                    == 0;
                if (writerFinishedDuringBinding)
                    dispatch_semaphore_signal(finished);
            }
        };
        [view render:nil];
        check(writerAttempted
                && (!writerCanFinishDuringBinding
                    || writerFinishedDuringBinding)
                && dispatch_semaphore_wait(
                       finished, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC))
                    == 0,
            "resolution-change decoder attempt finishes without deadlock");
        check(boundWidths.count == 3
                && boundWidths[0].unsignedIntegerValue
                    == 2 * boundWidths[1].unsignedIntegerValue
                && boundWidths[1].unsignedIntegerValue
                    == boundWidths[2].unsignedIntegerValue,
            "encoder binds Y/U/V from one coherent texture generation");
        completeAll();
        [view stopRunning];

        for (NSUInteger fault = 0; fault < 3; fault++)
        {
            view = newView();
            [view handleFrame:&frame];
            missingDrawable = fault == 0;
            missingBuffer = fault == 1;
            missingEncoder = fault == 2;
            [view render:nil];
            missingDrawable = missingBuffer = missingEncoder = NO;
            [view render:nil];
            check(submitted == 1,
                fault == 0       ? "recovers after unavailable drawable"
                    : fault == 1 ? "recovers after unavailable command buffer"
                                 : "recovers after unavailable encoder");
            completeAll();
            [view stopRunning];
        }
        printf("RESULT %lu failure(s); host fake-Metal ordering only, not a "
               "GPU or SIGSEGV reproduction\n",
            (unsigned long)failures);
    }
    return failures ? 1 : 0;
}
