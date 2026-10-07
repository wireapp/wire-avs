// Run from the repository root: node wasm/test/publisherUpdate.test.js
// Uses the existing TypeScript installation in build/dist/wasm.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('../../build/dist/wasm/node_modules/typescript');
const {test} = require('node:test');
const source = fs.readFileSync(require('node:path').join(__dirname, '../src/avs_pc.ts'), 'utf8');
const ast = ts.createSourceFile('avs_pc.ts', source, ts.ScriptTarget.Latest, true);
const names = ['pc_SetReceiveTracks', 'setupReceiverTransform', 'pc_Close', 'pc_CreateNegotiatedDataChannel', 'setupDataChannel', 'dataChannelHandler', 'pc_ReplaceTrack', 'requestPublisherVideoUpdate', 'createSdp', 'sdpMap', 'discardPublisherVideo', 'preparePublisherTracks', 'pc_SetRemoteDescription', 'pc_SetLocalDescription', 'gatheringComplete'];
const functions = ast.statements.filter(node => ts.isFunctionDeclaration(node) &&
  node.name && names.includes(node.name.text)).map(node => node.getText(ast)).join('\n');
assert.equal(ast.statements.filter(node => ts.isFunctionDeclaration(node) &&
  node.name && names.includes(node.name.text)).length, names.length);
const code = ts.transpileModule(functions, {compilerOptions: {target: ts.ScriptTarget.ES2017}}).outputText;
function runtime(globals) {
  const context = vm.createContext({PC_MEDIA_SENDRECV: 0, PC_MEDIA_SENDONLY: 1, PC_MEDIA_RECVONLY: 2, Promise, Error, pc_log: () => {}, LOG_LEVEL_INFO: 1, ...globals});
  vm.runInContext(code, context);
  return context;
}
function track(kind, id) {
  return {kind, id, enabled: true, stops: 0, stop() {++this.stops;}};
}
function stream(tracks) {
  return {getTracks: () => tracks, getVideoTracks: () => tracks.filter(t => t.kind === 'video')};
}
function publisher(audio) {
  const added = [];
  const rtc = {getSenders: () => audio ? [{track: audio}] : [],
    addTransceiver: (track, options) => added.push({track, options})};
  return {pc: {rtc, convid: 'call', muted: true}, added};
}

test('adding camera preserves muted audio and never replaces a track', async () => {
  const audio = track('audio', 'existing-audio');
  audio.enabled = false;
  const video = track('video', 'camera');
  const {pc, added} = publisher(audio);
  let argumentsUsed;
  const ctx = runtime({userMediaHandler: (...args) => {
    argumentsUsed = args;
    return Promise.resolve(stream([video]));
  }});
  await ctx.preparePublisherTracks(pc, true, false);
  assert.deepEqual(argumentsUsed, ['call', false, true, false]);
  assert.equal(added.length, 1);
  assert.equal(added[0].track, video);
  assert.equal(added[0].options.direction, 'sendonly');
  assert.equal(audio.enabled, false);
  assert.equal(audio.stops, 0);
});

test('unexpected newly captured audio is stopped without touching existing audio', async () => {
  const audio = track('audio', 'existing');
  const extra = track('audio', 'unexpected');
  const video = track('video', 'camera');
  const {pc, added} = publisher(audio);
  const ctx = runtime({userMediaHandler: () => Promise.resolve(stream([extra, video]))});
  await ctx.preparePublisherTracks(pc, true, false);
  assert.equal(extra.stops, 1);
  assert.equal(audio.stops, 0);
  assert.equal(added.length, 1);
});

test('late capture after closing the publisher is discarded', async () => {
  const {pc, added} = publisher(track('audio', 'existing'));
  const video = track('video', 'camera');
  let complete;
  const ctx = runtime({userMediaHandler: () => new Promise(resolve => {complete = resolve;})});
  const preparation = ctx.preparePublisherTracks(pc, true, false);
  pc.rtc = null;
  complete(stream([video]));
  await assert.rejects(preparation, /closed during media capture/);
  assert.equal(video.stops, 1);
  assert.equal(added.length, 0);
});

test('capture without a video track cannot produce a successful camera update', async () => {
  const {pc, added} = publisher(track('audio', 'existing'));
  const ctx = runtime({userMediaHandler: () => Promise.resolve(stream([]))});
  await assert.rejects(ctx.preparePublisherTracks(pc, true, false), /No video track/);
  assert.equal(added.length, 0);
});

