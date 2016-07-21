# mediaengine - Build system
#
# webrtc/sdk
#

ifeq ($(AVS_OS),ios)

MENG_CPPFLAGS += \
	-Imediaengine/webrtc/sdk/objc/Framework/Headers \
	-Imediaengine/webrtc/sdk/objc/Framework/Classes

MENG_SRCS += \
	webrtc/sdk/objc/Framework/Classes/RTCLogging.mm \
	webrtc/sdk/objc/Framework/Classes/RTCUIApplication.mm \



#	webrtc/sdk/objc/Framework/Classes/NSString+StdString.mm \
	webrtc/sdk/objc/Framework/Classes/RTCAVFoundationVideoSource.mm \
	webrtc/sdk/objc/Framework/Classes/RTCAudioTrack.mm \
	webrtc/sdk/objc/Framework/Classes/RTCConfiguration.mm \
	webrtc/sdk/objc/Framework/Classes/RTCDataChannel.mm \
	webrtc/sdk/objc/Framework/Classes/RTCDataChannelConfiguration.mm \
	webrtc/sdk/objc/Framework/Classes/RTCFieldTrials.mm \
	webrtc/sdk/objc/Framework/Classes/RTCFileLogger.mm \
	webrtc/sdk/objc/Framework/Classes/RTCIceCandidate.mm \
	webrtc/sdk/objc/Framework/Classes/RTCIceServer.mm \
	webrtc/sdk/objc/Framework/Classes/RTCMediaConstraints.mm \
	webrtc/sdk/objc/Framework/Classes/RTCMediaStream.mm \
	webrtc/sdk/objc/Framework/Classes/RTCMediaStreamTrack.mm \
	webrtc/sdk/objc/Framework/Classes/RTCOpenGLVideoRenderer.mm \
	webrtc/sdk/objc/Framework/Classes/RTCPeerConnection+DataChannel.mm \
	webrtc/sdk/objc/Framework/Classes/RTCPeerConnection+Stats.mm \
	webrtc/sdk/objc/Framework/Classes/RTCPeerConnection.mm \
	webrtc/sdk/objc/Framework/Classes/RTCPeerConnectionFactory.mm \
	webrtc/sdk/objc/Framework/Classes/RTCRtpCodecParameters.mm \
	webrtc/sdk/objc/Framework/Classes/RTCRtpEncodingParameters.mm \
	webrtc/sdk/objc/Framework/Classes/RTCRtpParameters.mm \
	webrtc/sdk/objc/Framework/Classes/RTCRtpReceiver.mm \
	webrtc/sdk/objc/Framework/Classes/RTCRtpSender.mm \
	webrtc/sdk/objc/Framework/Classes/RTCSSLAdapter.mm \
	webrtc/sdk/objc/Framework/Classes/RTCSessionDescription.mm \
	webrtc/sdk/objc/Framework/Classes/RTCStatsReport.mm \
	webrtc/sdk/objc/Framework/Classes/RTCTracing.mm \
	webrtc/sdk/objc/Framework/Classes/RTCVideoFrame.mm \
	webrtc/sdk/objc/Framework/Classes/RTCVideoRendererAdapter.mm \
	webrtc/sdk/objc/Framework/Classes/RTCVideoSource.mm \
	webrtc/sdk/objc/Framework/Classes/RTCVideoTrack.mm \
	webrtc/sdk/objc/Framework/Classes/avfoundationvideocapturer.mm

endif
