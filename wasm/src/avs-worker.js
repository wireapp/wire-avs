
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

  let sendXX = (sendXX)
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
          sendXX = {
              op: "setvstream",
              self: coder.self,
              userid: uinfo.userid,
              clientid: uinfo.clientid,
              ssrc: ssrc,
              track_id: track_id,
          };
          // postMessage({
          //     op: "setvstream",
          //     self: coder.self,
          //     userid: uinfo.userid,
          //     clientid: uinfo.clientid,
          //     ssrc: ssrc,
          //     track_id: track_id,
          // });
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

    if (isVideo && sendXX != null && rtcFrame.type == 'key') {
        doLog("##XX send:" + rtcFrame.type);
        postMessage(sendXX);
        sendXX = null;
    }
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