test('SDP completion is reported only after the remote answer resolves', async () => {
  let complete;
  const calls = [];
  const pc = {self: 99, media_direction: 2, rtc: {
    localDescription: {type: 'offer', sdp: 'offer'}, getTransceivers: () => [],
    setRemoteDescription: () => new Promise(resolve => {complete = resolve;})
  }};
  const ctx = runtime({
    pc_log: () => {}, LOG_LEVEL_INFO: 1, LOG_LEVEL_WARN: 2,
    connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: value => value, ccall: (...args) => calls.push(args)},
    pc_env: 0, ENV_FIREFOX: 1, CONV_TYPE_CONFERENCE: 2, PC_MEDIA_SENDONLY: 2,
    sdpDirectionSummary: () => '', extractSSRCs: () => {}
  });
  ctx.pc_SetRemoteDescription(1, 'answer', 'answer');
  assert.equal(calls.length, 0);
  complete();
  await new Promise(setImmediate);
  assert.equal(calls.length, 1);
  assert.equal(calls[0][0], 'pc_sdp_applied_handler');
  assert.equal(calls[0][3][2], 0);
});

test('reused complete ICE still reports the new local offer', async () => {
  const gathered = [];
  const pc = {self: 99, media_direction: 2, rtc: {
    iceGatheringState: 'complete', localDescription: null,
    setLocalDescription(description) {this.localDescription = description; return Promise.resolve();}
  }};
  const ctx = runtime({pc_log: () => {}, LOG_LEVEL_INFO: 1,
    connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: value => value},
    pc_env: 0, ENV_FIREFOX: 1, CONV_TYPE_CONFERENCE: 2, PC_MEDIA_SENDONLY: 2,
    ccallGatheringHandler: (...args) => gathered.push(args)
  });
  ctx.pc_SetLocalDescription(1, 'offer', 'new-offer');
  assert.equal(gathered.length, 0);
  await new Promise(setImmediate);
  assert.equal(gathered.length, 1);
  assert.equal(gathered[0][2], 'new-offer');
});

test('rejected remote answer reports an error instead of success', async () => {
  const calls = [];
  const pc = {self: 99, media_direction: 2, rtc: {
    localDescription: {type: 'offer', sdp: 'offer'}, getTransceivers: () => [],
    setRemoteDescription: () => Promise.reject(new Error('invalid answer'))
  }};
  const ctx = runtime({pc_log: () => {}, LOG_LEVEL_INFO: 1, LOG_LEVEL_WARN: 2,
    connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: value => value, ccall: (...args) => calls.push(args)},
    pc_env: 0, ENV_FIREFOX: 1, CONV_TYPE_CONFERENCE: 2, PC_MEDIA_SENDONLY: 2,
    sdpDirectionSummary: () => '', extractSSRCs: () => {}
  });
  ctx.pc_SetRemoteDescription(1, 'answer', 'answer');
  await new Promise(setImmediate);
  assert.equal(calls.length, 1);
  assert.equal(calls[0][3][2], 1);
});


test('camera off stops only video and keeps audio and the same connection', async () => {
  const audio = track('audio', 'mic');
  const video = track('video', 'camera');
  let stopped = 0;
  const sender = {track: video};
  const rtc = {
    getSenders: () => [{track: audio}, sender],
    getTransceivers: () => [{sender: {track: audio}}, {sender, stop: () => ++stopped}],
    removeTrack: value => assert.equal(value, sender),
  };
  const pc = {rtc, sending_video: true};
  const ctx = runtime({userMediaHandler: () => {throw Error('No new capture expected');}});
  await ctx.preparePublisherTracks(pc, false, false);
  assert.equal(pc.rtc, rtc);
  assert.equal(audio.stops, 0);
  assert.equal(video.stops, 1);
  assert.equal(stopped, 1);
  assert.equal(pc.sending_video, false);
});



