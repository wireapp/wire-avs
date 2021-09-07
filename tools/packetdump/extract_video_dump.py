from array import *
import matplotlib.pyplot as pyplot
import sys
import os

def read_uint8(f):
    tmp = f.read(1)
    if tmp == "": return -1;
    out = int(tmp.encode('hex'),16)
    return out

def read_uint16(f, order='big'):
    if order == 'big':
        seq = [0,1];
    else:
        seq = [1,0];
    tmp = f.read(2)
    if tmp == "": return -1;
    out = 0
    for j in seq: out = (out << 8) + int(tmp[j].encode('hex'),16)
    return out

def read_uint32(f, order='big'):
    if order == 'big':
        seq = [0,1,2,3];
    else:
        seq = [3,2,1,0];
    tmp = f.read(4)
    out = 0
    for j in seq: out = (out << 8) + int(tmp[j].encode('hex'),16)
    return out

file_name = sys.argv[1];

pre, ext = os.path.splitext(file_name)

f = open(file_name,"rb")
try:
    byte = f.read(30)
    print byte
    length = read_uint16(f);
    D_min = 0xffffffff;
    packets = 0;
    while length != -1:
        plen = read_uint16(f);
        time = read_uint32(f);
        Vp8Len = read_uint32(f);
        tmp = read_uint8(f);
        pt = read_uint8(f);
        seq = read_uint16(f);
        timestamp = read_uint32(f);
        ssrc = read_uint32(f);
        byte = f.read(18)
        packets = packets + 1;
        D = time - timestamp/90;
        if(D < D_min): D_min = D
        length = read_uint16(f);
finally:
    f.close()

f = open(file_name,"rb")
try:
    byte = f.read(30)
    length = read_uint16(f);
    packets = 0;
    timeTot = array('f',[]);
    timeBase = array('f',[]);
    timeRtx = array('f',[]);
    dTot = array('L',[]);
    dBase = array('L',[]);
    dRtx = array('f',[]);
    Vp8Len = array('L',[]);
    timestampBuf = array('L',[]);
    seqBuf = array('L',[]);
    ptBuf = array('L',[]);
    D_min_smth = time * 0.0001;
    alpha = 0.0001;
    prev_timestamp = -1;
    rtxPt = -1000;
    while length != -1:
        plen = read_uint16(f);
        time = read_uint32(f);
        timeTot.append(float(time)/1000.0);
        Vp8Len.append(read_uint32(f,'little'));
        tmp = read_uint8(f);
        pt = read_uint8(f);
        ptBuf.append(pt);
        seq = read_uint16(f);
        seqBuf.append(seq);
        timestamp = read_uint32(f);
        timestampBuf.append(timestamp);
        ssrc = read_uint32(f);
        byte = f.read(18);
        d = time - timestamp/90;
        d = d - D_min;
        if d < D_min_smth:
            D_min_smth = d;
        else:
            D_min_smth = D_min_smth*(1-alpha) + d*alpha;
        d = d - int(D_min_smth)
        dTot.append(d);
        if (pt == 100 or pt == 228):
            dBase.append(d);
            timeBase.append(float(time)/1000.0);
        else:
            dRtx.append(d);
            timeRtx.append(float(time)/1000.0);
            rtxPt = pt & 0x7f;
        packets = packets + 1;
        length = read_uint16(f);
finally:
    f.close()

maxD = max(dTot);
#if maxD > 1000:
    #maxD = 1000;
pyplot.figure();
pyplot.subplot(3,1,1);
pyplot.plot(timeTot,dTot);
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylim(min(dTot), maxD*1.2)
pyplot.ylabel('Delay (ms) tot')
#pyplot.title('Total')
pyplot.subplot(3,1,2);
pyplot.plot(timeBase,dBase);
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylim(min(dTot), max(dBase)*1.2)
pyplot.ylabel('Delay (ms) base')
#pyplot.title('Base')
pyplot.subplot(3,1,3);
pyplot.plot(timeRtx,dRtx,'.');
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylim(min(dTot), maxD*1.2)
pyplot.ylabel('Delay (ms) rtx')
pyplot.xlabel('Time (s)')
#pyplot.title('Rtx')

#pyplot.show();

pyplot.savefig(pre + '_Delay.png');

# Plot the bitrate
timeRate = array('f',[]);
Rate = array('f',[]);
Pps = array('f',[]);
Fps = array('f',[]);
loss = array('f',[]);
for i in range(0,packets-1):
    t1 = timeTot[i];
    t2 = t1;
    j = i;
    totBytes = Vp8Len[j];
    pkts = 1;
    frames = 0;
    seqArr = array('f',[]);
    while t2 < (t1 + 1):
        j = j + 1;
        if j > (packets-1):
            t2 = t1 + 3;
        else:
            totBytes = totBytes + Vp8Len[j];
            if ptBuf[j] == 228 or ptBuf[j] == (rtxPt + 128):
                frames = frames + 1;
            if ptBuf[j] == 100 or ptBuf[j] == 228:
                seqArr.append(seqBuf[j]);
            pkts = pkts + 1;
            t2 = timeTot[j];
    dt = t2 - t1;
    timeRate.append((t1 + t2)/2);
    Rate.append((totBytes*8)/dt);
    Pps.append(pkts/dt);
    Fps.append(frames/dt);
    if len(seqArr) > 1:
        dSeq = (max(seqArr)-min(seqArr));
        L = dSeq - len(seqArr);
        if dSeq > 0:
            L = L/(max(seqArr)-min(seqArr));
            loss.append(L*100)
    else:
        loss.append(100)

pyplot.figure();
pyplot.subplot(4,1,1);
pyplot.plot(timeRate,Rate);
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylabel('Rate (bps)')
pyplot.subplot(4,1,2);
pyplot.plot(timeRate,Pps);
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylabel('Rate (pps)')
pyplot.subplot(4,1,3);
pyplot.plot(timeRate,Fps);
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylabel('Rate (Fps)')
pyplot.subplot(4,1,4);
pyplot.plot(timeRate,loss);
pyplot.xlim(min(timeTot), max(timeTot))
pyplot.ylim(0, 100)
pyplot.ylabel('Loss Rate (%)')
pyplot.xlabel('Time (s)')
pyplot.savefig(pre + '_Rate.png');

