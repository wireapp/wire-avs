import {normalizeIceCandidate, ScanCandidate} from './avs_scan';

type TestStep = 'ice_gathering_finished' | 'connected' | 'media_flow' | 'measurement_finished';
type CandidateSummary = {localCandidateFound: boolean; relayCandidateFound: boolean};

export type ConnectionTestProgress = {
  step: TestStep;
  status: 'ok' | 'error';
  candidates: CandidateSummary;
  error?: string;
};

export interface ConnectionTestResult {
  measurementDurationMs: number;
  device: {status: 'ok'; microphone: 'ready'; localCandidates: ScanCandidate[]};
  network: {
    status: 'ok' | 'warning' | 'unknown';
    selectedCandidateProtocol: 'udp' | 'tcp' | null;
    roundTripTimeMs: number | null;
    jitterMs: number | null;
    packetLossPercent: number | null;
  };
  wireServer: {
    status: 'ok' | 'warning' | 'unknown';
    target: 'sft';
    connectionStatus: 'connected';
    route: 'direct' | 'turn' | null;
    failureReason: null;
  };
}

const SETUP_MS = 20000;
const MEASUREMENT_MS = 20000;
const POLL_MS = 500;
const steps: TestStep[] = ['ice_gathering_finished', 'connected', 'media_flow', 'measurement_finished'];

interface AudioStats {
  sent: number;
  received: number;
  lost: number | null;
  jitter: number | null;
  uplinkJitter: number | null;
  uplinkLossPercent: number | null;
  uplinkLost: Map<string, number>;
}

interface TestSession {
  convid?: string;
  rtc?: RTCPeerConnection;
  candidates: ScanCandidate[];
  microphoneReady: boolean;
  mediaEstablished: boolean;
  step: number;
  fail: (message: string, end?: boolean) => void;
}

const peers = new Map<string, TestSession>();

export function observeTestPeer(convid: string, rtc: RTCPeerConnection) {
  const test = peers.get(convid);
  if (!test) return;
  if (test.rtc && test.step > 0) {
    test.fail('WebRTC connection was replaced');
    return;
  }
  test.rtc = rtc;
  test.candidates = [];
  test.microphoneReady = false;
  test.mediaEstablished = false;
}

export function observeTestMicrophone(convid: string, rtc: RTCPeerConnection, stream: MediaStream) {
  const test = peers.get(convid);
  if (!test || test.rtc !== rtc) return;
  test.microphoneReady = stream.getAudioTracks().some(track => track.readyState === 'live');
  if (!test.microphoneReady) test.fail('No live microphone track');
}

export function observeTestCandidate(convid: string, rtc: RTCPeerConnection, candidate: RTCIceCandidate) {
  const test = peers.get(convid);
  if (test && test.rtc === rtc && candidate.candidate) {
    test.candidates.push(normalizeIceCandidate(candidate, String(test.candidates.length)));
  }
}

export function failTestPeer(convid: string, rtc: RTCPeerConnection, message: string) {
  const test = peers.get(convid);
  if (test && test.rtc === rtc) test.fail(message);
}

function audioStats(report: RTCStatsReport): AudioStats {
  const audio: AudioStats = {
    sent: 0, received: 0, lost: null, jitter: null,
    uplinkJitter: null, uplinkLossPercent: null,
    uplinkLost: new Map(),
  };
  report.forEach(stat => {
    if ((stat.kind ?? stat.mediaType) !== 'audio') return;
    if (stat.type === 'outbound-rtp') {
      audio.sent += stat.packetsSent ?? 0;
    } else if (stat.type === 'inbound-rtp') {
      audio.received += stat.packetsReceived ?? 0;
      if (typeof stat.packetsLost === 'number') audio.lost = (audio.lost ?? 0) + stat.packetsLost;
      if (typeof stat.jitter === 'number') audio.jitter = stat.jitter * 1000;
    } else if (stat.type === 'remote-inbound-rtp') {
      if (Number.isFinite(stat.jitter) && stat.jitter >= 0) audio.uplinkJitter = stat.jitter * 1000;
      if (Number.isFinite(stat.fractionLost) && stat.fractionLost >= 0 && stat.fractionLost <= 1)
        audio.uplinkLossPercent = stat.fractionLost * 100;
      if (Number.isFinite(stat.packetsLost)) audio.uplinkLost.set(stat.id, stat.packetsLost);
    }
  });
  return audio;
}