for (const [from, to] of [['camera', 'screen'], ['screen', 'camera']]) {
  test(`source change ${from} to ${to} retires video and adds a new transceiver`, async () => {
    const audio = track('audio', 'mic');
    const oldVideo = track('video', from);
    const newVideo = track('video', to);
    const added = [];
    let stopped = 0;
    const sender = {track: oldVideo, replaceTrack: () => {throw Error('No replaceTrack');}};
    const rtc = {getSenders: () => [{track: audio}, sender],
      getTransceivers: () => [{sender: {track: audio}}, {sender, stop: () => ++stopped}],
      removeTrack: value => {assert.equal(value, sender); sender.track = null;},
      addTransceiver: (track, options) => added.push({track, options})};
    const pc = {rtc, convid: 'call', publisher_video_source: from};
    let args;
    const ctx = runtime({userMediaHandler: (...values) => {
      args = values; return Promise.resolve(stream([newVideo]));
    }});
    await ctx.preparePublisherTracks(pc, to === 'camera', to === 'screen');
    assert.deepEqual(args, ['call', false, to === 'camera', to === 'screen']);
    assert.equal(stopped, 1);
    assert.equal(oldVideo.stops, 1);
    assert.equal(audio.stops, 0);
    assert.equal(pc.rtc, rtc);
    assert.equal(added.length, 1);
    assert.equal(added[0].track, newVideo);
    assert.equal(added[0].options.direction, 'sendonly');
    assert.equal(pc.publisher_video_source, to);
  });
}

test('capture failure preserves the existing video before transceiver retirement', async () => {
  const audio = track('audio', 'mic');
  const oldVideo = track('video', 'camera');
  const pc = {rtc: {getSenders: () => [{track: audio}, {track: oldVideo}]}, publisher_video_source: 'camera'};
  const ctx = runtime({userMediaHandler: () => Promise.reject(Error('capture declined'))});
  await assert.rejects(ctx.preparePublisherTracks(pc, false, true), /capture declined/);
  assert.equal(oldVideo.stops, 0);
  assert.equal(audio.stops, 0);
  assert.equal(pc.publisher_video_source, 'camera');
});

test('publisher offers preserve rejected video sections without legacy SDP munging', async () => {
  const audio = track('audio', 'mic');
  const rawSdp = 'v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\nm=video 0 UDP/TLS/RTP/SAVPF 96\r\na=inactive\r\n';
  const rtc = {getSenders: () => [{track: audio}], getTransceivers: () => [],
    createOffer: () => Promise.resolve({type: 'offer', sdp: rawSdp})};
  const pc = {rtc, media_direction: 1};
  let offer;
  const ctx = runtime({PC_MEDIA_SENDONLY: 1, PC_MEDIA_RECVONLY: 2,
    PC_VIDEO_STATE_STARTED: 1, PC_VIDEO_STATE_SCREENSHARE: 2,
    userMediaHandler: () => {throw Error('Must keep audio');},
    pc_log: () => {}, LOG_LEVEL_INFO: 1, LOG_LEVEL_WARN: 2,
    applyMediaDirection: () => {}, ccallStartGatherHandler: () => {},
    ccallLocalSdpHandler: (_pc, err, _type, sdp) => {assert.equal(err, 0); offer = sdp;}});
  ctx.createSdp(pc, 0, 0, true);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(offer, rawSdp);
});

test('late source capture after stop is discarded without publishing a new source', async () => {
  const audio = track('audio', 'mic');
  const oldVideo = track('video', 'camera');
  const screen = track('video', 'screen');
  let complete;
  const pc = {rtc: {getSenders: () => [{track: audio}, {track: oldVideo}]}, publisher_video_source: 'camera'};
  const ctx = runtime({userMediaHandler: () => new Promise(resolve => {complete = resolve;})});
  const pending = ctx.preparePublisherTracks(pc, false, true);
  pc.rtc = null;
  complete(stream([screen]));
  await assert.rejects(pending, /closed during media capture/);
  assert.equal(screen.stops, 1);
  assert.equal(audio.stops, 0);
  assert.equal(pc.publisher_video_source, 'camera');
});


test('publisher replaceTrack API requests native refresh instead of browser replacement', () => {
  const oldVideo = track('video', 'camera-one');
  const nextVideo = track('video', 'camera-two');
  const pc = {self: 17, media_direction: 1, rtc: {getSenders: () => [{track: oldVideo}]}};
  const calls = [];
  const ctx = runtime({PC_MEDIA_SENDONLY: 1, PC_MEDIA_RECVONLY: 2,
    connectionsStore: {getPeerConnectionByConvid: () => [pc]},
    em_module: {ccall: (...args) => {calls.push(args); return 0;}},
    replace_track: () => {throw Error('Must use publisher UPDATE');}});
  ctx.pc_ReplaceTrack('call', nextVideo);
  assert.equal(pc.pending_video_track, nextVideo);
  assert.equal(calls.length, 1);
  assert.equal(calls[0][0], 'pc_video_source_changed_handler');
  assert.equal(oldVideo.stops, 0);
});

