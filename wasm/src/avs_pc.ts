/*eslint-disable sort-keys, no-console */
import {WcallLogHandler} from "./avs_wcall";

declare var RTCRtpSender: any;
declare var RTCRtpScriptTransform: any;

export type UserMediaHandler = (
  convid: string,
  useAudio: boolean,
  useVideo: boolean,
  useScreenShare: boolean
) => Promise<MediaStream>;


export type AudioStreamHandler = (
  convid: string,
  stream_id: string,
  streams: readonly MediaStream[] | null
) => void;

export type VideoStreamHandler = (
  convid: string,
  remote_userid: string,
  remote_clientid: string,
  streams: readonly MediaStream[] | null
) => void;

type RelayCand = {
    hasRelay: boolean
};

type LocalStats = {
    ploss: number;
    lastploss: number;
    bytes: number;
    lastbytes: number;
    recv_apkts: number;
    recv_vpkts: number;
    sent_apkts: number;
    sent_vpkts: number;
    rtt: number;
};

type UserInfo = {
    label: string;
    userid: string;
    clientid: string;
    ssrca: string;
    ssrcv: string;
    iva: Uint8Array;
    ivv: Uint8Array;
    audio_level: number;
    first_recv_audio: boolean;
    first_succ_audio: boolean;
    first_succ_video: boolean;
    transp_ssrcv: string | null;
};

interface PeerConnection {
  self: number;
  convid: string;
  rtc: RTCPeerConnection | null;
  turnServers: any[];
  remote_userid: string;
  remote_clientid: string;
  vstate: number;
  conv_type: number,
  call_type: number;
  sending_video: boolean;
  muted: boolean;
  stats: LocalStats;
  users: any;
  iva: Uint8Array;
  ivv: Uint8Array;
  streams: {[ssrc: string]: string};
  gatherTimer: any;
}

const ENV_FIREFOX = 1;

const TIMEOUT_GATHER = 2000;

let em_module: any;
let logFn: WcallLogHandler | null = null;
let userMediaHandler: UserMediaHandler | null = null;
let audioStreamHandler: AudioStreamHandler | null = null;
let videoStreamHandler: VideoStreamHandler | null = null;
let insertableLegacy: boolean = false;
let insertableStreams: boolean = false;

let pc_env = 0;
let pc_envver = 0;
let worker: Worker;

// Use inlined worker instead....
//let worker = new Worker('/worker/avs-worker.js', {name: 'AVS worker'});

// This is the content of the avs-worker.js file, copied here.

