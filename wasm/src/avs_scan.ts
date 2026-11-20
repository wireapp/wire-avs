const SCAN_TIMEOUT_MS = 10000;

interface ConfiguredServer {
  urls: string;
  username?: string;
  credential?: string;
}

export interface ScanCandidate {
  id: string;
  type: RTCIceCandidateType | null;
  ip: string | null;
  hostname?: string;
  port: number | null;
  protocol: RTCIceProtocol | null;
  serverUrl: string | null;
}

export interface ScanRun {
  serverUrl: string | null;
  requestedTransport: "udp" | "tcp" | null;
  status: "complete" | "timeout" | "failed";
  gatheringDurationMs: number | null;
  localCandidates: ScanCandidate[];
  errors: { url: string | null; code: number | null; message: string }[];
}

export interface ScanReport {
  schemaVersion: 1;
  runs: ScanRun[];
}

function requestedTransport(url: string | null): ScanRun["requestedTransport"] {
  if (!url) return null;
  const transport = /[?&]transport=(udp|tcp)(?:&|$)/i.exec(url);
  if (transport) return transport[1].toLowerCase() as "udp" | "tcp";
  if (/^(stuns|turns):/i.test(url)) return "tcp";
  if (/^(stun|turn):/i.test(url)) return "udp";
  return null;
}

export function normalizeIceCandidate(candidate: RTCIceCandidate, id: string): ScanCandidate {
  const address = candidate.address || null;
  const isIp = address !== null && (address.includes(":") || /^\d+(\.\d+){3}$/.test(address));
  return {
    id,
    type: candidate.type ?? null,
    ip: isIp ? address : null,
    ...(address && !isIp ? { hostname: address } : {}),
    port: candidate.port ?? null,
    protocol: candidate.protocol ?? null,
    serverUrl: (candidate as RTCIceCandidate & { url?: string }).url || null,
  };
}

function gather(server: ConfiguredServer | null, index: number, signal: AbortSignal): Promise<ScanRun> {
  return new Promise(resolve => {
    const result: ScanRun = {
      serverUrl: server?.urls ?? null,
      requestedTransport: requestedTransport(server?.urls ?? null),
      status: "failed",
      gatheringDurationMs: null,
      localCandidates: [],
      errors: [],
    };
    let pc: RTCPeerConnection | undefined;
    let startedAt: number | undefined;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let finished = false;

    function finish(status: ScanRun["status"]) {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      signal.removeEventListener("abort", abort);
      result.status = status;
      if (startedAt !== undefined) {
        result.gatheringDurationMs = Math.round(performance.now() - startedAt);
      }
      if (pc) {
        pc.onicecandidate = null;
        pc.onicecandidateerror = null;
        pc.close();
      }
      resolve(result);
    }

    function abort() { finish("failed"); }

    function fail(error: unknown) {
      if (finished) return;
      result.errors.push({ url: result.serverUrl, code: null, message: String(error) });
      finish("failed");
    }

    if (signal.aborted) {
      abort();
      return;
    }
    signal.addEventListener("abort", abort);
    try {
      const rtc = new RTCPeerConnection({ iceServers: server ? [server] : [] });
      pc = rtc;
      rtc.onicecandidate = ({ candidate }) => {
        if (finished) return;
        if (candidate === null) {
          finish("complete");
        } else if (candidate.candidate) {
          result.localCandidates.push(normalizeIceCandidate(candidate, `${index}:${result.localCandidates.length}`));
        }
      };
      rtc.onicecandidateerror = event => {
        const error = event as RTCPeerConnectionIceErrorEvent;
        result.errors.push({ url: error.url || null, code: error.errorCode, message: error.errorText });
      };
      rtc.createDataChannel("ice-scan");
      timer = setTimeout(() => finish("timeout"), SCAN_TIMEOUT_MS);
      rtc.createOffer().then(offer => {
        if (finished) return;
        clearTimeout(timer);
        startedAt = performance.now();
        timer = setTimeout(() => finish("timeout"), SCAN_TIMEOUT_MS);
        return rtc.setLocalDescription(offer);
      }).catch(fail);
    } catch (error) {
      fail(error);
    }
  });
}

async function scan(config: string, signal: AbortSignal): Promise<ScanReport> {
  const { ice_servers: entries = [] }: {
    ice_servers?: { urls?: string | string[]; url?: string; username?: string; credential?: string }[];
  } = JSON.parse(config);
  if (!Array.isArray(entries)) throw new Error("Invalid ICE server list");
  const servers: ConfiguredServer[] = [];
  for (const entry of entries) {
    const urls = entry.urls ?? entry.url;
    for (const url of Array.isArray(urls) ? urls : [urls]) {
      if (typeof url === "string" && url) {
        servers.push({ urls: url, username: entry.username, credential: entry.credential });
      }
    }
  }
  if (entries.length && !servers.length) throw new Error("ICE server list contains no URLs");
  const inputs = servers.length ? servers : [null];
  const runs: ScanRun[] = [];
  for (const server of inputs) {
    if (signal.aborted) break;
    runs.push(await gather(server, runs.length, signal));
  }
  return { schemaVersion: 1, runs };
}

export function installScanDriver(module: any) {
  const active = new Map<number, { id: number; abort: AbortController }>();
  const pending = new Map<number, { resolve: (report: ScanReport) => void; reject: (error: Error) => void }>();
  const run = module.addFunction((wuser: number, id: number, config: number) => {
    if (!config) {
      const current = active.get(wuser);
      if (current?.id === id) {
        active.delete(wuser);
        current.abort.abort();
      }
      return;
    }
    const abort = new AbortController();
    active.set(wuser, { id, abort });
    const complete = (json: string | null) => {
      if (active.get(wuser)?.id !== id) return;
      active.delete(wuser);
      // A full multi-server report may be larger than the ccall stack.
      const size = json === null ? 0 : module.lengthBytesUTF8(json) + 1;
      const buffer = size ? module._malloc(size) : 0;
      try {
        if (buffer) module.stringToUTF8(json, buffer, size);
        module.ccall("wcall_scan_complete", null, ["number", "number", "number"], [wuser, id, buffer]);
      } finally {
        if (buffer) module._free(buffer);
      }
    };
    scan(module.UTF8ToString(config), abort.signal).then(
      report => complete(JSON.stringify(report)),
      () => complete(null),
    );
  }, "viii");
  const result = module.addFunction((wuser: number, err: number, json: number) => {
    const request = pending.get(wuser);
    pending.delete(wuser);
    if (!request) return;
    if (err) {
      request.reject(new Error(`ICE scan failed (${err})`));
    } else {
      try {
        request.resolve(JSON.parse(module.UTF8ToString(json)));
      } catch (error) {
        request.reject(new Error(`Invalid ICE scan result: ${error}`));
      }
    }
  }, "viii");
  module.ccall("wcall_scan_set_driver", null, ["number", "number"], [run, result]);

  return (wuser: number): Promise<ScanReport> => new Promise((resolve, reject) => {
    if (pending.has(wuser)) {
      reject(new Error("ICE scan already running"));
      return;
    }
    pending.set(wuser, { resolve, reject });
    try {
      const err = module.ccall("wcall_start_scan", "number", ["number"], [wuser]);
      if (err) throw new Error(`Could not start ICE scan (${err})`);
    } catch (error) {
      pending.delete(wuser);
      reject(error);
    }
  });
}