test('explicit camera track bypasses same-source capture shortcut and creates a new sender', async () => {
  const audio = track('audio', 'mic');
  const oldVideo = track('video', 'camera-one');
  const nextVideo = track('video', 'camera-two');
  const added = [];
  const sender = {track: oldVideo};
  const pc = {pending_video_track: nextVideo, publisher_video_source: 'camera', rtc: {
    getSenders: () => [{track: audio}, sender],
    getTransceivers: () => [{sender, stop() {}}], removeTrack: () => {sender.track = null;},
    addTransceiver: value => added.push(value)}};
  const ctx = runtime({MediaStream: class {constructor(tracks) {Object.assign(this, stream(tracks));}},
    userMediaHandler: () => {throw Error('Must use supplied camera track');}});
  await ctx.preparePublisherTracks(pc, true, false);
  assert.equal(pc.pending_video_track, undefined);
  assert.equal(oldVideo.stops, 1);
  assert.equal(audio.stops, 0);
  assert.deepEqual(added, [nextVideo]);
});

test('pending camera change retries only after the current remote answer completes', async () => {
  let complete;
  const calls = [];
  const pc = {self: 99, media_direction: 1, pending_video_track: track('video', 'next'), rtc: {
    localDescription: {type: 'offer', sdp: 'offer'}, getTransceivers: () => [],
    setRemoteDescription: () => new Promise(resolve => {complete = resolve;})}};
  const ctx = runtime({pc_log: () => {}, LOG_LEVEL_INFO: 1, LOG_LEVEL_WARN: 2,
    connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: value => value, ccall: (...args) => {calls.push(args); return 1;}},
    pc_env: 0, ENV_FIREFOX: 1, CONV_TYPE_CONFERENCE: 2, PC_MEDIA_SENDONLY: 1,
    sdpDirectionSummary: () => '', extractSSRCs: () => {}});
  ctx.pc_SetRemoteDescription(1, 'answer', 'answer');
  assert.equal(calls.length, 0);
  complete();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(calls[0][0], 'pc_sdp_applied_handler');
  assert.equal(calls[1][0], 'pc_video_source_changed_handler');
  assert.ok(pc.pending_video_track);
});


test('turning video off discards a queued camera track rather than keeping capture alive', async () => {
  const audio = track('audio', 'mic');
  const pending = track('video', 'pending-camera');
  const pc = {pending_video_track: pending, rtc: {
    getSenders: () => [{track: audio}], getTransceivers: () => []}};
  const ctx = runtime({userMediaHandler: () => {throw Error('No capture expected');}});
  await ctx.preparePublisherTracks(pc, false, false);
  assert.equal(pc.pending_video_track, undefined);
  assert.equal(pending.stops, 1);
  assert.equal(audio.stops, 0);
});


test('reserved server-events channel never reaches ECall message or lifecycle handlers', () => {
  let callbacks = 0;
  const pc = {self: 1, convid: 'call', closing: false, ignoredDataChannels: {}};
  const dc = {label: 'server-events'};
  const ctx = runtime({
    LOG_LEVEL_WARN: 2, CATALOG_DATA_CHANNEL_LABEL: 'catalog',
    connectionsStore: {storeDataChannel: () => 7},
    ccallDcEstabHandler: () => ++callbacks,
    ccallDcStateChangeHandler: () => ++callbacks,
    ccallDcDataHandler: () => ++callbacks,
  });
  ctx.dataChannelHandler(pc, {channel: dc});
  dc.onopen();
  dc.onmessage({data: '{"event":"transport-ready"}'});
  dc.onerror({});
  dc.onclose();
  assert.equal(callbacks, 0);
});


test('additional peer channels preserve existing subscriptions and forward messages', () => {
  let closed = 0, stateChanges = 0;
  const received = [];
  const old = {close: () => ++closed};
  const pc = {self: 1, applicationDcHnd: 1, closing: false, ignoredDataChannels: {},
    rtc: {createDataChannel: (label, options) => ({label, id: options.id})}};
  const channels = [];
  const ctx = runtime({
    CATALOG_DATA_CHANNEL_LABEL: 'catalog', DC_STATE_OPEN: 1, LOG_LEVEL_WARN: 2,
    em_module: {UTF8ToString: () => 'calling-3.0/peer-b'},
    connectionsStore: {getPeerConnection: () => pc, getDataChannel: () => old,
      storeDataChannel: dc => {channels.push(dc); return 2;}},
    ccallDcStateChangeHandler: () => ++stateChanges,
    ccallDcDataHandler: (_, data) => received.push(data),
  });
  assert.equal(ctx.pc_CreateNegotiatedDataChannel(1, 1, 6), 2);
  assert.equal(closed, 0);
  assert.equal(pc.applicationDcHnd, 1);
  channels[0].onopen();
  channels[0].onmessage({data: 'peer-message'});
  channels[0].onclose();
  channels[0].onerror({});
  assert.equal(stateChanges, 1);
  assert.deepEqual(received, ['peer-message']);
});

