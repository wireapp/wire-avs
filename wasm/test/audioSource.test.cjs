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
  const context = createContext({
    postMessage: (message) => messages.push(message),
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
  return { context, coder, receive, identities };
}

test("reports authenticated sender switches once per receiver and keeps the audio frames", async () => {
  const { receive, identities } = await workerContext();
  assert.equal((await receive()).length, 1);
  await receive();
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
  const { coder, receive, identities } = await workerContext();
  coder.audio.users["99"].userid = "first";
  await receive();
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

test("forwards worker identities to the matching conversation and allows unregistering", () => {
  const context = peerContext({
    URL: { createObjectURL: () => "blob:test" },
    Worker: function () {},
  });
  const calls = [];
  context.connectionsStore = {
    getPeerConnectionBySelf: (self) =>
      self === 7 ? [{ convid: "conversation" }] : [],
  };
  context.exports.default.setAudioSourceHandler((...args) => calls.push(args));
  context.createWorker();
  const message = {
    op: "audioSource",
    self: 7,
    track_id: "receiver",
    userid: "user",
    clientid: "client",
  };
  context.worker.onmessage({ data: message });
  context.worker.onmessage({ data: { ...message, self: 99 } });
  context.exports.default.setAudioSourceHandler(null);
  context.worker.onmessage({ data: message });
  assert.deepEqual(calls, [["conversation", "receiver", "user", "client"]]);
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

test("reports direct-call tracks without requiring a legacy audio stream handler", () => {
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
  context.exports.default.setAudioSourceHandler((...args) => calls.push(args));
  context.pc_Create(7, 0, 0);
  pc.rtc.ontrack({ track: { id: "receiver", kind: "audio" }, streams: [] });
  assert.deepEqual(calls, [["conversation", "receiver", "user", "client"]]);
});

test("reports a late direct-call identity for live audio receivers only", () => {
  const context = peerContext();
  const calls = [];
  const pc = {
    convid: "conversation",
    conv_type: 0,
    rtc: {
      getReceivers: () => [
        { track: { id: "audio", kind: "audio", readyState: "live" } },
        { track: { id: "ended", kind: "audio", readyState: "ended" } },
        { track: { id: "video", kind: "video", readyState: "live" } },
      ],
    },
  };
  context.connectionsStore = { getPeerConnection: () => pc };
  context.exports.default.setAudioSourceHandler((...args) => calls.push(args));
  context.pc_SetRemoteUserClientId(7, 1, 2);
  assert.deepEqual(calls, [["conversation", "audio", "user", "client"]]);
  pc.conv_type = context.CONV_TYPE_CONFERENCE;
  context.pc_SetRemoteUserClientId(7, 1, 2);
  assert.equal(calls.length, 1);
});
