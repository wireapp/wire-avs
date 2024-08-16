
#import <MetalKit/MetalKit.h>

#import "AVSVideoView.h"
#import "AVSFlowManager.h"

#include "ShaderSource.h"

#include <re.h>
#include <avs.h>

@implementation AVSVideoView {
	id<MTLDevice> _metalDevice;
	CAMetalLayer *_metalLayer;
	id<MTLFunction> _vertexFunction;
	id<MTLFunction> _fragmentFunction;
	id<MTLCommandQueue> _commandQueue;

	BOOL _running;
	BOOL _paused;
	int _newFrame;
	
	UIView *view;

	MTLRenderPassDescriptor *_renderPassDescriptor;
	id<MTLRenderPipelineState> _pipelineState;
	id<MTLBuffer>              _vertices;
	id<MTLTexture>             _depthTarget;

	// Buffers.
	id<MTLBuffer> _vertexBuffer;
	
	// Textures.
	id<MTLTexture> _yTexture;
	id<MTLTexture> _uTexture;
	id<MTLTexture> _vTexture;

	MTLTextureDescriptor *_lumaDescriptor;
	MTLTextureDescriptor *_chromaDescriptor;

	CADisplayLink* _displayLink;	

	int _chromaWidth;
	int _chromaHeight;

	//dispatch_semaphore_t _gpuSemaphore;

	int _oldRotation;
	int _oldWidth;
	int _oldHeight;

	NSLock *_lock;
	CGSize _viewFrame;
	BOOL _shouldFill;
	BOOL _forceRecalc;
}

+ (Class)layerClass
{
	return [CAMetalLayer class];
}


- (id)init
{
	NSError *err;

	NSLog(@"AVSVideoView_metal: init\n");
	
	self = [super init];
	if (self) {
		_lock = [[NSLock alloc] init];

		_newFrame = 0;
		
		_metalDevice = MTLCreateSystemDefaultDevice();
		if (_metalDevice == nil) {
			NSLog(@"AVSVideoView_metal: no device");
			return nil;
		}
		[self setupLayer];

		//_gpuSemaphore = dispatch_semaphore_create(1);
	
		NSLog(@"AVSVideoView_metal: compiling source...\n");
		id<MTLLibrary> metalLib = [_metalDevice newLibraryWithSource:g_ShaderSrc options:nil error:&err];
		if (metalLib == nil) {
			NSLog(@"AVSVideoView_metal: failed to compile shaders: %@\n", err.localizedDescription);
			return nil;
		}
		_vertexFunction = [metalLib newFunctionWithName:@"vertexPassthrough"];
		if (_vertexFunction == nil) {
			NSLog(@"AVSVideoView_metal: VertexShader failed\n");
		}
		else {
			NSLog(@"AVSVideoView_metal: VertexShader succeeded\n");
		}
		_fragmentFunction = [metalLib newFunctionWithName:@"fragmentColorConversion"];
		if (_fragmentFunction == nil) {
			NSLog(@"AVSVideoView_metal: FragmentShader failed\n");
		}
		else {
			NSLog(@"AVSVideoView_metal: FragmentShader succeeded\n");
		}

		MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
		pipelineDescriptor.label = @"AVSPipeline";
		pipelineDescriptor.vertexFunction = _vertexFunction;
		pipelineDescriptor.fragmentFunction = _fragmentFunction;
		pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
		pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;		
		_pipelineState = [_metalDevice newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&err];
		if (!_pipelineState) {
			NSLog(@"AVSVideoView_metal: pipeline state failed: %@\n", err);
		}

		_commandQueue = [_metalDevice newCommandQueue];

		_renderPassDescriptor = [MTLRenderPassDescriptor new];
		_renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
		_renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
		_renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

		float vertexBufferArray[16] = {0};
		_vertexBuffer = [_metalDevice newBufferWithBytes:vertexBufferArray
							  length:sizeof(vertexBufferArray)
							 options:MTLResourceCPUCacheModeWriteCombined];
		if (!_vertexBuffer) {
			NSLog(@"AVSVideoView_Metal: cannot create vertex buffer\n");
		}
	
		NSLog(@"AVSVideoView-init: source compiled successfully\n");

		self.autoresizingMask =
			UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

#if TARGET_OS_IPHONE
		[[NSNotificationCenter defaultCenter] addObserver:self
		      selector:@selector(applicationWillResignActive:)
		      name:UIApplicationWillResignActiveNotification 
		      object:NULL];
		[[NSNotificationCenter defaultCenter] addObserver:self
		      selector:@selector(applicationDidBecomeActive:)
		      name:UIApplicationDidBecomeActiveNotification
		      object:NULL];
#endif
		
	}
		
	return self;
}


- (void)setupLayer
{
	_metalLayer = (CAMetalLayer*) self.layer;
	_metalLayer.opaque = YES;
}

- (CALayer *)makeBackingLayer
{
	return [CAMetalLayer layer];
}