const workerContent = `

var coders = {};

const HDR_VERSION = 0;

/*
Header

                     1 1 1 1 1 1
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| V=0 |E|  RES  |S|LEN  |1|KLEN |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     KID...  (length=KLEN)     |
+-------------------------------+
|      CTR... (length=LEN)      |
+-------------------------------+

Extensions

 0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+---------------------------+
|M| ELN |  EID  |   VAL... (length=ELEN)    |
+-+-+-+-+-+-+-+-+---------------------------+
*/

function calcLen(val) {
    let l = 0;

    if (val == 0)
	return 1;

    while (val != 0) {
	l++;
	val = val >>> 8;	
    }

    return l;
}

function writeBytes(buf, p, len, val)
{
    switch (len) {
    case 8:
	buf[p] = (val >>> 56) & 0xff;
	p++;

    case 7:
	buf[p] = (val >>> 48) & 0xff;
	p++;

    case 6:
	buf[p] = (val >>> 40) & 0xff;
	p++;
	
    case 5:
	buf[p] = (val >>> 32) & 0xff;
	p++;

    case 4:
	buf[p] = (val >>> 24) & 0xff;
	p++;

    case 3:
	buf[p] = (val >>> 16) & 0xff;
	p++;

    case 2:
	buf[p] = (val >>> 8) & 0xff;
	p++;

    case 1:
	buf[p] = val & 0xff;
	break;

    default:
	return 0;
    }
    
    return len;
}

function readBytes(buf, p, len) {
    let l = len;
    let v = 0;

    while (l > 0) {
	v = v * 256;
	v = v + buf[p];
	p++;
	l--;
    }

    const lv = {
	len: len,
	val: v
    };
    
    return lv;
}

function frameHeaderDecode(buf) {

    const MAX_EXTS = 16;
    let ext = buf[0] & 0x10;
    const flen = ((buf[1] >>> 4) & 0x7) + 1;
    const x = (buf[1] >>> 3) & 0x1;
    const klen = buf[1] & 0x7;
    let p = 2;
    let kid = -1;
    let fid = -1;
    let csrc = "0";
    let i = 0;

    if (x) {
	const lv = readBytes(buf, p, klen + 1);

	p += lv.len;
	kid = lv.val;
    }
    else {
	kid = klen;
    }

    const lv = readBytes(buf, p, flen);
    p += lv.len;
    fid = lv.val;

    while(ext && p < buf.length && i < MAX_EXTS) {
	const b = buf[p];
	ext = b & 0x80;
	const elen = ((b >>> 4) & 0x7) + 1;
	const eid = (b & 0xf);

        if (eid == 1 && elen == 4) {
            const csrco = readBytes(buf, p + 1, elen);
            csrc = csrco.val.toString();
        }
        p += elen + 1;
        i++;
    }

    if (kid < 0 || fid < 0) {
	return null;
    }
    else {
	const frameHdr = {
	    len: p,
	    frameId: fid,
	    keyId: kid,
	    csrc: csrc
	}
	return frameHdr;
    }
}

function frameHeaderEncode(fid, kid, csrc)
{
    const ab = new ArrayBuffer(32);
    const buf = new Uint8Array(ab);
    let sig = 0;
    let x = 0;
    let klen = 0;
    let flen = 0;
    let p = 2;
    let ext = (csrc > 0) ? 1 : 0;

    if (kid > 7) {
	x = 1;
	klen = calcLen(kid) - 1;
    }
    else {
	klen = kid;
    }

    flen = calcLen(fid);

    buf[0] = (HDR_VERSION << 5) + (ext << 4);
    buf[1] = (sig << 7) + ((flen - 1) << 4) + (x << 3) + klen;
    if (x) {
	p += writeBytes(buf, p, klen + 1, kid);
    }
    p += writeBytes(buf, p, flen, fid);

    if (csrc > 0) {
        buf[p] = 0x31;
        p++;
        p+= writeBytes(buf, p, 4, csrc);
    }
    return buf.subarray(0, p);
}

function xor_iv(iv, fid, kid) {
  const oiv = new Uint8Array(iv);

  oiv[0] = oiv[0] ^ ((fid >> 24) & 0xFF);
  oiv[1] = oiv[1] ^ ((fid >> 16) & 0xFF);
  oiv[2] = oiv[2] ^ ((fid >>  8) & 0xFF);
  oiv[3] = oiv[3] ^ ((fid      ) & 0xFF);
  oiv[4] = oiv[4] ^ ((kid >> 24) & 0xFF);
  oiv[5] = oiv[5] ^ ((kid >> 16) & 0xFF);
  oiv[6] = oiv[6] ^ ((kid >>  8) & 0xFF);
  oiv[7] = oiv[7] ^ ((kid      ) & 0xFF);

  return oiv;
}

function getMediaKey(self, index) {
    postMessage({
	op: "getMediaKey",
	self: self,
	index: index
    });
}

function getCurrentMediaKey(self, index) {
    postMessage({
	op: "getCurrentMediaKey",
	self: self
    });
}

function doLog(str) {
  postMessage({
	op: "log",
	level: 1,
	logString: str
  });
}

function encryptFrame(coder, rtcFrame, controller) {
  const dataBuf = rtcFrame.data;
  const data = new Uint8Array(dataBuf);
  const t = Object.prototype.toString.call(rtcFrame);
  const isVideo = t === '[object RTCEncodedVideoFrame]';
  const userid = "self";

  const meta = rtcFrame.getMetadata();
  let ssrc = meta.synchronizationSource;

  if (isVideo)
    ssrc = coder.video.ssrc;

  if (isVideo && !coder.video.first_recv) {
    doLog("frame_enc: encrypt: first frame received type: video uid: " +
          userid.substring(0,8) + " fid: " + coder.frameId + " ssrc: " + ssrc);
    coder.video.first_recv = true;
    coder.video.first_succ = false;
  }
  if (!isVideo && !coder.audio.first_recv) {
    doLog("frame_enc: encrypt: first frame received type: audio uid: " +
          userid.substring(0,8) + " fid: " + coder.frameId + " ssrc: " + ssrc);
    coder.audio.first_recv = true;
    coder.audio.first_succ = false;
  }

  if (coder.currentKey == null) {
    getCurrentMediaKey(coder.self);
    return;
  }

  const baseiv = isVideo ? coder.video.iv : coder.audio.iv;
  const iv = xor_iv(baseiv, coder.frameId, coder.currentKey.id);

  const hdr = frameHeaderEncode(coder.frameId, coder.currentKey.id, ssrc);
  crypto.subtle.encrypt(
    {
      name: "AES-GCM",
      iv: iv,
      tagLength: 128,
      additionalData: hdr
    },
    coder.currentKey.key,
    data
  )
  .then(function(encdata){
    const enc8 = new Uint8Array(encdata);  	    
    const enc = new Uint8Array(hdr.length + enc8.length);
      
    enc.set(hdr);
    enc.set(enc8, hdr.length);

    rtcFrame.data = enc.buffer;
    controller.enqueue(rtcFrame);

    if (isVideo && !coder.video.first_succ) {
      doLog("frame_enc: encrypt: first frame encrypted type: video uid: " +
            userid.substring(0,8) + " fid: " + coder.frameId + " csrc: " + ssrc);
      coder.video.first_succ = true;
    }
    if (!isVideo && !coder.audio.first_succ) {
      doLog("frame_enc: encrypt: first frame encrypted type: audio uid: " +
            userid.substring(0,8) + " fid: " + coder.frameId + " csrc: " + ssrc);
      coder.audio.first_succ = true;
    }

    coder.frameId++;
  });
}

function decryptFrame(track_id, coder, rtcFrame, controller) {
  const dataBuf = rtcFrame.data;
  const data = new Uint8Array(dataBuf);

  const t = Object.prototype.toString.call(rtcFrame);
  const isVideo = t === '[object RTCEncodedVideoFrame]'

  const frameHdr = frameHeaderDecode(data);

  if (frameHdr == null) {
    doLog('decryptFrame: failed to decode frame header');
    return;
  }

  const meta = rtcFrame.getMetadata();
  let ssrc = meta.synchronizationSource.toString();
  let csrc = null;
  if (frameHdr.csrc != "0") {
     csrc = frameHdr.csrc;
  }
  else if (meta.contributingSources && meta.contributingSources.length > 0) {
     csrc = meta.contributingSources[0].toString();
  }
  else {
     csrc = ssrc;
  }

  if (!csrc) {
      doLog('decryptFrame: no csrc for ssrc ' + ssrc);
      return;
  }

  let uinfo = null;
  if (isVideo) {
     uinfo = coder.video.users[csrc];
  }
  else {
     uinfo = coder.audio.users[csrc];
  }

  if (!uinfo) {
      doLog('decryptFrame: no userinfo for csrc: ' + csrc);
      return;
  }
 
  //console.log('frame_dec: video ' + isVideo + ' ssrc ' + ssrc + ' csrc ' + csrc + ' user ' + uinfo.userid.substring(0,8));

  if (isVideo && uinfo.transp_ssrcv != ssrc) {
      doLog("frame_dec: decrypt: first frame received type: video uid: " +
            uinfo.userid.substring(0,8) + " fid: " + frameHdr.frameId + " csrc: " + csrc + " ssrc: " + ssrc + " uinfo.ssrcv: " + uinfo.ssrcv);
      uinfo.first_succ_video = false;

      /* Only call videoStreamHandler for selective video */
      if (uinfo.ssrcv != ssrc) {
          for (const [key, u] of Object.entries(coder.video.users)) {
              if (u.transp_ssrcv == ssrc) {
                  postMessage({
                      op: "setvstream",
                      self: coder.self,
                      userid: u.userid,
                      clientid: u.clientid,
                      ssrc: "0",
		      track_id: "0",
                  });
                  u.transp_ssrcv = null;
              }
          }

          postMessage({
              op: "setvstream",
              self: coder.self,
              userid: uinfo.userid,
              clientid: uinfo.clientid,
              ssrc: ssrc,
	      track_id: track_id,
          });
      }
      uinfo.transp_ssrcv = ssrc;
  }

  if (!isVideo && !uinfo.first_recv_audio) {
    doLog("frame_dec: decrypt: first frame received type: audio uid: " +
          uinfo.userid.substring(0,8) + " fid: " + frameHdr.frameId + " csrc: " + csrc);
    uinfo.first_recv_audio = true;
    uinfo.first_succ_audio = false;
  }
  /*
  console.log("coder[" + coder.self + "]"
	+ " len=" + frameHdr.len
	+ " kid="+ frameHdr.keyId
	+ " fid=" + frameHdr.frameId);
  */

  let kinfo = null;
  for (i = 0; i < coder.keys.length; i++) {
    if (coder.keys[i].id == frameHdr.keyId) {
      kinfo = coder.keys[i];
      break;
    }
  }

  if (!kinfo) {
    getMediaKey(coder.self, frameHdr.keyId);
    return;
  }

  let iv = null;
  if (isVideo) {
    iv = xor_iv(uinfo.ivv, frameHdr.frameId, frameHdr.keyId);
  }
  else {
    iv = xor_iv(uinfo.iva, frameHdr.frameId, frameHdr.keyId);
  }

  const hdr = data.subarray(0, frameHdr.len);
  const dec = data.subarray(frameHdr.len);
  crypto.subtle.decrypt(
    {
      name: "AES-GCM",
      iv: iv,
      tagLength: 128,
      additionalData: hdr
    },
    kinfo.key,
    dec
  )
  .then(function(decdata){
    rtcFrame.data = decdata;
    controller.enqueue(rtcFrame);
    if (isVideo && !uinfo.first_succ_video) {
      doLog("frame_dec: decrypt: first frame decrypted type: video uid: " +
            uinfo.userid.substring(0,8) + " fid: " + frameHdr.frameId + " csrc: " + csrc);
      uinfo.first_succ_video = true;
    }
    if (!isVideo && !uinfo.first_succ_audio) {
      doLog("frame_dec: decrypt: first frame decrypted type: audio uid: " +
            uinfo.userid.substring(0,8) + " fid: " + frameHdr.frameId + " csrc: " + csrc);
      uinfo.first_succ_audio = true;
    }
  })
  .catch(err => {
     console.log(err);
  });
}

function setupSender(readableStream, writableStream, coder) {
	const transformStream = new TransformStream({
	    transform: (frame, controller) => {encryptFrame(coder, frame, controller);},
	});
	readableStream
          .pipeThrough(transformStream)
          .pipeTo(writableStream);
}

function setupReceiver(readableStream, writableStream, coder, track_id) {
       const transformStream = new TransformStream({
	   transform: (frame, controller) => {decryptFrame(track_id, coder, frame, controller);},
       });
       readableStream
         .pipeThrough(transformStream)
         .pipeTo(writableStream); 
}


onrtctransform = (event) => {
    let transform;
    const writableStream = event.transformer.writable;
    const readableStream = event.transformer.readable;
    const coder = coders[event.transformer.options.self];

    if (!writableStream) {
       pc_log(LOG_LEVEL_WARN, "onrtctransform: no writable stream");
       return;
    }
    if (event.transformer.options.name === "senderTransform")
      setupSender(readableStream, writableStream, coder);
    else if (event.transformer.options.name === 'receiverTransform') {
      const track_id = event.transformer.options.track_id;
      doLog("onrtctransform: receiver trackid=" + track_id);
      setupReceiver(readableStream, writableStream, coder, track_id);
    }
};


onmessage = async (event) => {
    const {op, self} = event.data;
    doLog('AVS worker op=' + op + ' self=' + self);

    let coder = coders[self];
    if (!coder) {
	doLog('AVS worker: no coder for self=' + self);
    }    
    /*
    const {opinfo, readableStream, writableStream} = event.data;
    const mval = opinfo.mtype === 'video' ? 1 : 0; // corresponds to enum frame_media_type
    */
    if (op === 'create') {
	const { iva, ivv } = event.data;
	if (!coder) {
	    coder = {
		self: self,
		frameId: Math.floor(Math.random() * 4294967295),
		currentKey: null,
		keys: [],
		audio: {
		    first_recv: false,
		    first_succ: false,
		    iv: iva,
		    ssrc: "0",
		    users: {},
		},
		video: {
		    first_recv: false,
		    first_succ: false,
		    iv: ivv,
		    ssrc: "0",
		    users: {},
		},
	    }
	    coders[self] = coder;

	    doLog('AVS worker: adding coder for self=' + self);
	}
    }
    else if (op === 'destroy') {
	coders[self] = null;
    }
    else if (op === 'updateSsrc') {
	const { ssrca, ssrcv } = event.data;
	coder.audio.ssrc = ssrca;
	coder.video.ssrc = ssrcv;
    }
    else if (op === 'addUser') {
	if (!coder) {
	    doLog('addUser: no coder');
	    return;
	}
	const {userInfo} = event.data;

	doLog('addUser: adding audio ssrc: '+userInfo.ssrca);
	coder.audio.users[userInfo.ssrca] = userInfo;
	if (userInfo.ssrcv != 0) {
	    coder.video.users[userInfo.ssrcv] = userInfo;
	}
    }
    else if (op === 'setupSender') {
	const {readableStream, writableStream} = event.data;

	setupSender(readableStream, writableStream, coder);
    }
    else if (op === 'setupReceiver') {
       const {readableStream, writableStream} = event.data;

       if (!coder) {
	   doLog('setupSender: no coder for self=' + self);
           return;
       }

       setupReceiver(readableStream, writableStream, coder, null);
   }
   else if (op === 'setMediaKey') {
       const {index, current, key} = event.data;

       doLog("setMediaKey: got key for: " + self
		   + " index=" + index + " current=" + current
		   + " keyLen=" + key.length);

       crypto.subtle.importKey(
         "raw",
         key,
         "AES-GCM",
         false,
         ["encrypt", "decrypt"]
       )
       .then(function(k){
         let kinfo = null;
         for (i = 0; i < coder.keys.length; i++) {
           if (coder.keys[i].id == index) {
             coder.keys[i].key = k;
             kinfo = coder.keys[i];
             break;
           }
         }
         if (!kinfo) {
           kinfo = {id:index, key:k};
           coder.keys.push(kinfo);
         }
         if (index == current)
           coder.currentKey = kinfo;

         while (coder.keys.length > 4) {
           coder.keys.shift();
         }
       });
   }    
};
`