test('primary negotiated channel still replaces its provisional predecessor', () => {
  let closed = 0;
  const pc = {self: 1, applicationDcHnd: 1, closing: false, ignoredDataChannels: {},
    rtc: {createDataChannel: label => ({label})}};
  const ctx = runtime({
    CATALOG_DATA_CHANNEL_LABEL: 'catalog',
    em_module: {UTF8ToString: () => 'calling-3.0'},
    connectionsStore: {getPeerConnection: () => pc,
      getDataChannel: () => ({close: () => ++closed}), storeDataChannel: () => 2},
  });
  assert.equal(ctx.pc_CreateNegotiatedDataChannel(1, 1, 2), 2);
  assert.equal(closed, 1);
  assert.equal(pc.applicationDcHnd, 2);
  assert.equal(pc.ignoredDataChannels[1], true);
});


test('closing the owner releases every subscribed channel and suppresses late callbacks', () => {
  const closed = [], removed = [];
  let callbacks = 0, rtcClosed = 0;
  const pc = {self: 1, convid: 'call', applicationDcHnd: 1, closing: false,
    ignoredDataChannels: {}, rtc: {getTransceivers: () => [], close: () => ++rtcClosed}};
  const channels = new Map();
  const ctx = runtime({
    CATALOG_DATA_CHANNEL_LABEL: 'catalog',
    connectionsStore: {storeDataChannel: dc => {const id = channels.size + 1; channels.set(id, dc); return id;},
      getPeerConnection: () => pc, getDataChannel: id => channels.get(id),
      removeDataChannel: id => removed.push(id), removePeerConnection: () => {}},
    worker: {postMessage: () => {}},
    ccallDcStateChangeHandler: () => ++callbacks,
  });
  for (const label of ['calling-3.0/peer-b', 'calling-3.0/peer-c']) {
    const dc = {label, close: () => {closed.push(label); dc.onclose();}};
    ctx.setupDataChannel(pc, dc);
  }
  ctx.pc_Close(1);
  assert.equal(closed.length, 2);
  assert.deepEqual(removed, [1, 2]);
  assert.equal(callbacks, 0);
  assert.equal(rtcClosed, 1);
});


test('receive MID bindings remove only deselected media without closing transport', () => {
  const audio = track('audio', 'audio-b'), video = track('video', 'video-b');
  const pc = {convid: 'call', receiveTracks: {'1': {userId: 'b', clientId: 'two', kind: 'audio'}, '2': {userId: 'b', clientId: 'two', kind: 'video'}},
    rtc: {getTransceivers: () => [{mid: '1', receiver: {track: audio}}, {mid: '2', receiver: {track: video}}]}};
  const removed = [];
  const ctx = runtime({worker: {postMessage: () => {}}, connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: () => JSON.stringify([{mid: '1', userId: 'b', clientId: 'two', kind: 'audio'}])},
    videoStreamHandler: (...args) => removed.push(args), audioStreamHandler: () => assert.fail('audio removed')});
  assert.equal(ctx.pc_SetReceiveTracks(1, 1), 0);
  assert.equal(audio.enabled, true);
  assert.equal(video.enabled, false);
  assert.equal(removed.length, 1);
  assert.equal(removed[0][1], 'b');
  assert.equal(Object.keys(pc.receiveTracks).length, 1);
});

test('receive transform carries participant binding and track ID to worker', () => {
  const messages = [];
  const readable = {}, writable = {};
  const receiver = {track: {id: 'received-b', kind: 'audio'}, createEncodedStreams: () => ({readable, writable})};
  const ctx = runtime({insertableLegacy: false, insertableStreams: true, pc_env: 1, ENV_FIREFOX: 2,
    worker: {postMessage: (message) => messages.push(message)}});
  ctx.setupReceiverTransform({self: 7}, receiver, '["b","two"]');
  assert.equal(messages[0].participant, '["b","two"]');
  assert.equal(messages[0].track_id, 'received-b');
});

