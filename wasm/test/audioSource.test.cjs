const assert = require("node:assert/strict");
const { webcrypto } = require("node:crypto");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");
const { test } = require("node:test");
const { createContext, runInContext } = require("node:vm");
const ts = require("typescript");

// Compile the actual shipping source, including its embedded worker, without a native WASM build.
const pcSource = ts.transpileModule(
  readFileSync(join(__dirname, "../src/avs_pc.ts"), "utf8"),
  {
    compilerOptions: {
      module: ts.ModuleKind.CommonJS,
      target: ts.ScriptTarget.ES5,
    },
  }
).outputText;

function peerContext(extra = {}) {
  const context = createContext({ exports: {}, Blob, console, ...extra });
  runInContext(pcSource, context);
  context.em_module = {
    ccall() {},
    UTF8ToString: (value) => (value === 1 ? "user" : "client"),
  };
  return context;
}

async function workerContext() {
  const pc = peerContext();
  const messages = [];
  let decryption;
  let now = 0;
  const context = createContext({
    postMessage: (message) => messages.push(message),
    performance: { now: () => now },
    console: { log() {} },
    Uint8Array,
    ArrayBuffer,
    DataView,
    crypto: {
      subtle: {
        decrypt: (...args) => {
          decryption = webcrypto.subtle.decrypt(...args);
          return decryption;
        },
      },
    },
  });
  runInContext(pc.workerContent, context);
  const key = await webcrypto.subtle.importKey(
    "raw",
    new Uint8Array(32).fill(7),
    "AES-GCM",
    false,
    ["encrypt", "decrypt"]
  );
  const user = (userid, clientid) => ({
    userid,
    clientid,
    iva: new Uint8Array(12),
    ivv: new Uint8Array(12),
  });
  const coder = {
    self: 7,
    keys: [{ id: 1, key }],
    audio: {
      users: { 42: user("first", "client-1"), 99: user("second", "client-2") },
    },
  };
  async function receive(
    source = 42,
    { track = "receiver-1", corrupt = false } = {}
  ) {
    const header = context.frameHeaderEncode(1, 1, source);
    const encrypted = await webcrypto.subtle.encrypt(
      {
        name: "AES-GCM",
        iv: context.xor_iv(coder.audio.users[String(source)].iva, 1, 1),
        additionalData: header,
        tagLength: 128,
      },
      key,
      new Uint8Array([1, 2, 3])
    );
    const data = Uint8Array.from([...header, ...new Uint8Array(encrypted)]);
    if (corrupt) data[data.length - 1] ^= 1;
    const frame = {
      data: data.buffer,
      getMetadata: () => ({
        synchronizationSource: 123,
        contributingSources: [],
      }),
      [Symbol.toStringTag]: "RTCEncodedAudioFrame",
    };
    const output = [];
    context.decryptFrame(track, coder, frame, {
      enqueue: (value) => output.push(value),
    });
    await decryption.catch(() => {});
    await Promise.resolve();
    return output;
  }
  const identities = () =>
    messages
      .filter((message) => message.op === "audioSource")
      .map((message) => ({ ...message }));
  return {
    context,
    coder,
    receive,
    identities,
    advance: (milliseconds) => {
      now += milliseconds;
    },
  };
}

test("reports authenticated sender switches once per receiver and keeps the audio frames", async () => {
  const { receive, identities, advance } = await workerContext();
  assert.equal((await receive()).length, 1);
  await receive();
  advance(1000);
  await receive(99);
  await receive(99, { track: "receiver-2" });
  assert.deepEqual(identities(), [
    {
      op: "audioSource",
      self: 7,
      track_id: "receiver-1",
      userid: "first",
      clientid: "client-1",
    },
    {
      op: "audioSource",
      self: 7,
      track_id: "receiver-1",
      userid: "second",
      clientid: "client-2",
    },
    {
      op: "audioSource",
      self: 7,
      track_id: "receiver-2",
      userid: "second",
      clientid: "client-2",
    },
  ]);
});