- (void)getVertexData:(struct avs_vidframe *)vf
	buffer:(float *)buffer
{
	CGSize frame = _viewFrame; //self.frame;
	float dw = (float)frame.width;
	float dh = (float)frame.height;
	
	float vw = vf->w;
	float vh = vf->h;

	float xscale = 1.0f;
	float yscale = 1.0f;

	if (dw == 0.0f || dh == 0.0f ||
	    vw == 0 || vh == 0) {
		return;
	}

	float da = dw / dh;
	float va = vw / vh;

	BOOL fill = _shouldFill;

	// 180 & 270 are double-flipped 0 & 90
	if (vf->rotation == 180 || vf->rotation == 270) {
		xscale = -1.0f;
		yscale = -1.0f;
	}

	// for 90 & 270 we should match width to height & vice versa
	if (vf->rotation == 90 || vf->rotation == 270) {
		va = 1.0f / va;
	}

	if (da / va < _fillRatio &&
	    va / da < _fillRatio) {
		fill = YES;
	}
	
	if (fill == (da > va)) {
		yscale *= da / va;
	}
	else {
		xscale *= va / da;
	}

	switch (vf->rotation) {
	case 0:
	case 180: {
		float values[16] = {
			-xscale,  yscale, 0.0f, 0.0f,
			 xscale,  yscale, 1.0f, 0.0f,
			-xscale, -yscale, 0.0f, 1.0f,
			 xscale, -yscale, 1.0f, 1.0f,
		};
		memcpy(buffer, values, sizeof(values));
	}
		break;

	case 90:
	case 270: {
		float values[16] = {
			 xscale,  yscale, 0.0f, 0.0f,
			 xscale, -yscale, 1.0f, 0.0f,
			-xscale,  yscale, 0.0f, 1.0f,
			-xscale, -yscale, 1.0f, 1.0f,
		};
		memcpy(buffer, values, sizeof(values));
	}
		break;
	
	default:
		break;
	}
}


- (BOOL)setupTexturesForFrame:(struct avs_vidframe *)frame
{
	BOOL needsUpdate = NO;
	
	if (!_vertexBuffer) {
		warning("AVSVideoView_metal: setupTexturesFromFrame: no vertex buffer\n");
		return NO;
	}

	// Recompute the texture cropping and recreate vertexBuffer if necessary.
	if (frame->rotation != _oldRotation
	  || frame->w != _oldWidth
	  || frame->h != _oldHeight
	  || _forceRecalc) {
		[self getVertexData:frame
			     buffer:(float *)_vertexBuffer.contents];
		_oldRotation = frame->rotation;
		_oldWidth = frame->w;
		_oldHeight = frame->h;
		_forceRecalc = NO;
		needsUpdate = YES;
	}
	
	// Luma (y) texture.
	if (!_lumaDescriptor || needsUpdate) {
		_lumaDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
										     width:frame->w
										    height:frame->h
										 mipmapped:NO];
		_lumaDescriptor.usage = MTLTextureUsageShaderRead;
		_yTexture = [_metalDevice newTextureWithDescriptor:_lumaDescriptor];
	}

	[_yTexture replaceRegion:MTLRegionMake2D(0, 0, frame->w, frame->h)
		     mipmapLevel:0
		       withBytes:frame->y
		     bytesPerRow:frame->ys];

	// Chroma (u,v) textures
	if (!_chromaDescriptor || needsUpdate) {
		_chromaWidth = frame->w / 2;
		_chromaHeight = frame->h / 2;
		_chromaDescriptor =
			[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
									   width:_chromaWidth
									  height:_chromaHeight
								       mipmapped:NO];
		_chromaDescriptor.usage = MTLTextureUsageShaderRead;
		_uTexture = [_metalDevice newTextureWithDescriptor:_chromaDescriptor];
		_vTexture = [_metalDevice newTextureWithDescriptor:_chromaDescriptor];
	}

	// U
	[_uTexture replaceRegion:MTLRegionMake2D(0, 0, _chromaWidth, _chromaHeight)
		     mipmapLevel:0
		       withBytes:frame->u
		     bytesPerRow:frame->us];
	
	// V
	[_vTexture replaceRegion:MTLRegionMake2D(0, 0, _chromaWidth, _chromaHeight)
		     mipmapLevel:0
		       withBytes:frame->v
		     bytesPerRow:frame->vs];

	return _yTexture != nil && _uTexture != nil &&  _vTexture != nil;
}