function callStreamHandler(pc: PeerConnection,
                           userid: string,
                           clientid: string,
                           ssrc: string,
			   track_id: string) {

  pc_log(LOG_LEVEL_INFO, `vsh: ${videoStreamHandler} rtc: ${pc.rtc} ssrc: ${ssrc}`);
  if (ssrc == "0") {
    if (videoStreamHandler) {
      pc_log(LOG_LEVEL_INFO, `calling vsh(${pc.convid.substring(0,8)}, ${userid.substring(0,8)}, ${clientid.substring(0,4)}) to remove renderer`);
      videoStreamHandler(pc.convid,
                         userid,
                         clientid,
                         null);
    }
  }
  else if (pc.rtc) {
    if (pc_env === ENV_FIREFOX) {
      pc.rtc.getTransceivers().forEach(trans => {
        const track = trans.receiver.track;

	if (track.kind != 'video')
	  return;
	  
        pc_log(LOG_LEVEL_INFO, `vsh: id:${track.id} looking for: ${track_id}`);
        if (track.id === track_id) {
          let stream = new MediaStream([trans.receiver.track]);
          if (videoStreamHandler) {
            pc_log(LOG_LEVEL_INFO, `calling vsh(${pc.convid.substring(0,8)}, ${userid.substring(0,8)}, ${clientid.substring(0,4)}) with 1 stream`);
            videoStreamHandler(pc.convid,
                               userid,
                               clientid,
                               [stream]);
          }
        }
      });
    }
    else {
      const label = pc.streams[ssrc];
      pc.rtc.getTransceivers().forEach(trans => {
        const track = trans.receiver.track;
        pc_log(LOG_LEVEL_INFO, `vsh: label:${track.label} id:${track.id} looking for: ${label}`);
        if (trans.receiver.track.label === label) {
          let stream = new MediaStream([trans.receiver.track]);
          if (videoStreamHandler) {
            pc_log(LOG_LEVEL_INFO, `calling vsh(${pc.convid.substring(0,8)}, ${userid.substring(0,8)}, ${clientid.substring(0,4)}) with 1 stream`);
            videoStreamHandler(pc.convid,
                               userid,
                               clientid,
                               [stream]);
          }
        }
      });
    }
  }
}

function createWorker() {
  const blob = new Blob([workerContent], { type: 'text/javascript' });
  const url = URL.createObjectURL(blob);

  worker = new Worker(url , {name: 'AVS worker'});
  worker.onmessage = (event) => {
    const {op, self} = event.data;
  
    if (op === 'getMediaKey') {
      const {index} = event.data;

      ccallGetMediaKey(self, index);
    }
    else if (op === 'getCurrentMediaKey') {
      ccallGetCurrentMediaKey(self);
    }
    else if (op === 'log') {
      const {level, logString} = event.data;
      pc_log(level, logString);
    }
    else if (op === 'setvstream') {
      const {userid, clientid, ssrc, track_id} = event.data;
      pc_log(LOG_LEVEL_INFO, "setvstream: called");
      let pcs = connectionsStore.getPeerConnectionBySelf(self);

      if (pcs.length == 1) {
        pc_log(LOG_LEVEL_INFO, `setvstream: calling videoStreamHandler: pc=${pcs[0]}, ${userid}/${clientid} ssrc=${ssrc}`);
        callStreamHandler(pcs[0], userid, clientid, ssrc, track_id);
      }
    }
  }
  
}

/* The following constants closely reflect the values
 * defined in the the C-land counterpart peerconnection_js.c
 */

const PC_SIG_STATE_UNKNOWN         = 0;
const PC_SIG_STATE_STABLE          = 1;
const PC_SIG_STATE_LOCAL_OFFER     = 2;
const PC_SIG_STATE_LOCAL_PRANSWER  = 3;
const PC_SIG_STATE_REMOTE_OFFER    = 4;
const PC_SIG_STATE_REMOTE_PRANSWER = 5;
const PC_SIG_STATE_CLOSED          = 6;

const PC_GATHER_STATE_UNKNOWN      = 0;
const PC_GATHER_STATE_NEW          = 1;
const PC_GATHER_STATE_GATHERING    = 2;
const PC_GATHER_STATE_COMPLETE     = 3;

const PC_VIDEO_STATE_STOPPED       = 0;
const PC_VIDEO_STATE_STARTED       = 1;
const PC_VIDEO_STATE_BAD_CONN      = 2;
const PC_VIDEO_STATE_PAUSED        = 3;
const PC_VIDEO_STATE_SCREENSHARE   = 4;

const DC_STATE_CONNECTING          = 0;
const DC_STATE_OPEN                = 1;
const DC_STATE_CLOSING             = 2;
const DC_STATE_CLOSED              = 3;
const DC_STATE_ERROR               = 4;

const LOG_LEVEL_DEBUG              = 0;
const LOG_LEVEL_INFO               = 1;
const LOG_LEVEL_WARN               = 2;
const LOG_LEVEL_ERROR              = 3;

const CALL_TYPE_NORMAL             = 0;
const CALL_TYPE_VIDEO              = 1;
const CALL_TYPE_FORCED_AUDIO       = 2;

const CONV_TYPE_ONEONONE           = 0;
const CONV_TYPE_GROUP              = 1;
const CONV_TYPE_CONFERENCE         = 2;

const connectionsStore = (() => {
  const peerConnections: (PeerConnection | null)[] = [null];
  const dataChannels: (RTCDataChannel | null)[] = [null];

  const storeItem = <T>(store: T[], item: T) => {
    let index = store.indexOf(item);
    if (index === -1) index = store.push(item) - 1;

    return index;
  };

  const removeItem = <T>(store: (T|null)[], index: number) => {
    store[index] = null;
  };

  const getItem = <T>(store: T[], index: number) => {
    return store[index];
  };

  const existsItem = <T>(store: T[], item: T) => {
    return store.indexOf(item) != -1;
  };

  return {
    storePeerConnection: (pc: PeerConnection) => storeItem(peerConnections, pc),
    getPeerConnection: (index: number) => getItem(peerConnections, index),
    getPeerConnectionByConvid: (convid: string): PeerConnection[] => {
      return peerConnections.filter(pc => {
        return !!pc && pc.convid === convid;
      }) as PeerConnection[];
    },
    getPeerConnectionBySelf: (self: number): PeerConnection[] => {
      return peerConnections.filter(pc => {
        return !!pc && pc.self == self;
      }) as PeerConnection[];
    },
    removePeerConnection: (index: number) => removeItem(peerConnections, index),
    storeDataChannel: (dataChannel: RTCDataChannel) =>
      storeItem(dataChannels, dataChannel),
    getDataChannel: (index: number) => getItem(dataChannels, index),
    removeDataChannel: (index: number) => removeItem(dataChannels, index)
  };
})();

function pc_log(level: number, msg: string, err: any = null) {

    em_module.ccall(
      "pc_log",
      null,
      ["number", "string"],
      [level, msg]);

    /* Log now goes through AVS for anonymization
    if (logFn)
	logFn(level, msg, err);
    */
}

function uinfo_from_ssrca(pc: PeerConnection, ssrc: string) : UserInfo | null
{
  for (const key of Object.keys(pc.users)) {
    const uinfo : UserInfo = pc.users[key];
    if (uinfo.ssrca === ssrc) {
       return uinfo;
    }
  }
  return null;
}

function replace_track(pc: PeerConnection, newTrack: MediaStreamTrack) {
    const rtc = pc.rtc;

    if (!rtc)
	return false;

    const senders = rtc.getSenders();
    if (!senders)
	return false;
    
    for (const sender of senders) {
	if (!sender)
	    continue;

	if (!sender.track)
            continue;

	if (sender.track.kind === newTrack.kind) {
	    const oldTrack = sender.track;

	    if (!oldTrack) {
		pc_log(LOG_LEVEL_INFO, 'replace_track: oldtrack null');
	    }
	    else {			
		const enabled = oldTrack.enabled;

        // newTrack.enabled = enabled;
        sender.replaceTrack(newTrack).then(() => {
            if(!!sender && !!sender.track) {
                sender.track.enabled = enabled
		pc_log(LOG_LEVEL_INFO, `replace_track: kind=${newTrack.kind} enabled=${sender.track.enabled}`);
            }
        });

		return true;
	    }
	}
    }

    return false;
}
function update_tracks(pc: PeerConnection, stream: MediaStream): Promise<void> {

    const rtc = pc.rtc;
    const tracks = stream.getTracks();

    let found = false;

    pc_log(LOG_LEVEL_INFO, `update_tracks: pc=${pc.self} tracks=${tracks.length}`);
    if (!rtc) {
        return Promise.resolve();
    }
    const senders = rtc.getSenders();
    for (const sender of senders) {
        found = false;
        if (!sender)
            continue;

        if (!sender.track) {
            rtc.removeTrack(sender);
            continue;
        }

        for (const track of tracks) {
            if (track)
                pc_log(LOG_LEVEL_INFO, `update_tracks: kind=${track.kind} sender=${sender.track.kind}`);
            else
                pc_log(LOG_LEVEL_INFO, `update_tracks: sender.track=${sender.track} track=${track}`);

            if (sender.track) {
                if (sender.track.kind === track.kind) {
                    found = true;
                    sender.track.enabled = true;
                    break;
                }
            }
        }
        if (!found) {
            if (sender.track) {
                sender.track.enabled = false;
            }

        }
    }

    const videoSenderUpdates: Promise<void>[] = []
    tracks.forEach(track => {
        if (track.kind === 'video') {
            pc.sending_video = true;
        } else {
            track.enabled = !pc.muted;
        }
        if (!replace_track(pc, track)) {
            pc_log(LOG_LEVEL_INFO, `update_tracks: adding track of kind=${track.kind}, id=${track.id}`);
            if (track.kind === 'video' && pc.conv_type !== CONV_TYPE_ONEONONE) {
                const transceivers = rtc.getTransceivers();
                for (const trans of transceivers) {
                    if (trans.mid === 'video' && !!trans.sender) {
                        pc_log(LOG_LEVEL_INFO, `update_tracks: adjust`)
                        const {params, layerFound} = getEncodingParameter(trans.sender, pc.vstate === PC_VIDEO_STATE_SCREENSHARE)

                        pc_log(LOG_LEVEL_INFO, 'add track: ' + layerFound);
                        rtc.addTrack(track, stream)
                        if (layerFound) {
                            videoSenderUpdates.push(trans.sender.setParameters(params).catch((e) => pc_log(LOG_LEVEL_ERROR, `update_tracks: set params ${e}`))
                                .then(() => {
                                    pc_log(LOG_LEVEL_INFO, 'setParameters: ' + JSON.stringify(params));
                                })
                                .then());
                        }
                    }
                }
            } else {
                rtc.addTrack(track, stream);
            }
        }
    });
    // Either the codecs did not need to be adjusted because we are in a 1:1 call, or we are not sending a video track.
    // In that case, there is no need to wait for the asynchronous process
    if (videoSenderUpdates.length === 0) {
        return Promise.resolve();
    }
    // Wait until setup is finish
    return Promise.all(videoSenderUpdates).catch(e => {
        pc_log(LOG_LEVEL_ERROR, `update_tracks: error=${e}`)
    }).then();
}