test("includes client identity when the same user changes sender device", async () => {
  const { coder, receive, identities, advance } = await workerContext();
  coder.audio.users["99"].userid = "first";
  await receive();
  advance(1000);
  await receive(99);
  assert.deepEqual(
    identities().map((message) => message.clientid),
    ["client-1", "client-2"]
  );
});

test("never reports failed authentication and does not poison a later valid identity", async () => {
  const { receive, identities } = await workerContext();
  assert.equal((await receive(42, { corrupt: true })).length, 0);
  assert.deepEqual(identities(), []);
  assert.equal((await receive()).length, 1);
  assert.equal(identities().length, 1);
});

test("keeps accepting audio when there is no receiver track ID", async () => {
  const { receive, identities } = await workerContext();
  assert.equal((await receive(42, { track: null })).length, 1);
  assert.deepEqual(identities(), []);
});

test("extends the stream callback with metadata only when requested", () => {
  const context = peerContext({
    URL: { createObjectURL: () => "blob:test" },
    Worker: function () {},
  });
  const calls = [];
  const streams = [{}];
  const track = { readyState: "live" };
  const pc = {
    convid: "conversation",
    rtc: {},
    audioStreams: {
      receiver: { streamId: "original-stream-id", streams, track },
    },
  };
  context.connectionsStore = {
    getPeerConnectionBySelf: (self) => (self === 7 ? [pc] : []),
  };
  const handler = (...args) => calls.push(args);
  context.exports.default.setAudioStreamHandler(handler);
  context.createWorker();
  const message = {
    op: "audioSource",
    self: 7,
    track_id: "receiver",
    userid: "user",
    clientid: "client",
  };
  context.worker.onmessage({ data: message });
  assert.equal(calls.length, 0);
  context.exports.default.setAudioStreamHandler(handler, true);
  context.worker.onmessage({ data: message });
  context.worker.onmessage({ data: { ...message, self: 99 } });
  context.worker.onmessage({ data: { ...message, track_id: "unknown" } });
  track.readyState = "ended";
  context.worker.onmessage({ data: message });
  assert.equal(calls.length, 1);
  assert.equal(calls[0][1], "original-stream-id");
  assert.equal(calls[0][2], streams);
  assert.deepEqual(
    { ...calls[0][3] },
    { trackId: "receiver", userId: "user", clientId: "client" }
  );
});

test("preserves receiver track IDs through both encoded transform APIs", async () => {
  const context = peerContext({
    RTCRtpScriptTransform: function (worker, options) {
      this.options = options;
    },
  });
  const sent = [];
  context.worker = { postMessage: (message) => sent.push(message) };
  context.insertableStreams = true;
  const receiver = {
    track: { id: "receiver", kind: "audio" },
    createEncodedStreams: () => ({ readable: {}, writable: {} }),
  };
  context.setupReceiverTransform({ self: 7 }, receiver);
  assert.equal(sent[0].track_id, "receiver");
  context.pc_env = 1;
  context.setupReceiverTransform({ self: 7 }, receiver);
  assert.equal(receiver.transform.options.track_id, "receiver");
  const { context: worker, coder } = await workerContext();
  worker.coders[7] = coder;
  const configured = [];
  worker.setupReceiver = (...args) => configured.push(args);
  await worker.onmessage({ data: sent[0] });
  assert.equal(configured[0][3], "receiver");
});

test("includes a known direct-call identity in the initial stream callback", () => {
  const context = peerContext({ RTCPeerConnection: function () {} });
  const calls = [];
  const pc = {
    self: 7,
    convid: "conversation",
    remote_userid: "user",
    remote_clientid: "client",
    turnServers: [],
  };
  context.connectionsStore = { getPeerConnection: () => pc };
  context.exports.default.setAudioStreamHandler(
    (...args) => calls.push(args),
    true
  );
  context.pc_Create(7, 0, 0);
  pc.rtc.ontrack({ track: { id: "receiver", kind: "audio" }, streams: [] });
  assert.equal(calls.length, 1);
  assert.deepEqual(
    { ...calls[0][3] },
    { trackId: "receiver", userId: "user", clientId: "client" }
  );
});