- (void)render:(CADisplayLink*)displayLink
{
	[_lock lock];
	if (!_running || _paused) {
		[_lock unlock];
		return;
	}

	if (_newFrame == 0 && !_forceRecalc) {
		[_lock unlock];
		return;
	}
	[_lock unlock];
	
	//__block dispatch_semaphore_t blockSem = _gpuSemaphore;

	if (!_renderPassDescriptor || _viewFrame.width == 0 || _viewFrame.height == 0) {
		//dispatch_semaphore_signal(blockSem);
		return;
	}

	id<CAMetalDrawable> currentDrawable = [_metalLayer nextDrawable];
	if (!currentDrawable) {
		//dispatch_semaphore_signal(blockSem);
		return;
	}

	id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];

	commandBuffer.label = @"AVSCommandBuffer";
	[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
			// GPU work has completed.
			[_lock lock];
			if (_newFrame > 0)
				_newFrame--;
			[_lock unlock];
			//dispatch_semaphore_signal(blockSem);
		}];
	
	_renderPassDescriptor.colorAttachments[0].texture = currentDrawable.texture;
		
	id<MTLRenderCommandEncoder> renderEncoder =
		[commandBuffer renderCommandEncoderWithDescriptor:_renderPassDescriptor];
	renderEncoder.label = @"AVSEncoder";

	// Set context state.
	//[renderEncoder pushDebugGroup:renderEncoderDebugGroup];
	[renderEncoder setRenderPipelineState:_pipelineState];
	[renderEncoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0];

	[renderEncoder setFragmentTexture:_yTexture atIndex:0];
	[renderEncoder setFragmentTexture:_uTexture atIndex:1];
	[renderEncoder setFragmentTexture:_vTexture atIndex:2];

	[renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
			  vertexStart:0
			  vertexCount:4
			instanceCount:1];
		
	//[renderEncoder popDebugGroup];
	[renderEncoder endEncoding];

	[commandBuffer presentDrawable:currentDrawable];
	[commandBuffer commit];
}


 - (BOOL)handleFrame:(struct avs_vidframe *)frame
{
	//NSLog(@"handleFrame: frame=%dx%d\n", frame->w, frame->h);
	@autoreleasepool {
		// Wait until any pending GPU work is done
		//_block dispatch_semaphore_t blockSem = _gpuSemaphore;		
		//dispatch_semaphore_wait(blockSem, DISPATCH_TIME_FOREVER);
		if ([self setupTexturesForFrame:frame]) {
			[_lock lock];
			_newFrame++;
			[_lock unlock];
		} else {
			//dispatch_semaphore_signal(blockSem);
		}
	}

	return NO;
}


- (void)startRunning
{
	info("AVSVideoView_metal: startRunning on view: %p\n", self);
	
	[_lock lock];
	_running = YES;
	[_lock unlock];

	_displayLink =
		[CADisplayLink displayLinkWithTarget:self selector:@selector(render:)];
	[_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
}

- (void)stopRunning
{
	_running = NO;
	[_displayLink invalidate];
	_displayLink = nil;
}

- (void)applicationWillResignActive:(NSNotification *)notification
{
	[_lock lock];
	_paused = YES;
	[_lock unlock];
}

- (void)applicationDidBecomeActive:(NSNotification *)notification;
{
	[_lock lock];
	_paused = NO;
	[_lock unlock];
}

- (void)didMoveToWindow
{
	AVSFlowManager *fm = [AVSFlowManager getInstance];

	info("didMoveToWindow self: %p window: %p flowmgr %p\n", self, self.window, fm);
	if (fm) {
		if (self.window) {
			[fm attachVideoView: self];
		}
		else {
			[fm detachVideoView: self];
		}
	}

	if (self.window) {
		[self startRunning];
	}
	else {
		[self stopRunning];
	}

	_forceRecalc = YES;
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)interfaceOrientation
{
	return YES;
}

- (void) setShouldFill:(BOOL)fill
{
	[_lock lock];
	_shouldFill = fill;
	_forceRecalc = YES;
	[_lock unlock];
}

- (BOOL) shouldFill
{
	return _shouldFill;
}

- (void)layoutSubviews
{
	[_lock lock];
	CGFloat scale = [UIScreen mainScreen].scale;
	_metalLayer.drawableSize = CGSizeMake(self.bounds.size.width * scale,
					      self.bounds.size.height * scale);
					  
	_viewFrame = self.bounds.size;
	_forceRecalc = YES;
	[_lock unlock];

	info("layoutSubviews: bounds=%dx%d frame=%dx%d drawable=%dx%d scale=%f\n",
	     (int)self.bounds.size.width, (int)self.bounds.size.height,
	     (int)self.frame.size.width, (int)self.frame.size.height,
	     (int)_metalLayer.drawableSize.width, (int)_metalLayer.drawableSize.height,
	     scale);
}

-(void)dealloc
{
#if TARGET_OS_IPHONE
	[[NSNotificationCenter defaultCenter] removeObserver:self
	      name:UIApplicationWillResignActiveNotification 
	      object:NULL];
	[[NSNotificationCenter defaultCenter] removeObserver:self
	      name:UIApplicationDidBecomeActiveNotification
	      object:NULL];
#endif

	[[AVSFlowManager getInstance] detachVideoView: self];
}


@end