function getEncodingParameter(sender: RTCRtpSender, isScreenShare: boolean) {
    pc_log(LOG_LEVEL_INFO, `update_tracks: adjust`)
    const params = sender.getParameters()

    if (!params.encodings) {
        pc_log(LOG_LEVEL_INFO, `update_tracks: no params`)
        params.encodings = [];
    }

    let layerFound = false;
    params.encodings.forEach((coding) => {
        if(coding.rid === 'l') {
            //@ts-ignore
            coding.scalabilityMode = 'L1T1'
            coding.active = !isScreenShare
            coding.scaleResolutionDownBy = 2.0;
            coding.maxBitrate = 500000;
            layerFound = true;
        }
        if(coding.rid === 'h') {
            //@ts-ignore
            coding.scalabilityMode = 'L1T1'

            coding.scaleResolutionDownBy = 1.0;
            coding.active = true;
            coding.maxBitrate = isScreenShare? 1500000: 3000000;
            layerFound = true;
        }
    });

    return {params, layerFound}
}

function sigState(stateStr: string) {
  let state = PC_SIG_STATE_UNKNOWN;

  switch (stateStr) {
    case "stable":
      state = PC_SIG_STATE_STABLE;
      break;

    case "have-local-offer":
      state = PC_SIG_STATE_LOCAL_OFFER;
      break;

    case "have-remote-offer":
      state = PC_SIG_STATE_REMOTE_OFFER;
      break;

    case "have-local-pranswer":
      state = PC_SIG_STATE_LOCAL_PRANSWER;
      break;

    case "have-remote-pranswer":
      state = PC_SIG_STATE_REMOTE_PRANSWER;
      break;

    case "closed":
      state = PC_SIG_STATE_CLOSED;
      break;
  }

  return state;
}

function ccallLocalSdpHandler(
  pc: PeerConnection,
  err: number,
  type: string,
  sdp: string
) {
  em_module.ccall(
    "pc_local_sdp_handler",
    null,
    ["number", "number", "string", "string", "string"],
    [pc.self, err, "avs", type, sdp]
  );
}

function ccallStartGatherHandler(
  pc: PeerConnection
) {
  em_module.ccall(
    "pc_start_gather_handler",
    null,
    ["number"],
    [pc.self]
  );
}


function ccallSignallingHandler(pc: PeerConnection, state: number) {
  em_module.ccall(
    "pc_signalling_handler",
    null,
    ["number", "number"],
    [pc.self, state]
  );
}

function ccallGatheringHandler(pc: PeerConnection, type: string, sdp: string) {
  em_module.ccall(
    "pc_gather_handler",
    null,
    ["number", "string", "string"],
    [pc.self, type, sdp]
  );
}

function ccallConnectionHandler(pc: PeerConnection, state: string) {
  em_module.ccall(
    "pc_connection_handler",
    null,
    ["number", "string"],
    [pc.self, state]
  );
}

/* Data-channel helpers */

function ccallDcEstabHandler(pc: PeerConnection, dc: number) {
  em_module.ccall(
    "dc_estab_handler",
    null,
    ["number", "number"],
    [pc.self, dc]
  );
}

function ccallDcStateChangeHandler(pc: PeerConnection, state: number) {
  em_module.ccall(
    "dc_state_handler",
    null,
    ["number", "number"],
    [pc.self, state]
  );
}

function ccallDcDataHandler(pc: PeerConnection, data: string) {
  em_module.ccall(
    "dc_data_handler",
    null,
    ["number", "string", "number"],
    [pc.self, data, data.length]
  );
}


function ccallGetMediaKey(
  self: number,
  index: number)
{
  em_module.ccall(
    "pc_get_media_key",
    null,
    ["number", "number"],
    [self, index]
  );
}


function ccallGetCurrentMediaKey(
  self: number)
{
  em_module.ccall(
    "pc_get_current_media_key",
    null,
    ["number"],
    [self]
  );
}

function gatheringComplete(pc: PeerConnection) {
  if (pc.gatherTimer != null) {
     clearTimeout(pc.gatherTimer);
     pc.gatherTimer = null;
  }
  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }
  const sdp = rtc.localDescription;
  if (!sdp) {
    return;
  }
  ccallGatheringHandler(pc, sdp.type.toString(), sdp.sdp.toString());
}

function gatheringHandler(pc: PeerConnection) {
  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }
  const state = rtc.iceGatheringState;

  pc_log(LOG_LEVEL_INFO, `ice gathering state=${state}`);

  switch (state) {
    case "new":
      break;

    case "gathering":
      break;

    case "complete":
      gatheringComplete(pc);
      break;
  }
}

function negotiationHandler(pc: PeerConnection) {
  pc_log(LOG_LEVEL_INFO, `negotiationHandler: ${pc.self}`);    
}


function signallingHandler(pc: PeerConnection) {
  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }
  const stateStr = rtc.signalingState;

  pc_log(LOG_LEVEL_INFO, `signalingHandler: state: ${stateStr}`);

  ccallSignallingHandler(pc, sigState(stateStr));
}

function setMute(pc: PeerConnection) {
  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }

  const senders = rtc.getSenders();
  for (const sender of senders) {
    const track = sender.track;
    if (track && track.kind === "audio") {
      track.enabled = !pc.muted;
    }
  }
}

function candidateHandler(pc: PeerConnection, cand: RTCIceCandidate | null) {    
    const mindex = cand ? cand.sdpMLineIndex : null;

    if (cand !== null)
	pc_log(LOG_LEVEL_INFO, `candidateHandler: cand=${cand.candidate} type=${cand.type} mindex=${mindex}`);
    else {
	pc_log(LOG_LEVEL_INFO, `candidateHandler: cand=NULL`);
    }

    if (!pc || !pc.rtc)
	return;
    
    if (!cand) {
	pc_log(LOG_LEVEL_INFO, 'candidateHandler: end-of-candidates');	
	gatheringComplete(pc);

        return;
    }

    /* As soon as we get the first relay, we start a gathering timer
     * this was we ensure that gathering never takes more than the
     * timeout period.
     */
    if (cand.type === 'relay') {
        if (pc.gatherTimer == null) {
	    pc.gatherTimer = setTimeout(() => {
	      pc.gatherTimer = null;
	      gatheringComplete(pc);
	    }, TIMEOUT_GATHER);
	}
    }
}

function connectionHandler(pc: PeerConnection) {
  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }
  const state = rtc.iceConnectionState;

  pc_log(LOG_LEVEL_INFO, `connectionHandler state: ${state}`);

  ccallConnectionHandler(pc, state);

  setMute(pc);
}

function setupDataChannel(pc: PeerConnection, dc: RTCDataChannel) {
  const dcHnd = connectionsStore.storeDataChannel(dc);
  dc.onopen = () => {
    pc_log(LOG_LEVEL_INFO, "dc-opened");
    ccallDcStateChangeHandler(pc, DC_STATE_OPEN);
  };
  dc.onclose = () => {
    pc_log(LOG_LEVEL_INFO, "dc-closed");
    ccallDcStateChangeHandler(pc, DC_STATE_CLOSED);
  };
  dc.onerror = event => {
    if (event instanceof RTCErrorEvent) {
      pc_log(LOG_LEVEL_INFO, `dc-error: ${event.error}`);
      ccallDcStateChangeHandler(pc, DC_STATE_ERROR);
    }
  };
  dc.onmessage = event => {
    pc_log(LOG_LEVEL_INFO, `dc-onmessage: data=${event.data.length}`);
        ccallDcDataHandler(pc, event.data.toString());
  };

  return dcHnd;
}

function dataChannelHandler(pc: PeerConnection, event: RTCDataChannelEvent) {
  const dc = event.channel;
  pc_log(LOG_LEVEL_INFO, `dataChannelHandler: ${dc}`);

  const dcHnd = setupDataChannel(pc, dc);

  ccallDcEstabHandler(pc, dcHnd);
}

function pc_SetEnv(env: number) {

    pc_log(LOG_LEVEL_INFO, `setEnv=${env}`);
    pc_env = env;
}

