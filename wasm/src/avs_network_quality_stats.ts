/** JSON.parse(quality_info) from the AVS network-quality callback. */
export interface NetworkQualityStats {
  /** Existing quality code: 1 normal, 2 medium, 3 poor, 4 network problem, 5 reconnecting. */
  quality: number;
  /** Connection RTT in milliseconds; legacy field, zero can mean unavailable. */
  rtt: number;
  loss: Directions;
  jitter: {audio: Directions; video: Directions};
  connection: {
    /** Original local candidate protocol, retained for compatibility. */
    protocol: CandidateProtocol;
    candidate: CandidateType;
    /** TLS when a local relay is reached over TLS; otherwise candidate protocol. */
    local_proto: CandidateProtocol | 'TLS';
    local_cand: CandidateType;
    remote_proto: CandidateProtocol;
    remote_cand: CandidateType;
  };
  peer: 'User' | 'Server' | 'Unknown';
  tx: {audio: AudioMetrics; video: VideoMetrics};
  rx: {audio: ReceivedAudioMetrics[]; video: ReceivedVideoMetrics[]};
}

export type CandidateProtocol = 'UDP' | 'TCP' | 'Unknown' | '???';
export type CandidateType = 'Host' | 'Srflx' | 'Prflx' | 'Relay' | 'Unknown' | '???';

export interface Directions {
  tx: number;
  rx: number;
}

export interface AudioMetrics {
  /** bit/s over the sample interval; -1 means unavailable. */
  bitrate: number;
  /** Milliseconds; -1 means unavailable. */
  jitter: number;
  /** Percent, not fraction: 0.5 means 0.5%; -1 means unavailable. */
  loss: number;
}

export interface VideoMetrics extends AudioMetrics {
  /** Integer pixels; zero means unavailable. */
  width: number;
  height: number;
  /** Frames per second; -1 means unavailable. */
  fps: number;
}

export interface TrackSource {
  /** Browser track identifier, or report ID if unavailable. Scoped to the connection. */
  trackId: string;
  userId: string | null;
  clientId: string | null;
}

export interface ReceivedAudioMetrics extends TrackSource, AudioMetrics {}
export interface ReceivedVideoMetrics extends TrackSource, VideoMetrics {}