test('worker script parses and associates bound frames with participant rather than SFU SSRC', async () => {
  const declaration = ast.statements.find(node => ts.isVariableStatement(node) &&
    node.declarationList.declarations.some(item => item.name.getText(ast) === 'workerContent'));
  const template = declaration.declarationList.declarations[0].initializer.text;
  const workerAst = ts.createSourceFile('worker.js', template, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
  const decrypt = workerAst.statements.find(node => ts.isFunctionDeclaration(node) && node.name.text === 'decryptFrame').getText(workerAst);
  new vm.Script(template); // Also validates the embedded script, which tsc treats as a string.
  let selectedIv;
  const ctx = vm.createContext({doLog: () => {}, postMessage: () => {},
    xor_iv: (iv) => iv,
    frameHeaderDecode: () => ({csrc: 'original-publisher-ssrc', frameId: 1, keyId: 1, len: 1}),
    crypto: {subtle: {decrypt: (_options, _key, _data) => {selectedIv = _options.iv; return Promise.resolve(new ArrayBuffer(1));}}}});
  vm.runInContext(decrypt, ctx);
  // The chosen participant has no publisher SSRC mapping; only the MID-derived binding identifies it.
  const user = {userid: 'b', clientid: 'two', iva: new Uint8Array(12), ivv: new Uint8Array(12), first_recv_audio: true};
  const coder = {participants: {'["b","two"]': user}, audio: {users: {}}, video: {users: {}}, keys: [{id: 1, key: {}}]};
  let frames = 0;
  const frame = {data: new ArrayBuffer(4), getMetadata: () => ({synchronizationSource: 999})};
  ctx.decryptFrame('track-b', coder, frame, {enqueue: () => ++frames}, '["b","two"]');
  await Promise.resolve();
  assert.equal(selectedIv, user.iva);
  assert.equal(frames, 1);
  coder.receiverParticipants = {'track-b': null};
  ctx.decryptFrame('track-b', coder, frame, {enqueue: () => ++frames}, '["b","two"]');
  await Promise.resolve();
  assert.equal(frames, 1);
  delete coder.receiverParticipants['track-b'];
  // A supplied but unknown MID binding must not fall back to another participant's SSRC.
  coder.audio.users['original-publisher-ssrc'] = user;
  ctx.decryptFrame('track-b', coder, frame, {enqueue: () => ++frames}, '["unknown","client"]');
  await Promise.resolve();
  assert.equal(frames, 1);
});


test('subscriber answer preserves SDP and reports fresh gathering only after local application', async () => {
  let complete, received, gathered = 0;
  const pc = {self: 7, media_direction: 2, conv_type: 0, rtc: {iceGatheringState: 'complete',
    setLocalDescription: description => {received = description; return new Promise(resolve => {complete = resolve;});}}};
  const ctx = runtime({connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: value => value}, pc_env: 1, ENV_FIREFOX: 1, CONV_TYPE_CONFERENCE: 2,
    sdpMap: () => assert.fail('Subscriber answer must stay intact')});
  ctx.gatheringComplete = () => ++gathered;
  ctx.pc_SetLocalDescription(1, 'answer', 'fresh-subscriber-answer');
  assert.equal(received.sdp, 'fresh-subscriber-answer');
  assert.equal(gathered, 0);
  complete();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(gathered, 1);
});

test('subscriber offer application failure reaches native transport error handler', async () => {
  const calls = [];
  const pc = {self: 8, media_direction: 2, rtc: {getTransceivers: () => [],
    setRemoteDescription: () => Promise.reject(Error('invalid offer'))}};
  const ctx = runtime({connectionsStore: {getPeerConnection: () => pc},
    em_module: {UTF8ToString: value => value, ccall: (...args) => calls.push(args)},
    pc_env: 0, ENV_FIREFOX: 1, CONV_TYPE_CONFERENCE: 2, LOG_LEVEL_WARN: 2, sdpDirectionSummary: () => '', extractSSRCs: () => {}});
  ctx.pc_SetRemoteDescription(1, 'offer', 'invalid-offer');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(calls.length, 1);
  assert.equal(calls[0][0], 'pc_sdp_applied_handler');
  assert.deepEqual(Array.from(calls[0][3]), [8, 0, 1]);
});