function pc_New(self: number, convidPtr: number,
	        audioIvPtr: number, videoIvPtr: number, ivlen: number) {
  pc_log(LOG_LEVEL_INFO, "pc_New");

  const iva = new ArrayBuffer(ivlen);
  const aptr = new Uint8Array(em_module.HEAPU8.buffer, audioIvPtr, ivlen);
  const iva8 = new Uint8Array(iva); 
  iva8.set(aptr);

  const ivv = new ArrayBuffer(ivlen);
  const vptr = new Uint8Array(em_module.HEAPU8.buffer, videoIvPtr, ivlen);
  const ivv8 = new Uint8Array(ivv); 
  ivv8.set(vptr);

  const pc: PeerConnection = {
    self: self,
    convid: em_module.UTF8ToString(convidPtr),
    rtc: null,
    turnServers: [],
    remote_userid: "",
    remote_clientid: "",
    vstate: PC_VIDEO_STATE_STOPPED,
    sending_video: false,
    call_type: CALL_TYPE_NORMAL,
    conv_type: CONV_TYPE_ONEONONE,
    muted: false,
    users: {},
    iva: iva8,
    ivv: ivv8,
    stats: {
      ploss: 0,
      lastploss: 0,
      bytes: 0,
      lastbytes: 0,
      recv_apkts: 0,
      recv_vpkts: 0,
      sent_apkts: 0,
      sent_vpkts: 0,
      rtt: 0
    },
    streams: {},
    gatherTimer: null
  };

  worker.postMessage({op: 'create', self: pc.self, iva: iva8, ivv: ivv8});

  const hnd = connectionsStore.storePeerConnection(pc);

  return hnd;
}

function pc_Create(hnd: number, privacy: number, conv_type: number) {
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  pc.conv_type = conv_type;
  
  const transportPolicy = privacy !== 0 ? "relay" : "all";

  const useEncoding = pc.conv_type === CONV_TYPE_CONFERENCE;

  const config : any = {
    bundlePolicy: "max-bundle",
    iceServers: pc.turnServers,
    rtcpMuxPolicy: 'require',
    iceTransportPolicy: transportPolicy,
    encodedInsertableStreams: useEncoding,
    forceEncodedVideoInsertableStreams: useEncoding,
    forceEncodedAudioInsertableStreams: useEncoding,
  };

  pc_log(
    LOG_LEVEL_INFO,
    `pc_Create: configuring: ${pc.turnServers.length} TURN servers`
  );

  const rtc = new RTCPeerConnection(config);

  pc.rtc = rtc;
  rtc.onicegatheringstatechange = () => gatheringHandler(pc);
  rtc.oniceconnectionstatechange = () => connectionHandler(pc);
  rtc.onicecandidate = (event) => candidateHandler(pc, event.candidate);  
  rtc.onsignalingstatechange = event => signallingHandler(pc);
  rtc.ondatachannel = event => dataChannelHandler(pc, event);
  rtc.onnegotiationneeded = () => negotiationHandler(pc);  

  let label: string = '';
  rtc.ontrack = event => {
      pc_log(LOG_LEVEL_INFO, `onTrack: self=${pc.self} convid=${pc.convid.substring(0,8)} userid=${pc.remote_userid.substring(0,8)}/${pc.remote_clientid.substring(0,4)} streams=${event.streams.length}`);

      if (event.streams && event.streams.length > 0) {
	  for (const stream of event.streams) {
	      pc_log(LOG_LEVEL_INFO, `onTrack: convid=${pc.convid.substring(0,8)} stream=${stream}`);
	      for (const track of stream.getTracks()) {
		  if (track) {
		      if (pc_env === ENV_FIREFOX)
		        label = track.id;
	 	      else 
		        label = track.label;
		      pc_log(LOG_LEVEL_INFO, `onTrack: convid=${pc.convid.substring(0,8)} track=${track.id}/${track.label}=>${label} kind=${track.kind} enabled=${track.enabled}/${track.muted}/undefined/${track.readyState} remote=undefined`);
		      if (!track.enabled)
		       	track.enabled = true;

			if (pc.conv_type === CONV_TYPE_CONFERENCE) {
			  try {
			    setupReceiverTransform(pc, event.receiver);
			  }
			  catch(err) {
			    pc_log(LOG_LEVEL_WARN, "onTrack: setupReceiverTransform failed: " + err, err);
			  }
		      }
		  }
	      }
	  }
      }

      if (event.track.kind == "audio" && audioStreamHandler) {
          pc_log(LOG_LEVEL_INFO, `onTrack: calling ash(${pc.convid.substring(0,8)}, ${label}) with ${event.streams.length} streams`);
	  audioStreamHandler(
	      pc.convid,
              hnd.toString() + label,
	      event.streams);
      }
      if (event.track.kind == "video" && videoStreamHandler) {
          let userid = pc.remote_userid;
	  let clientid = pc.remote_clientid;

	  const uinfo: UserInfo = pc.users[label];

	  if (uinfo) {
	      userid = uinfo.userid;
	      clientid = uinfo.clientid;
	  }

          if (userid != "sft" && userid != "") {
              pc_log(LOG_LEVEL_INFO, `onTrack: calling vsh(${pc.convid.substring(0,8)}, ${userid.substring(0,8)}, ${clientid.substring(0,4)}) with ${event.streams.length} streams`);
	      videoStreamHandler(
	          pc.convid,
	          userid,
	          clientid,
	          event.streams);
	  }
      }
  };
}

function pc_Close(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_Close: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  if (pc.rtc) {
    pc.rtc.getTransceivers().forEach(trans => {
      const track = trans.receiver.track;
      if (track && track.kind === "audio") {
        let label: String = '';
	if (pc_env == ENV_FIREFOX)
	  label = track.id;
	else
          label = track.label;
        if (audioStreamHandler && pc.rtc) {
          pc_log(LOG_LEVEL_INFO, `pc_Close: calling ash(${pc.convid.substring(0,8)}, ${label}) with 0 streams`);
          audioStreamHandler(pc.convid, hnd.toString() + label, null);
        }
      }
    });
  }
  worker.postMessage({op: 'destroy', self: pc.self});

  connectionsStore.removePeerConnection(hnd);
    
  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }
    
  rtc.close();
  pc.rtc = null;
}

