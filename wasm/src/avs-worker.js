

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

    let ext = buf[0] & 0x10;
    const flen = ((buf[1] >>> 4) & 0x7) + 1;
    const x = (buf[1] >>> 3) & 0x1;
    const klen = buf[1] & 0x7;
    let p = 2;
    let kid = -1;
    let fid = -1;

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

    // Code below handles extensions, we don't use them for now...
    /*
    while(ext) {
	const b = buf[p];
	ext = b & 0x80;
	const elen = ((b >>> 4) & 0x7) + 2;
	p += elen;
    }
    */

    if (kid < 0 || fid < 0) {
	return null;
    }
    else {
	const frameHdr = {
	    len: p,
	    frameId: fid,
	    keyId: kid
	}
	return frameHdr;
    }
}

function frameHeaderEncode(fid, kid)
{
    const ab = new ArrayBuffer(32);
    const buf = new Uint8Array(ab);
    let sig = 0;
    let x = 0;
    let klen = 0;
    let flen = 0;
    let p = 2;
    let ext = 0;

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

    return buf.subarray(0, p);
}

function xor_iv(iv, fid, kid) {
  var oiv = new Uint8Array(iv);

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

function encryptFrame(coder, rtcFrame, controller) {
  const dataBuf = rtcFrame.data;
  const data = new Uint8Array(dataBuf);
  const isVideo = false;

  //console.log("encryptFrame=", rtcFrame);
  const t = Object.prototype.toString.call(rtcFrame);
  if (t === '[object RTCEncodedVideoFrame]')
      isVideo = true;
    
  if (coder.currentKey == null) {
    getCurrentMediaKey(coder.self);
    return;
  }

  const baseiv = isVideo ? coder.video.iv : coder.audio.iv;
  const iv = xor_iv(baseiv, coder.frameId, coder.currentKey.id);
    
  const hdr = frameHeaderEncode(coder.frameId, coder.currentKey.id);
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

    coder.frameId++;
  });
}

function decryptFrame(coder, rtcFrame, controller) {
  const dataBuf = rtcFrame.data;
  const data = new Uint8Array(dataBuf);
  const ssrc = rtcFrame.synchronizationSource.toString();

  let uinfo = coder.audio.users[ssrc];
  let isVideo = false;
    
  if (!uinfo) {
      uinfo = coder.video.users[ssrc];
      isVideo = true;
  }
  if (!uinfo) {
      console.log('decryptFrame: no userinfo for ssrc: ' + ssrc);
      return;
  }
    
  const frameHdr = frameHeaderDecode(data);

  if (frameHdr == null) {
    console.log('decryptFrame: failed to decode frame header');
    return;
  }

  /*
  console.log("coder[" + coder.self + "]"
	+ " len=" + frameHdr.len
	+ " kid="+ frameHdr.keyId
	+ " fid=" + frameHdr.frameId);
  */

  var kinfo = null;
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
  })
  .catch(err => {
     console.log(err);
  });
}

onmessage = async (event) => {
    const {op, self} = event.data;
    console.log('AVS worker op=' + op + ' self=' + self);

    let coder = coders[self];
    if (!coder) {
	console.log('AVS worker: no coder for self=' + self);
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
		frameId: Math.random() * 4294967296,		
		currentKey: null,
		keys: [],
		audio: {
		    iv: iva,
		    users: [],
		},
		video: {
		    iv: ivv,
		    users: [],
		},
	    }
	    coders[self] = coder;

	    console.log('AVS worker: adding coder for self=' + self);
	}
    }
    else if (op === 'destroy') {
	coders[self] = null;
    }
    else if (op === 'addUser') {
	if (!coder) {
	    console.log('addUser: no coder');
	    return;
	}
	const {userInfo} = event.data;

	console.log('addUser: adding audio ssrc: '+userInfo.ssrca);
	coder.audio.users[userInfo.ssrca] = userInfo;
	if (userInfo.ssrcv != 0) {
	    coder.video.users[userInfo.ssrcv] = userInfo;
	}
    }
    else if (op === 'setupSender') {
	const {readableStream, writableStream} = event.data;

	const transformStream = new TransformStream({
	    transform: (frame, controller) => {encryptFrame(coder, frame, controller);},
	});
	readableStream
          .pipeThrough(transformStream)
            .pipeTo(writableStream);

	console.log('AVS worker sender setup successfully');
	
    }
    else if (op === 'setupReceiver') {
       const {readableStream, writableStream} = event.data;

       if (!coder) {
	   console.log('setupSender: no coder for self=' + self);
           return;
       }

       const transformStream = new TransformStream({
	   transform: (frame, controller) => {decryptFrame(coder, frame, controller);},
       });
       readableStream
        .pipeThrough(transformStream)
        .pipeTo(writableStream);
       
	console.log('AVS worker receiver setup successfully');
   }
   else if (op === 'setMediaKey') {
       const {index, current, key} = event.data;

       console.log("setMediaKey: got key for: " + self
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
         var kinfo = null;
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
