README CALLING 3.0
------------------


Calling 3.0 is using E2EE messages for the signalling channel
and all call states are stored in the Clients.




Guidelines for the ECONN module:

- No static/global data, fully re-entrant




STATE MACHINE:
-------------

```

    send      .----------.      recv
   setup .----|   IDLE   |----. setup
         |    '----------'    |
         |                    |
        \|/                  \|/
    .----------.        .----------.
    | PENDING  |        | PENDING  |
    | OUTGOING |        | INCOMING |
    '----------'        '----------'
         |                    |
   recv  |                    | send
   setup |   \.-----------./  | setup
         '----| ANSWERED  |---'
             /'-----------'\
                    |
                    | datachan_established
                   \|/
             .-------------.
             | DATA CHANNEL|
             | ESTABLISHED |
             '-------------'
                   |
                   | conn_close
                  \|/
             .-------------.
             | TERMINATING |
             '-------------'
```



ARCHITECTURE:
------------

```
    .----------------.
    | User Interface |
    '----------------'
             |
             |
            \|/
    .----------------.        .-----------.
    |      ECALL     | -----> | Mediaflow |
    '----------------'        '-----------'
             |
             |
            \|/
    .----------------.
    |      ECONN     |.
    '----------------'|.
     '----------------'|
      '----------------'
```




SAMPLE MESSAGES:
---------------

SETUP and CANCEL messages are sent via Backend.
PROPSYNC and HANGUP messages are sent via DataChannel.


```
{
        "version" : "3.0",
        "type" : "SETUP",
        "sessid" : "4x12",
        "resp" : false,
        "sdp" : "v=0\r\no=- 2291589356 1242618047 IN IP4 192.168.10.231\r\ns=-\r\nc=IN IP4 192.168.10.231\r\nt=0 0\r\na=tool:avsmaster 0.snapshot (x86_64\/darwin)\r\na=group:BUNDLE audio video\r\na=x-OFFER\r\nm=audio 57485 UDP\/TLS\/RTP\/SAVPF 111\r\nc=IN IP4 54.73.89.65\r\nb=AS:50\r\na=rtpmap:111 opus\/48000\/2\r\na=fmtp:111 stereo=0;sprop-stereo=0;useinbandfec=1\r\na=rtcp:59775\r\na=sendrecv\r\na=mid:audio\r\na=ssrc:2924691974 cname:TLohO1cjIpEc8kJ\r\na=rtcp-mux\r\na=fingerprint:sha-256 07:52:B0:D8:DF:33:E0:54:8B:A6:DD:C0:3A:C9:FB:4F:80:E1:F6:CE:57:D0:C3:24:50:3D:6B:8D:98:EF:24:DE\r\na=setup:actpass\r\na=end-of-candidates\r\nm=video 57485 UDP\/TLS\/RTP\/SAVPF 100 96\r\nc=IN IP4 54.73.89.65\r\nb=AS:800\r\na=rtpmap:100 VP8\/90000\r\na=rtcp-fb:100 ccm fir\r\na=rtcp-fb:100 nack\r\na=rtcp-fb:100 nack pli\r\na=rtcp-fb:100 goog-remb\r\na=extmap:3 http:\/\/www.webrtc.org\/experiments\/rtp-hdrext\/abs-send-time\r\na=extmap:4 urn:3gpp:video-orientation\r\na=rtpmap:96 rtx\/90000\r\na=fmtp:96 apt=100\r\na=rtcp:59775\r\na=sendrecv\r\na=mid:video\r\na=rtcp-mux\r\na=fingerprint:sha-256 07:52:B0:D8:DF:33:E0:54:8B:A6:DD:C0:3A:C9:FB:4F:80:E1:F6:CE:57:D0:C3:24:50:3D:6B:8D:98:EF:24:DE\r\na=setup:actpass\r\na=ssrc-group:FID 3498994868 2813151609\r\na=ssrc:3498994868 cname:TLohO1cjIpEc8kJ\r\na=ssrc:3498994868 msid:m3oV2xznxFuDKG8CQ7sfXjTUjPqsqpjIuse bc9c4d5f-04a2-4cd1-1f89-6e3c39bcb6ee\r\na=ssrc:3498994868 mslabel:m3oV2xznxFuDKG8CQ7sfXjTUjPqsqpjIuse\r\na=ssrc:3498994868 label:bc9c4d5f-04a2-4cd1-1f89-6e3c39bcb6ee\r\na=ssrc:2813151609 cname:TLohO1cjIpEc8kJ\r\na=ssrc:2813151609 msid:m3oV2xznxFuDKG8CQ7sfXjTUjPqsqpjIuse bc9c4d5f-04a2-4cd1-1f89-6e3c39bcb6ee\r\na=ssrc:2813151609 mslabel:m3oV2xznxFuDKG8CQ7sfXjTUjPqsqpjIuse\r\na=ssrc:2813151609 label:bc9c4d5f-04a2-4cd1-1f89-6e3c39bcb6ee\r\nm=application 59774 DTLS\/SCTP 5000\r\na=sendrecv\r\na=mid:data\r\na=fingerprint:sha-256 07:52:B0:D8:DF:33:E0:54:8B:A6:DD:C0:3A:C9:FB:4F:80:E1:F6:CE:57:D0:C3:24:50:3D:6B:8D:98:EF:24:DE\r\na=setup:actpass\r\na=sctpmap:5000 webrtc-datachannel 1024\r\n",

        "props" : {
                "videosend" : "yes,please"
        }
}
```


A CANCEL request:
```
{
        "version" : "3.0",
        "type" : "CANCEL",
        "sessid" : "4x12",
        "resp" : false
}
```


A PROPSYNC request to sync local and remote properties:
```
{
        "version" : "3.0",
        "type" : "PROPSYNC",
        "sessid" : "Ui6E",
        "resp" : false,
        "props" : {
                "videosend" : "yes,please"
        }
}
```


A HANGUP request to end the call (via DataChannel)
```
{
        "version" : "3.0",
        "type" : "HANGUP",
        "sessid" : "Ui6E",
        "resp" : false
}
```




REFERENCES:
----------


http://soft.vub.ac.be/~tvcutsem/distsys/clocks.pdf


keyrotation for group-calls
re-SETUP (e.g. for network changes, re-keying)
turn: TLS-transport always
      DTLS-transport or not (needed for group-call key rotation)
      mobility extension
      dtls: use cookie to authenticate a romaing client

Calling3Flowsv10.pptx
