// Run from the repository root: node wasm/test/publisherUpdate.test.js
// Uses the existing TypeScript installation in build/dist/wasm.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('../../build/dist/wasm/node_modules/typescript');
const {test} = require('node:test');
const source = fs.readFileSync(require('node:path').join(__dirname, '../src/avs_pc.ts'), 'utf8');
const ast = ts.createSourceFile('avs_pc.ts', source, ts.ScriptTarget.Latest, true);
const names = ['discardPublisherVideo', 'preparePublisherTracks', 'pc_SetRemoteDescription', 'pc_SetLocalDescription', 'gatheringComplete'];
const functions = ast.statements.filter(node => ts.isFunctionDeclaration(node) &&
  node.name && names.includes(node.name.text)).map(node => node.getText(ast)).join('\n');
assert.equal(ast.statements.filter(node => ts.isFunctionDeclaration(node) &&
  node.name && names.includes(node.name.text)).length, names.length);
const code = ts.transpileModule(functions, {compilerOptions: {target: ts.ScriptTarget.ES2017}}).outputText;
function runtime(globals) {
  const context = vm.createContext({Promise, Error, ...globals});
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
