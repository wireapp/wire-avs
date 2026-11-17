# Web audio source identity

Register the source callback before starting calls:

```ts
wcall.setAudioSourceHandler((conversationId, trackId, userId, clientId) => {
  // Find trackId in the audio streams delivered by setAudioStreamHandler.
  // Update the identity currently feeding that receiver track.
});
```

Conference receiver tracks can be reused for different senders. The callback
reports the first authenticated audio frame for a receiver, then changes in
user or client identity. Repeated frames from the same sender do not repeat the
notification. Failed decryption does not publish an identity.

For direct calls, the callback reports the peer identity when its audio track
arrives, or when the peer identity becomes available after the track. Existing
audio stream callbacks keep their current signature and behavior.

Notifications are asynchronous relative to decoded audio. They identify the
current sender, not sample-accurate boundaries in a captured PCM buffer. There
is no replay when registering after a call has already started.

Pass `null` to stop receiving notifications:

```ts
wcall.setAudioSourceHandler(null);
```

## Focused tests

```sh
cd wasm
npm ci
npm run test:audio-source
```

These tests compile the actual TypeScript peer-connection source and exercise
its embedded worker, including authenticated encryption and failed decryption.
They do not require building the native AVS core. The existing `npm test` suite
still requires the generated AVS/WASM artifacts.