function pc_AddTurnServer(
  hnd: number,
  urlPtr: number,
  usernamePtr: number,
  passwordPtr: number
) {
  pc_log(LOG_LEVEL_INFO, `pc_AddTurnServer: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  const url = em_module.UTF8ToString(urlPtr);
  const username = em_module.UTF8ToString(usernamePtr);
  const credential = em_module.UTF8ToString(passwordPtr);

  pc_log(LOG_LEVEL_INFO, `pc_AddTurnServer: hnd=${hnd} adding: ${url}`);

  const server = {
    urls: url,
    username: username,
    credential: credential
  };

  pc.turnServers.push(server);
}

function pc_HasVideo(hnd: number) : number {
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
      return 0;
  }

    pc_log(LOG_LEVEL_INFO, `pc_HasVideo(${hnd}): sending=${pc.sending_video}`)

    const senders = pc.rtc ? pc.rtc.getSenders() : [];
    pc_log(LOG_LEVEL_INFO, "pc_HasVideo: " + senders.length + " senders")
    for (const sender of senders) {
	pc_log(LOG_LEVEL_INFO, "pc_HasVideo: track=" + sender.track)
	if (sender.track) {
	    const track = sender.track;
	    pc_log(LOG_LEVEL_INFO, "pc_HasVideo: track.kind=" + track.kind + " enabled=" + track.enabled);

	    if (track.kind === 'video' && track.enabled)
		return 1;
	}
    }

    const txrxs = pc.rtc ? pc.rtc.getTransceivers() : [];
    for (const txrx of txrxs) {
	if (!txrx) {
	    pc_log(LOG_LEVEL_INFO, "pc_HasVideo: no txrx");
	    continue;
	}
	const rx = txrx.receiver;
	const tx = txrx.sender;
	pc_log(LOG_LEVEL_INFO, `pc_HasVideo: txrx dir=${txrx.direction}/${txrx.currentDirection} rx=${rx} tx=${tx}`);
    }
    
    return 0;
} 

function pc_SetVideoState(hnd: number, vstate: number) {
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
      return;
  }

  pc_log(LOG_LEVEL_INFO, `pc_SetVideoState: hnd=${hnd} vstate=${vstate}/${pc.vstate} callType=${pc.call_type} active=${pc.sending_video}`);
    
    if (pc.vstate === vstate)
	return;

    let active = false;

    switch(vstate) {
    case PC_VIDEO_STATE_STARTED:
    case PC_VIDEO_STATE_SCREENSHARE:
	active = true;
	break;

    default:
	active = false;
	break;
    }
    
    const rtc = pc.rtc;
    if (!rtc)
	return;
    
    let should_update = false;

    if (pc.vstate === PC_VIDEO_STATE_SCREENSHARE)
	should_update = true;
    else if (active && (pc.call_type !== CALL_TYPE_VIDEO || !pc.sending_video))
	should_update = true;
    else if (!active && (pc.call_type === CALL_TYPE_VIDEO || pc.sending_video))
	should_update = true;
    else if (vstate === PC_VIDEO_STATE_SCREENSHARE)
	should_update = true;

    pc_log(LOG_LEVEL_INFO, `pc_SetVideoState: should_update=${should_update} vstate=${vstate} active=${active}`);
    if (should_update && userMediaHandler) {
	const use_video = vstate === PC_VIDEO_STATE_STARTED;
	const use_ss = vstate === PC_VIDEO_STATE_SCREENSHARE;
	pc_log(LOG_LEVEL_INFO, `pc_SetVideoState: calling umh(1, ${use_video}, ${use_ss})`);
	userMediaHandler(pc.convid, true, use_video, use_ss)
	    .then((stream: MediaStream) => {
            return update_tracks(pc, stream);
	    });
	pc.sending_video = use_video || use_ss;
    }
    
    pc.vstate = vstate
}

function sdpMap(sdp: string, local: boolean, bundle: boolean): string {
    const sdpLines:  string[] = [];
    
    sdp.split('\r\n').forEach(sdpLine => {
	let outline: string | null;

	outline = sdpLine;

	if (local && !bundle) {
	    outline = sdpLine.replace(/^m=(application|video) 0/, 'm=$1 9');
	}
	else if (local && bundle) {
	    outline = sdpLine.replace(/^m=(application|video) 9/, 'm=$1 0');
	}
	else {
	    if(sdpLine.startsWith('a=sctpmap:')) {
		outline = 'a=sctp-port:5000';
	    }
	}
      
	if (outline != null) {
            sdpLines.push(outline);
	}
    });

    return sdpLines.join('\r\n');
}

function sdpCbrMap(sdp: string): string {
    const sdpLines:  string[] = [];
    
    sdp.split('\r\n').forEach(sdpLine => {
      let outline: string | null;

      outline = sdpLine;

      if(sdpLine.endsWith('sprop-stereo=0;useinbandfec=1')) {
        outline = sdpLine + ";cbr=1";
      }
      else if (sdpLine.startsWith('a=ssrc-group:')) {
        const group = sdpLine;
        // in case of only one ssrc in group (no nack/rtx), remove attribute
        const spaces = group.trim().split(' ').length;
        if (spaces < 3) {
          outline = null;
        }
      }

      else if (sdpLine.startsWith('a=simulcast:recv l;h') && pc_env !== ENV_FIREFOX) {
          // add conference attribute for chrome
          sdpLines.push('a=x-google-flag:conference')
      }
      // reorder rids for firefox
      else if (sdpLine.startsWith('a=simulcast:recv l;h') && pc_env === ENV_FIREFOX) {
          outline = 'a=simulcast:recv h;l'
      }

      else if (sdpLine.startsWith('a=rid:l recv') && pc_env === ENV_FIREFOX) {
          outline = 'a=rid:h recv';
      }

      else if (sdpLine.startsWith('a=rid:h recv') && pc_env === ENV_FIREFOX) {
          outline = 'a=rid:l recv';
      }

      if (outline != null) {
        sdpLines.push(outline);
      }
  });

  return sdpLines.join('\r\n');
}

function sdpRidReOrder (sdp: string): string {
    const sdpLines:  string[] = [];

    sdp.split('\r\n').forEach(sdpLine => {
        let outline: string | null;

        outline = sdpLine;

        if (sdpLine.startsWith('a=simulcast:send h;l') && pc_env === ENV_FIREFOX) {
            outline = 'a=simulcast:send l;h';
        }

        else if (sdpLine.startsWith('a=rid:h send') && pc_env === ENV_FIREFOX) {
            outline = 'a=rid:l send';
        }

        else if (sdpLine.startsWith('a=rid:l send') && pc_env === ENV_FIREFOX) {
            outline = 'a=rid:h send';
        }

        if (outline != null) {
            sdpLines.push(outline);
        }
    });
    return sdpLines.join('\r\n');
}

function pc_SetMediaKey(hnd: number, index: number, current: number, keyPtr: number, keyLen: number) {

  const pc = connectionsStore.getPeerConnection(hnd);

  if (pc == null) {
    return;
  }

  const buf = new ArrayBuffer(keyLen);
  const ptr = new Uint8Array(em_module.HEAPU8.buffer, keyPtr, keyLen);
  const buf8 = new Uint8Array(buf); 
  buf8.set(ptr);

  worker.postMessage({
    op: 'setMediaKey',
    self: pc.self,
    index: index,
    current: current,
    key: buf8
  });
}

function pc_UpdateSsrc(hnd: number, ssrcaPtr: number, ssrcvPtr: number) {

  const pc = connectionsStore.getPeerConnection(hnd);

  if (pc == null) {
    return;
  }

  const ssrca = em_module.UTF8ToString(ssrcaPtr);
  const ssrcv = em_module.UTF8ToString(ssrcvPtr);

  pc_log(LOG_LEVEL_INFO, `pc_UpdateSsrc: hnd=${hnd} ssrc=${ssrca} ssrcv=${ssrcv}`);

  worker.postMessage({
    op: 'updateSsrc',
    self: pc.self,
    ssrca: ssrca,
    ssrcv: ssrcv
  });
}


function setupSenderTransform(pc: PeerConnection, sender: any) {
  if (!sender || !sender.track) {
     return;
  }

  if (!insertableLegacy && !insertableStreams) {
      pc_log(LOG_LEVEL_WARN, "setupSenderTransform: insertable streams not supported");
      return;
  }  

  if (pc_env === ENV_FIREFOX) {
     sender.transform = new RTCRtpScriptTransform(worker, { name: "senderTransform", self: pc.self });
     return;
  }

  const mtype = sender.track.kind === 'video' ? 1 : 0; // corresponds to enum frame_media_type
  let senderStreams = null;

  if (insertableStreams)
     senderStreams = sender.createEncodedStreams();
  else
     senderStreams = mtype === 1 ? sender.createEncodedVideoStreams() : sender.createEncodedAudioStreams();

  pc_log(LOG_LEVEL_INFO, `setupSenderTransform: senderStream: ${senderStreams.readable}/${senderStreams.readableStream}`);
  
  const readableStream = senderStreams.readable || senderStreams.readableStream;
  const writableStream = senderStreams.writable || senderStreams.writableStream;


  worker.postMessage({   
    op: 'setupSender',
    self: pc.self,
    readableStream,
    writableStream,
  }, [readableStream, writableStream]);
}


function setupReceiverTransform(pc: PeerConnection, receiver: any) {
  if (!receiver || !receiver.track) {
      return;
  }

  if (!insertableLegacy && !insertableStreams) {
      pc_log(LOG_LEVEL_WARN, "setupReceiverTransform: insertable streams not supported");
      return;
  }

  if (pc_env == ENV_FIREFOX) {
     receiver.transform = new RTCRtpScriptTransform(worker, { name: "receiverTransform", self: pc.self, track_id: receiver.track.id });
     return;
  }

  const mtype = receiver.track.kind === 'video' ? 1 : 0 // corresponds to enum frame_media_type
  let receiverStreams = null;
  if (insertableStreams)
    receiverStreams = receiver.createEncodedStreams();
  else  
    receiverStreams =  mtype === 1 ? receiver.createEncodedVideoStreams() : receiver.createEncodedAudioStreams();

  pc_log(LOG_LEVEL_INFO, `setupReceiverTransform: receiverStream: ${receiverStreams.readable}/${receiverStreams.readableStream}`);

  const readableStream = receiverStreams.readable || receiverStreams.readableStream;
  const writableStream = receiverStreams.writable || receiverStreams.writableStream;

  worker.postMessage({
    op: 'setupReceiver',
    self: pc.self,
    readableStream,
    writableStream,
  }, [readableStream, writableStream]);
}

function createSdp(
  pc: PeerConnection,
  callType: number,
  vstate:  number,
  isOffer: boolean
) {
    const rtc = pc.rtc;

    pc_log(LOG_LEVEL_INFO, `createSdp: isOffer=${isOffer} rtc=${rtc}`); 
    if (!rtc) {
	return;
    }

    pc.call_type = callType;
    pc.vstate = vstate;

    const use_video = vstate === PC_VIDEO_STATE_STARTED;
    const use_ss = vstate === PC_VIDEO_STATE_SCREENSHARE;
    
    pc_log(LOG_LEVEL_INFO, `createSdp: calling umh(1, ${use_video}, ${use_ss})`);

    pc.sending_video = use_video || use_ss;
    
    if (userMediaHandler) {
	userMediaHandler(pc.convid, true, use_video, use_ss)
        .then((stream: MediaStream) => {
            return update_tracks(pc, stream).then(() => stream);
        }).then((stream: MediaStream) => {

		const doSdp: (options: RTCOfferOptions) => Promise<RTCSessionDescriptionInit> = isOffer
		      ? rtc.createOffer
		      : rtc.createAnswer;

		const offerVideoRx: RTCOfferOptions = isOffer ? {offerToReceiveVideo: true} : {};

		ccallStartGatherHandler(pc);

		doSdp
		    .bind(rtc)(offerVideoRx)
		    .then(sdp => {
			const typeStr = sdp.type;
			const sdpStr = sdp.sdp || '';

			pc_log(LOG_LEVEL_INFO, `createSdp: type=${typeStr} sdp=${sdpStr}`);
			
			const modSdp = sdpMap(sdpStr, true, false);
			ccallLocalSdpHandler(pc, 0, typeStr, modSdp);
		    })
		    .catch((err: any) => {
		        pc_log(LOG_LEVEL_WARN, 'createSdp: doSdp failed: ' + err, err);
			ccallLocalSdpHandler(pc, 1, "sdp-error", err.toString());
		    })
	    })
	    .catch((err: any) => {
	        pc_log(LOG_LEVEL_WARN, 'createSdp: userMedia failed: ' + err, err);
		ccallLocalSdpHandler(pc, 1, "media-error", err.toString());
	    });
    }
}

function pc_CreateOffer(hnd: number, callType: number, vstate: number) {
  const pc = connectionsStore.getPeerConnection(hnd);

  if (pc == null) {
    return;
  }

  pc_log(
    LOG_LEVEL_INFO,
    `pc_CreateOffer: hnd=${hnd} self=${pc.self.toString(16)} call_type=${callType}`
  );

  createSdp(pc, callType, vstate, true);
}

function pc_CreateAnswer(hnd: number, callType: number, vstate: number) {
  pc_log(LOG_LEVEL_INFO, `pc_CreateAnswer: ${hnd} callType=${callType}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  pc_log(LOG_LEVEL_INFO, 'pc_CreateAnswer: pc=' + pc);
  if (pc == null) {
    return;
  }

  createSdp(pc, callType, vstate, false);
}

function pc_AddDecoderAnswer(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_AddDecoderAnswer: ${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  pc_log(LOG_LEVEL_INFO, 'pc_AddDecoderAnswer: pc=' + pc);
  if (pc == null) {
    return;
  }
  const rtc = pc.rtc;
  if (!rtc)
     return;
     
  rtc.createAnswer().then(sdp => {
	const typeStr = sdp.type;
	const sdpStr = sdp.sdp || '';

	 //pc_log(LOG_LEVEL_INFO, `createSdp: type=${typeStr} sdp=${sdpStr}`);
			
	ccallLocalSdpHandler(pc, 0, typeStr, sdpStr);
  })
  .catch((err: any) => {
	pc_log(LOG_LEVEL_WARN, 'addDecoderAnswer: createAnswer failed: ' + err, err);  
	ccallLocalSdpHandler(pc, 1, "sdp-error", err.toString());
  });
}

function pc_AddUserInfo(hnd: number, labelPtr: number,
	                useridPtr: number, clientidPtr: number,
			ssrcaPtr: number, ssrcvPtr: number,
	                audioIvPtr: number, videoIvPtr: number, ivlen: number) {
  pc_log(LOG_LEVEL_INFO, `pc_AddUserInfo2: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  const label = em_module.UTF8ToString(labelPtr);
  const userId = em_module.UTF8ToString(useridPtr);
  const clientId = em_module.UTF8ToString(clientidPtr);
  const ssrca = em_module.UTF8ToString(ssrcaPtr);
  const ssrcv = em_module.UTF8ToString(ssrcvPtr);

  const iva = new ArrayBuffer(ivlen);
  const aptr = new Uint8Array(em_module.HEAPU8.buffer, audioIvPtr, ivlen);
  const iva8 = new Uint8Array(iva); 
  iva8.set(aptr);

  const ivv = new ArrayBuffer(ivlen);
  const vptr = new Uint8Array(em_module.HEAPU8.buffer, videoIvPtr, ivlen);
  const ivv8 = new Uint8Array(ivv); 
  ivv8.set(vptr);

  const uinfo : UserInfo = {
  	label: label,
  	userid: userId,
	clientid: clientId,
	ssrca: ssrca,
	ssrcv: ssrcv,
	iva: iva8,
	ivv: ivv8,
	audio_level: 0,
	first_recv_audio: false,
	first_succ_audio: false,
	first_succ_video: false,
	transp_ssrcv: null,
  };

  pc_log(LOG_LEVEL_INFO, `pc_AddUserInfo: label=${label} ${userId.substring(0,8)}/${clientId.substring(0,4)} ssrc:${ssrca}/${ssrcv}`);

  pc.users[label] = uinfo;

  worker.postMessage({
    op: 'addUser',
    self: pc.self,
    userInfo: uinfo
  });
}

function pc_RemoveUserInfo(hnd: number, labelPtr: number) {
  const label = em_module.UTF8ToString(labelPtr);

  pc_log(LOG_LEVEL_INFO, `pc_RemoveUserInfo: hnd=${hnd} label=${label}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }
  if (pc.users.hasOwnProperty(label)) {
    if (audioStreamHandler) {
      pc_log(LOG_LEVEL_INFO, `pcRemoveUserInfo: calling ash(${pc.convid.substring(0,8)}, ${label}) with 0 streams`);
      audioStreamHandler(pc.convid, hnd.toString() + label, null);
    }
    delete pc.users[label];
  }
}


function extractSSRCs(pc: PeerConnection,
                      sdp: string) {

  sdp.split('\r\n').forEach(l => {
    let m = l.match(/a=ssrc:(\d+) msid:(.*) (.*)/)
    if (m) {
      let ssrc = m[1];
      let label = m[3];

      pc.streams[ssrc] = label;
    }
  });
}

function pc_SetRemoteDescription(hnd: number, typePtr: number, sdpPtr: number) {
  pc_log(LOG_LEVEL_INFO, `pc_SetRemoteDescription: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  const type = em_module.UTF8ToString(typePtr);
  let sdp = em_module.UTF8ToString(sdpPtr);

  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }

  let sdpStr = sdp;
  if (pc_env === ENV_FIREFOX) {
      sdpStr = sdp.replace(/ DTLS\/SCTP (5000|webrtc-datachannel)/, ' UDP/DTLS/SCTP webrtc-datachannel');
      sdpStr = sdpMap(sdpStr, false, false);
  }

  /* Ensure that we force CBR on the offer */
  if (pc.conv_type == CONV_TYPE_CONFERENCE) {
     sdpStr = sdpCbrMap(sdpStr);
  }

  pc_log(LOG_LEVEL_INFO, `pc_SetRemoteDescription: hnd=${hnd} SDP=${sdpStr}`);

  rtc
    .setRemoteDescription({ type: type, sdp: sdpStr })
    .then(() => {})
    .catch((err: any) => {
      pc_log(LOG_LEVEL_WARN, "setRemoteDescription failed: " + err, err);
    });

  if (pc_env != ENV_FIREFOX)
    extractSSRCs(pc, sdp);
}

function pc_SetLocalDescription(hnd: number, typePtr: number, sdpPtr: number) {
  pc_log(LOG_LEVEL_INFO, `pc_SetLocalDescription: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  const type = em_module.UTF8ToString(typePtr);
  const sdp = em_module.UTF8ToString(sdpPtr);

  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }

  let sdpStr = '';

    if (pc_env === ENV_FIREFOX)
	sdpStr = sdpMap(sdp, true, false);
    else
	sdpStr = sdp;
    

    //pc_log(LOG_LEVEL_INFO, `pc_SetLocalDesription: type=${type} sdp=${sdpStr}`);
  rtc
    .setLocalDescription({ type: type, sdp: sdpStr })
    .then(() => {
      if (rtc && pc.conv_type === CONV_TYPE_CONFERENCE) {
	for (const sender of rtc.getSenders())
	  setupSenderTransform(pc, sender);
	}
    })
    .catch((err: any) => {
      pc_log(LOG_LEVEL_INFO, "setLocalDescription failed: " + err, err);
    });
}

function pc_LocalDescription(hnd: number, typePtr: number) {
  pc_log(LOG_LEVEL_INFO, `pc_LocalDescription: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
     return 0;
  }

  const rtc = pc.rtc;
  if (!rtc) {
    return 0;
  }
  const sdpDesc = rtc.localDescription;
  if (!sdpDesc) {
    return 0;
  }

  if (typePtr != null) {
    const type = em_module.UTF8ToString(typePtr);
    if (type != sdpDesc.type) {
      pc_log(LOG_LEVEL_WARN, "pc_LocalDescriptiont: wrong type");
      return 0;
    }
  }

  const sdp = sdpDesc.sdp.toString();
  let sdpStr = sdp;

  pc_log(LOG_LEVEL_INFO, `pc_LocalDescription: env=${pc_env}`);
    
  if (pc_env === ENV_FIREFOX) {
      sdpStr = sdp.replace(' UDP/DTLS/SCTP', ' DTLS/SCTP');	    
      sdpStr = sdpMap(sdpStr, true, false);
  }
    
    /* Ensure that we force CBR on the offer */
    if (pc_env === ENV_FIREFOX && pc.conv_type == CONV_TYPE_CONFERENCE) {
        sdpStr = sdpRidReOrder(sdpStr);
    }

    pc_log(LOG_LEVEL_INFO, `pc_LocalDescription: hnd=${sdpStr}`);


  const sdpLen = em_module.lengthBytesUTF8(sdpStr) + 1; // +1 for '\0'
  const ptr = em_module._malloc(sdpLen);
    
  em_module.stringToUTF8(sdpStr, ptr, sdpLen);

  return ptr;
}

function pc_HeapFree(ptr: number) {
  em_module._free(ptr);
}

function pc_IceGatheringState(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_IceGatheringState: hnd=${hnd}`);

  let state = PC_GATHER_STATE_UNKNOWN;
  const pc = connectionsStore.getPeerConnection(hnd);

  if (!pc) {
    return state;
  }

  const rtc = pc.rtc;
  if (!rtc) {
    return state;
  }
  const stateStr = rtc.iceGatheringState;

  switch (stateStr) {
    case "new":
      state = PC_GATHER_STATE_NEW;
      break;

    case "gathering":
      state = PC_GATHER_STATE_GATHERING;
      break;

    case "complete":
      state = PC_GATHER_STATE_COMPLETE;
      break;
  }

  return state;
}

function pc_SignalingState(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_SignalingState: hnd=${hnd}`);

  let state = PC_SIG_STATE_UNKNOWN;
  const pc = connectionsStore.getPeerConnection(hnd);
  if (!pc) {
    return state;
  }

  const rtc = pc.rtc;
  if (!rtc) {
    return state;
  }
  const stateStr = rtc.signalingState;
  state = sigState(stateStr);

  pc_log(
    LOG_LEVEL_INFO,
    `pc_SignalingState: hnd=${hnd} ` + stateStr + " mapped: " + state
  );

  return state;
}

function pc_ConnectionState(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_ConnectionState: hnd=${hnd}`);
  const pc = connectionsStore.getPeerConnection(hnd);
  if (!pc) {
    return 0;
  }

  /* Does this need mapping to an int, if it comes as a string,
   * or we return a string???
   */
  const rtc = pc.rtc;
  if (!rtc) {
    return 0;
  }
  const state = rtc.connectionState;

  return state;
}

function pc_SetMute(hnd: number, muted: number) {
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  pc.muted = muted !== 0;
  setMute(pc);
}

function pc_GetMute(hnd: number) {
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return 0;
  }

  return pc.muted;
}

