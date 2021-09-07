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

pt_count = {};
d_min = {};
f = open(file_name,"rb")
try:
    byte = f.read(30)
    print byte
    length = read_uint16(f);
    D_min = 0xffffffff;
    packets = 0;
    #print length
    while length != -1:
        plen = read_uint16(f);
        time = read_uint32(f);
        if plen < 1:
            #print length;
            #print plen;
            tmp = f.read(length - 8);
            #print time
        else:
            tmp = read_uint8(f);
            pt = read_uint8(f);
            seq = read_uint16(f);
            timestamp = read_uint32(f);
            ssrc = read_uint32(f);
            tmp = f.read(plen - 12);
            packets = packets + 1;
            D = time - timestamp/48;
            if (pt in pt_count):
                pt_count[pt] = pt_count[pt] + 1;
                if(D < d_min[pt]): d_min[pt] = D
            else:
                pt_count[pt] = 1;
                d_min[pt] = D;
            #print time, seq
        length = read_uint16(f);

    pt_opus = -1;
    max_cnt = -1;
    for p, c in pt_count.iteritems():
        if c > max_cnt:
            pt_opus = p;
            max_cnt = c;
#print pt_opus
    D_min = d_min[pt];
#print D_min;
finally:
    f.close()

f = open(file_name,"rb")
try:
    packets = 0;
    timeBuf = array('f',[]);
    d1Buf = array('f',[]);
    dBuf = array('L',[]);
    timestampBuf = array('L',[]);
    seqBuf = array('L',[]);
    D_min_smth = time * 0.0001;
    alpha = 0.0001;
    prev_timestamp = -1;
    byte = f.read(30)
    length = read_uint16(f);
    while length != -1:
        plen = read_uint16(f);
        time = read_uint32(f);
            #if packets < 10:
            #print plen, time
        if plen < 1:
            tmp = f.read(length - 8);
        else:
            tmp = read_uint8(f);
            pt = read_uint8(f);
            seq = read_uint16(f);
            timestamp = read_uint32(f);
            ssrc = read_uint32(f);
            tmp = f.read(plen - 12);
            if pt == pt_opus:
                timeBuf.append(float(time)/1000.0);
                seqBuf.append(seq);
                timestampBuf.append(timestamp);
                d = time - timestamp/48;
                d1Buf.append(d);
                d = d - D_min;
                #if d == 0:
                #print time, seq, d
                if d < D_min_smth:
                    D_min_smth = d;
                else:
                    D_min_smth = D_min_smth*(1-alpha) + d*alpha;
                #d = d - int(D_min_smth)
                dBuf.append(d);
                packets = packets + 1;
        length = read_uint16(f);
finally:
    f.close()

#print timeBuf[0:10]
#print seqBuf[0:10]
#print timestampBuf[0:10]
#print dBuf[0:10]

maxD = max(dBuf);
pyplot.figure();
#pyplot.subplot(2,1,1);
#pyplot.plot(timeBuf,d1Buf);
#pyplot.xlim(min(timeBuf), max(timeBuf))
#pyplot.ylim(min(dBuf), maxD*1.2)
#pyplot.ylabel('Delay (ms) tot')
#pyplot.xlabel('Time (s)')
#pyplot.subplot(2,1,2);
pyplot.plot(timeBuf,dBuf);
pyplot.xlim(min(timeBuf), max(timeBuf))
pyplot.ylim(min(dBuf), maxD*1.2)
pyplot.ylabel('Delay (ms) tot')
pyplot.xlabel('Time (s)')

pyplot.savefig(pre + '_Delay.png');

#pyplot.show();
