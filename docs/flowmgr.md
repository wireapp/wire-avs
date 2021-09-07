Flow Manager
============

In Zeta, calling is split into two more or less independent parts: call
state handling and media handling.  Call state handling controls which
users and devices have joined a call in a conversation.  Media handling
makes sure that audio and video data is being exchanged between all
devices that have joined a call.

The flow manager is a component of the AVS library that implements media
handling for calling.  It takes care of both the network and the AV side
of call media: it establishes media flows between devices, records audio
and video from the recording devices present in the system, encodes these
and sends them over the media flows, it receives encoded media from the
network, decodes it and plays it back on playback devices.

This document describes both the interface and function of the flow
manager in great detail.


Basics
------

Calling happens in the context of conversations.  Each conversation has an
associated call which devices can join.  Whether a device can join a call
and for how long is being controlled by the service.  This is determined by
the call state handling component of calling and is not being discussed
here.

In order to be able to participate in a call, a device needs to be able to
exchange data with remote locations. Since there can be several devices
that can potentially join a call, the remote location is not predetermined
and it can change during a call. However, for each device there is at most
one such location at any given time. The relationship between exactly two
devices for exchanging media is called a media flow.

Media flows are controlled by the service. Before starting a call, a device
asks the service for a set of flows, called 'posting for flows.' The
service determines any potential other devices in the call and creates a
media flow the asking devices and each of these. They receive their flows
without having asked for them through the `call.flow-add` event.

When a device has received a flow, be it by asking or throug an event, it
starts preparing the flow. This entails an offer/answer exchange for media
capabilities followed by an ICE exchange to determine and open actual
network connections.

A call starts once at least two devices have joined call. When this
happens, the service determines which media flow should be used to
exchange media over and informs the device by 'activating the flow'
through a `call.flow-active` event. Only when a flow is active is a device
allowed to send media over it. However, it does not have to. The device
can decide to keep the call muted or it can keep it on hold while being in
the call of another conversation.

Sending and receiving of media ends when the flow is being deactivated by
the service, yet again by way of a `call.flow-active` event.

Once the device determines that it doesn't want to be in a call anymore,
it can delete all its flows. If the other side of a flow decides to delete
it, the device will be informed by the service through a
`call.flow-delete` event.


The Flow Manager API
--------------------

The flow manager is a single object that provides a number of functions to
be called by the user and calls a number of callbacks to be registered by
the user.

There is only one flow manager per application. Functions that operate on
a per-conversation basis use the conversation ID as an argument.


Service Connection
------------------

The flow manager needs to communicate to the service. It does so both
through HTTP requests and events.

It emits HTTP requests through a callback registered at creation time.
This callback will contain all parts of the HTTP request as arguments.
Additionally, it contains an opaque pointer to a context. It is the user's
responsibility to create and dispatch an HTTP request. Once a response
arrives, the user must pass it to the flow manager through the
`flowmgr_resp()` function, passing the context pointer from the request.

Additionally, all events must be passed to the flow manager through the
`flowmgr_event()` function.


Managing Calls
--------------

When the API user anticipates that a call might start, it calls the
`flowmgr_acquire_flows()` function. This will cause the flow manager to
request flows from the service. The user can now proceed with call states.
Eventually, the service will activate one of the flows in response to the
device call state being set to active.

Now there's two options: if setting up media on the activated flow fails,
an error is emitted allowing the user to alert its user and wind down the
call. If, however, it succeeds, something called audio categories comes
into play.

It is also important that flows are released (flowmgr_release_flows()) after
a call participant has been set to "idle" and response has been received.

Media Categories
----------------

Media categories describe how recording and playback of media should be
handled for a conversation. Currently, there are four:

*  _regular_: this is the normal mode where only notification sounds are
   emitted from the conversation and no recording takes place;
*  _muted_: the conversation has been muted and no playback or recording
   should happen (note that this is independent of any muting feature
   implemented by UI);
*  _call_: the conversation has actively joined a call, incoming media is
   being played back and recorded media is being streamed to the network;
*  _playback_: an embedded media element in the conversation is playing
   its media.

The media category for each conversation is managed by the user of the
flow manager. In most cases, it will do so using the media manager.

The flow manager does, however, request a certain media category necessary
for its operation. When a flow has been activated, it requests the _call_
category. When an active flow has been deactivated, it requests the
_regular_ category. It is up to the user to actually set the requested
category and perform any necessary action for a change.

The flow manager requests a changed through `flowmgr_mcat_h`
callback. Any change to the media category of a conversation needs to be
communicated using the `flowmgr_mcat_changed()` function.

So, when a flow is being activated by the service, the flow manager will
request the _call_ media category for that conversation. It will only
start playback and recording, however, once it has been notified that this
change has happened.

Similarly, the user can decide at any time to revoke the _call_ media
category in which case the flow manager stops playback and recording.