function connectionStats(report: RTCStatsReport) {
  const stats = new Map<string, any>();
  let pairId: string | undefined;
  report.forEach(stat => {
    stats.set(stat.id, stat);
    if (stat.type === 'transport' && stat.selectedCandidatePairId) {
      pairId = stat.selectedCandidatePairId;
    }
  });
  const pair = pairId ? stats.get(pairId) : undefined;
  const local = pair && stats.get(pair.localCandidateId);
  const remote = pair && stats.get(pair.remoteCandidateId);
  const protocol = local?.protocol;
  const route: ConnectionTestResult['wireServer']['route'] =
    local?.candidateType === 'relay' || remote?.candidateType === 'relay' ? 'turn' :
    local?.candidateType && remote?.candidateType ? 'direct' : null;
  const selectedCandidateProtocol: ConnectionTestResult['network']['selectedCandidateProtocol'] =
    protocol === 'udp' || protocol === 'tcp' ? protocol : null;

  return {
    route,
    selectedCandidateProtocol,
    roundTripTimeMs: typeof pair?.currentRoundTripTime === 'number' ? pair.currentRoundTripTime * 1000 : null,
  };
}

export function installConnectionTest(module: any) {
  const sessions = new Map<number, TestSession>();
  module.avsWebTestEvent = (wuser: number, convid: string, event: number, reason: number) => {
    const test = sessions.get(wuser);
    if (!test) return 0;
    if (event === 0) {
      if (peers.has(convid)) return 0;
      test.convid = convid;
      peers.set(convid, test);
      return 1;
    }
    if (test.convid !== convid) return 0;
    if (event === 1) test.mediaEstablished = true;
    if (event === 2) Promise.resolve().then(() => test.fail('AVS closed the test (' + reason + ')', false));
    return 1;
  };

  return (wuser: number, video: number, onProgress?: (progress: ConnectionTestProgress) => void) =>
    new Promise<ConnectionTestResult>((resolve, reject) => {
      if (sessions.has(wuser)) {
        reject(new Error('Connection test already running'));
        return;
      }

      let finished = false;
      let timer: ReturnType<typeof setTimeout> | undefined;
      let deadline: ReturnType<typeof setTimeout> | undefined;
      let startedAt: number | undefined;
      let baseline: AudioStats | undefined;
      const test: TestSession = {candidates: [], microphoneReady: false, mediaEstablished: false, step: 0, fail};
      sessions.set(wuser, test);

      function emit(step: TestStep, status: 'ok' | 'error', error?: string) {
        const candidates = {
          localCandidateFound: test.candidates.length > 0,
          relayCandidateFound: test.candidates.some(candidate => candidate.type === 'relay'),
        };
        Promise.resolve().then(() => onProgress?.({step, status, candidates, ...(error ? {error} : {})}))
          .catch(error => console.warn('Connection test progress callback failed', error));
      }

      function finish() {
        finished = true;
        clearTimeout(timer);
        clearTimeout(deadline);
        sessions.delete(wuser);
        if (test.convid) peers.delete(test.convid);
      }

      function endCall() {
        if (test.convid) module.ccall('wcall_end', null, ['number', 'string'], [wuser, test.convid]);
      }

      function fail(message: string, end = true) {
        if (finished) return;
        const step = steps[test.step];
        finish();
        if (end) {
          try {
            endCall();
          } catch (error) {
            message += '; could not end AVS call: ' + String(error);
          }
        }
        emit(step, 'error', message);
        reject(new Error(step + ': ' + message));
      }

      async function poll() {
        if (finished) return;
        try {
          const rtc = test.rtc;
          if (rtc) {
            if (rtc.connectionState === 'failed' || rtc.connectionState === 'closed' ||
                (test.step === 3 && rtc.connectionState !== 'connected')) {
              fail('WebRTC connection ' + rtc.connectionState);
              return;
            }
            if (test.step === 0 && rtc.iceGatheringState === 'complete') {
              if (!test.candidates.length) {
                fail('ICE gathering produced no local candidates');
                return;
              }
              emit(steps[test.step++], 'ok');
            }
            const connected = rtc.connectionState === 'connected' && rtc.signalingState === 'stable' &&
              rtc.currentLocalDescription && rtc.currentRemoteDescription;
            if (test.step === 1 && connected) emit(steps[test.step++], 'ok');
            if (test.step >= 2 && connected && test.mediaEstablished && test.microphoneReady) {
              const report = await rtc.getStats();
              if (finished) return;
              if (rtc !== test.rtc || rtc.connectionState !== 'connected') {
                timer = setTimeout(poll, POLL_MS);
                return;
              }
              const audio = audioStats(report);
              if (test.step === 2 && (audio.sent > 0 || audio.received > 0)) {
                baseline = audio;
                startedAt = performance.now();
                clearTimeout(deadline);
                deadline = setTimeout(() => fail('Measurement could not finish'), MEASUREMENT_MS + 2000);
                emit(steps[test.step++], 'ok');
              } else if (baseline && startedAt !== undefined && performance.now() - startedAt >= MEASUREMENT_MS) {
                const received = audio.received - baseline.received;
                const sent = audio.sent - baseline.sent;
                const lost = audio.lost === null ? null : Math.max(0, audio.lost - (baseline.lost ?? 0));
                const downlinkLoss = lost !== null && received >= 0 && received + lost > 0
                  ? 100 * lost / (received + lost)
                  : null;
                let uplinkLoss = audio.uplinkLossPercent;
                if (uplinkLoss === null && sent > 0 &&
                    audio.uplinkLost.size === 1 && baseline.uplinkLost.size === 1) {
                  audio.uplinkLost.forEach((current, id) => {
                    const previous = baseline?.uplinkLost.get(id);
                    if (previous !== undefined)
                      uplinkLoss = 100 * Math.min(sent, Math.max(0, current - previous)) / sent;
                  });
                }
                if (sent <= 0 && received <= 0) {
                  fail('No audio packets during measurement');
                  return;
                }
                const {route, selectedCandidateProtocol, roundTripTimeMs} = connectionStats(report);
                const status = route === 'direct' ? 'ok' : route === 'turn' ? 'warning' : 'unknown';
                const result: ConnectionTestResult = {
                  measurementDurationMs: Math.round(performance.now() - startedAt),
                  device: {status: 'ok', microphone: 'ready', localCandidates: test.candidates},
                  network: {
                    status, selectedCandidateProtocol, roundTripTimeMs,
                    jitterMs: (received > 0 ? audio.jitter : null) ?? audio.uplinkJitter,
                    packetLossPercent: downlinkLoss ?? uplinkLoss,
                  },
                  wireServer: {status, target: 'sft', connectionStatus: 'connected', route, failureReason: null},
                };
                endCall();
                finish();
                emit('measurement_finished', 'ok');
                resolve(result);
                return;
              }
            }
          }
          timer = setTimeout(poll, POLL_MS);
        } catch (error) {
          fail(String(error));
        }
      }

      try {
        const err = module.ccall('wcall_web_test_start', 'number', ['number', 'number'], [wuser, video]);
        if (err) {
          fail('Could not start AVS test (' + err + ')', false);
          return;
        }
        deadline = setTimeout(() => fail('Test setup timed out'), SETUP_MS);
        void poll();
      } catch (error) {
        fail(String(error), false);
      }
    });
}