test("updates late direct-call metadata without replacing the original streams or stream ID", () => {
  const context = peerContext({ RTCPeerConnection: function () {} });
  const calls = [];
  const pc = {
    self: 7,
    convid: "conversation",
    remote_userid: "",
    remote_clientid: "",
    turnServers: [],
  };
  context.connectionsStore = { getPeerConnection: () => pc };
  context.exports.default.setAudioStreamHandler(
    (...args) => calls.push(args),
    true
  );
  context.pc_Create(7, 0, 0);
  const track = {
    id: "receiver",
    kind: "audio",
    readyState: "live",
    enabled: true,
  };
  const streams = [{ getTracks: () => [track] }];
  pc.rtc.ontrack({ track, streams });
  assert.equal(calls[0][3], undefined);
  context.pc_SetRemoteUserClientId(7, 1, 2);
  assert.equal(calls.length, 2);
  assert.equal(calls[1][1], calls[0][1]);
  assert.equal(calls[1][2], streams);
  assert.deepEqual(
    { ...calls[1][3] },
    { trackId: "receiver", userId: "user", clientId: "client" }
  );
  track.readyState = "ended";
  context.pc_SetRemoteUserClientId(7, 1, 2);
  assert.equal(calls.length, 2);
});

test("preserves the original callback behavior for clients that do not opt in", () => {
  const context = peerContext({ RTCPeerConnection: function () {} });
  const calls = [];
  const pc = {
    self: 7,
    convid: "conversation",
    remote_userid: "user",
    remote_clientid: "client",
    turnServers: [],
  };
  context.connectionsStore = { getPeerConnection: () => pc };
  context.exports.default.setAudioStreamHandler((...args) => calls.push(args));
  context.pc_Create(7, 0, 0);
  const streams = [];
  pc.rtc.ontrack({ track: { id: "receiver", kind: "audio" }, streams });
  context.pc_SetRemoteUserClientId(7, 1, 2);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].length, 3);
  assert.equal(calls[0][2], streams);
  assert.equal(calls[0][3], undefined);
});

test("does not revive a removed stream when sender metadata arrives later", () => {
  const context = peerContext({
    URL: { createObjectURL: () => "blob:test" },
    Worker: function () {},
  });
  const calls = [];
  const streams = [{}];
  const pc = {
    convid: "conversation",
    rtc: {},
    users: { receiver: {} },
    audioStreams: {
      receiver: {
        streamId: "7receiver",
        streams,
        track: { readyState: "live" },
      },
    },
  };
  context.connectionsStore = {
    getPeerConnection: () => pc,
    getPeerConnectionBySelf: () => [pc],
  };
  context.em_module.UTF8ToString = () => "receiver";
  context.exports.default.setAudioStreamHandler(
    (...args) => calls.push(args),
    true
  );
  context.createWorker();
  context.pc_RemoveUserInfo(7, 1);
  context.worker.onmessage({
    data: {
      op: "audioSource",
      self: 7,
      track_id: "receiver",
      userid: "user",
      clientid: "client",
    },
  });
  assert.equal(calls.length, 1);
  assert.equal(calls[0][1], "7receiver");
  assert.equal(calls[0][2], null);
});

test("limits source notifications per receiver without dropping audio or forgetting suppressed changes", async () => {
  const { receive, identities, advance } = await workerContext();
  assert.equal((await receive()).length, 1);
  advance(999);
  assert.equal((await receive(99)).length, 1);
  assert.equal(identities().length, 1);
  // A separate receiver is not blocked by the first receiver's guard.
  assert.equal((await receive(99, { track: "receiver-2" })).length, 1);
  assert.equal(identities().length, 2);
  advance(1);
  assert.equal((await receive(99)).length, 1);
  assert.deepEqual(
    identities().map((message) => [message.track_id, message.userid]),
    [
      ["receiver-1", "first"],
      ["receiver-2", "second"],
      ["receiver-1", "second"],
    ]
  );
  advance(1000);
  await receive(99);
  assert.equal(identities().length, 3);
});