function pc_SetRemoteUserClientId(
  hnd: number,
  useridPtr: number,
  clientidPtr: number
) {
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return;
  }

  pc.remote_userid = em_module.UTF8ToString(useridPtr);
  pc.remote_clientid = em_module.UTF8ToString(clientidPtr);
}

/* Data Channel related */
function pc_CreateDataChannel(hnd: number, labelPtr: number) {
  pc_log(LOG_LEVEL_INFO, `pc_CreateDataChannel: hnd=${hnd}`);
  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
    return 0;
  }

  const rtc = pc.rtc;
  if (!rtc) {
    return 0;
  }
  const label = em_module.UTF8ToString(labelPtr);
  const dc = rtc.createDataChannel(label);
  let dcHnd = 0;
  if (dc != null) {
    dcHnd = setupDataChannel(pc, dc);
  }

  return dcHnd;
}

function pc_DataChannelId(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_DataChannelId: hnd=${hnd}`);

  const dc = connectionsStore.getDataChannel(hnd);
  if (dc == null) {
    return -1;
  }

  return dc.id;
}

function pc_DataChannelState(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_DataChannelState: hnd=${hnd}`);

  let state = DC_STATE_ERROR;

  const dc = connectionsStore.getDataChannel(hnd);
  if (dc == null) {
    return state;
  }

  const str = dc.readyState;

  if (str == "connecting") {
    state = DC_STATE_CONNECTING;
  } else if (str == "open") {
    state = DC_STATE_OPEN;
  } else if (str == "closing") {
    state = DC_STATE_CLOSING;
  } else if (str == "closed") {
    state = DC_STATE_CLOSED;
  }

  return state;
}

function pc_DataChannelSend(hnd: number, dataPtr: number, dataLen: number) {
  pc_log(LOG_LEVEL_INFO, `pc_DataChannelSend: hnd=${hnd}`);

  const dc = connectionsStore.getDataChannel(hnd);
  if (dc == null) {
    return;
  }

  if (dc.readyState !== 'open') {
      pc_log(LOG_LEVEL_WARN, `pc_DataChannelSend: hnd=${hnd} not open`);
      return;
  }

  //const data = new Uint8Array(em_module.HEAPU8.buffer, dataPtr, dataLen);
  const data = em_module.UTF8ToString(dataPtr);

  dc.send(data);
}

function pc_DataChannelClose(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_DataChannelClose: hnd=${hnd}`);

  const dc = connectionsStore.getDataChannel(hnd);
  if (dc == null) {
    return;
  }

  dc.close();
}

/* Internal functions, used by avs_wcall directly */

function pc_InitModule(module: any, logh: WcallLogHandler) {
  createWorker();
  em_module = module;
  logFn = logh;

  const pt : any = RTCRtpSender.prototype;  

  insertableStreams = !!pt.createEncodedStreams || "transform" in pt;
  insertableLegacy = !!pt.createEncodedVideoStreams;
        
  pc_log(LOG_LEVEL_INFO, `insertable: ${insertableLegacy}/${insertableStreams}`);

  const callbacks = [
    [pc_SetEnv, "vi"],
    [pc_New, "iiiiii"],
    [pc_Create, "viii"],
    [pc_Close, "vi"],
    [pc_HeapFree, "vi"],
    [pc_AddTurnServer, "viiii"],
    [pc_IceGatheringState, "ii"],
    [pc_SignalingState, "ii"],
    [pc_ConnectionState, "ii"],
    [pc_CreateDataChannel, "iii"],
    [pc_CreateOffer, "viii"],
    [pc_CreateAnswer, "viii"],
    [pc_AddDecoderAnswer, "vi"],
    [pc_AddUserInfo, "viiiiiiiii"],
    [pc_RemoveUserInfo, "vii"],
    [pc_SetRemoteDescription, "viii"],
    [pc_SetLocalDescription, "viii"],
    [pc_LocalDescription, "iii"],
    [pc_SetMute, "vii"],
    [pc_GetMute, "ii"],
    [pc_GetLocalStats, "vi"],
    [pc_SetRemoteUserClientId, "viii"],
    [pc_HasVideo, "ii"],
    [pc_SetVideoState, "vii"],
    [pc_DataChannelId, "ii"],
    [pc_DataChannelState, "ii"],
    [pc_DataChannelSend, "viii"],
    [pc_DataChannelClose, "vi"],
    [pc_SetMediaKey, "viiiii"],
    [pc_UpdateSsrc, "viii"],
  ].map(([callback, signature]) => em_module.addFunction(callback, signature));

  em_module.ccall(
    "pc_set_callbacks",
    "null",
    callbacks.map(() => "number"),
    callbacks
  );
}

function pc_SetUserMediaHandler(umh: UserMediaHandler) {
  userMediaHandler = umh;
}

function pc_IsConferenceCallingSupported() {
  return insertableStreams || insertableLegacy;
}

function pc_SetAudioStreamHandler(ash: AudioStreamHandler) {
  audioStreamHandler = ash;
}

function pc_SetVideoStreamHandler(vsh: VideoStreamHandler) {
  videoStreamHandler = vsh;
}

function pc_ReplaceTrack(convid: string, newTrack: MediaStreamTrack) {
  const pcs = connectionsStore.getPeerConnectionByConvid(convid);
  if (pcs.length === 0) return;

  for (const pc of pcs) {
    if (!pc.rtc) {
      continue;
    }

      replace_track(pc, newTrack);
  }
}

function pc_GetStats(convid: string) : Promise<Array<{userid: string, stats: RTCStatsReport}>> {
  const pcs = connectionsStore.getPeerConnectionByConvid(convid);
  const statsPromises = [];

  for (const pc of pcs) {
    const rtc = pc.rtc;
    if (rtc)
      statsPromises.push(rtc.getStats().then(stats => ({userid: pc.remote_userid, stats: stats})));
  }

  return Promise.all(statsPromises) as Promise<Array<{userid: string, stats: RTCStatsReport}>>;
}

function pc_GetLocalStats(hnd: number) {
  pc_log(LOG_LEVEL_INFO, `pc_GetLocalStats: hnd=${hnd}`);

  const pc = connectionsStore.getPeerConnection(hnd);
  if (pc == null) {
     return;
  }

  const rtc = pc.rtc;
  if (!rtc) {
    return;
  }

  const txrxs = rtc.getTransceivers();
  txrxs.forEach(txrx => {
    const rx = txrx.receiver;
    if (rx) {
      const ssrcs = rx.getSynchronizationSources();
      ssrcs.forEach(ssrc => {
          let ssrca = "";
	  let aulevel = 0;

	  if (typeof ssrc.audioLevel !== 'undefined')
	      aulevel = ((ssrc.audioLevel * 512.0) | 0);

	  if (pc.conv_type != CONV_TYPE_ONEONONE) {
              const uinfo = uinfo_from_ssrca(pc, ssrc.source.toString());
	      if (uinfo) {
	         uinfo.audio_level = aulevel;
	         ssrca = uinfo.ssrca;
	      }
	  }

	  em_module.ccall(
	    "pc_set_audio_level",
	    null,
	    ["number", "number", "number"],
	    [pc.self, ssrca, aulevel]);
      });
      const csrcs = rx.getContributingSources();
      csrcs.forEach(csrc => {
        const uinfo = uinfo_from_ssrca(pc, csrc.source.toString());
	if (uinfo) {
	  uinfo.audio_level = 0;
	  if (typeof csrc.audioLevel !== 'undefined')
	    uinfo.audio_level = ((csrc.audioLevel * 512.0) | 0);

	  em_module.ccall(
	    "pc_set_audio_level",
	    null,
	    ["number", "number", "number"],
	    [pc.self, uinfo.ssrca, uinfo.audio_level]);
	  }
	});
     }
  });

  let self_audio_level = 0;
  let apkts = 0;
  let vpkts = 0;
  let ploss = 0;

  rtc.getStats()
    .then((stats) => {
	let rtt = 0;

        stats.forEach(stat => {
	    if (stat.type === 'inbound-rtp') {
               ploss = ploss + stat.packetsLost;
		const p = stat.packetsReceived;		
		if (stat.kind === 'audio') {
		   apkts = apkts + p;
		}
		else if (stat.kind === 'video') {
		   vpkts = vpkts + p;
		}		
	    }
	    else if (stat.type === 'outbound-rtp') {
		const p = stat.packetsSent;		
		if (stat.kind === 'audio') {
		    pc.stats.sent_apkts = p;
		}
		else if (stat.kind === 'video') {
		    pc.stats.sent_vpkts = p;
		}		
	    }	    
	    else if (stat.type === 'candidate-pair') {
		rtt = stat.currentRoundTripTime * 1000;
	    }
	    else if (stat.type === 'media-source') {
	    	 if (stat.kind === 'audio')
	            self_audio_level = stat.audioLevel ? ((stat.audioLevel * 512.0) | 0) : 0;
	    }
	});
	pc.stats.recv_apkts = apkts;
	pc.stats.recv_vpkts = vpkts;
	pc.stats.ploss = ploss - pc.stats.lastploss;
	pc.stats.lastploss = ploss;

	em_module.ccall(
	    "pc_set_stats",
	    null,
	    ["number",
	     "number",
	     "number", "number",
	     "number", "number",
	     "number", "number"],
	    [pc.self,
	     self_audio_level,
	     pc.stats.recv_apkts, pc.stats.recv_vpkts,
	     pc.stats.sent_apkts, pc.stats.sent_vpkts,
	     pc.stats.ploss, rtt]
	);
    })
    .catch((err) => pc_log(LOG_LEVEL_INFO, `pc_GetLocalStats: failed hnd=${hnd} err=${err}`, err));
}

export default {
  init: pc_InitModule,
  setUserMediaHandler: pc_SetUserMediaHandler,
  setAudioStreamHandler: pc_SetAudioStreamHandler,
  setVideoStreamHandler: pc_SetVideoStreamHandler,
  isConferenceCallingSupported: pc_IsConferenceCallingSupported,
  replaceTrack: pc_ReplaceTrack,
  getStats: pc_GetStats
};
