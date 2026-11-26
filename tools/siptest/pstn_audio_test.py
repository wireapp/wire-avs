#!/usr/bin/env python3
#
# pstn_audio_test.py -- local PSTN audio and echo test with siptest
#
# A baresip CLI plays the "PSTN" caller: it dials siptest's SIP UA with a
# WAV file as microphone (1 kHz bursts) and records what it hears. The
# siptest wire client sends 600 Hz bursts from a file and records what
# it hears, so there is no audio device and no acoustic echo path.
#
# Checks:
#   pstn -> wire  the wire client hears the PSTN tone
#   wire -> pstn  the PSTN caller hears the wire tone
#   echo          the PSTN caller does not hear its own tone back
#                 (the wireaudio mix-minus, or any loop through WebRTC)
#
# Only the standard library is used: tones are measured per 20ms frame
# with a Goertzel filter.
#
# Example, from the wire-avs root after building siptest:
#   tools/siptest/pstn_audio_test.py \
#       --baresip ~/projects/baresip/build/baresip
#
# See README.md for the requirements, options and output.
#

import argparse
import math
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave

PSTN_SRATE = 8000      # G.711
WIRE_SRATE = 16000     # siptest audio files (wcall_set_audio_files)
PSTN_FREQ = 1000.0
WIRE_FREQ = 600.0
TONE_DBFS = -12.0
FRAME_MS = 20
FADE_SECS = 0.02

# The tone is considered present in a frame above this level
DETECT_DBFS = -50.0


def tone(srate, freq, secs, on_secs, off_secs, lead_secs, dbfs):
    """Bursts of a sine tone, starting after lead_secs of silence.

    The bursts fade in and out: hard edges splatter into the other
    tone's frequency and read as echo.
    """
    amp = 32767 * 10 ** (dbfs / 20)
    period = on_secs + off_secs
    samples = []
    for i in range(int(secs * srate)):
        t = i / srate - lead_secs
        pos = t % period
        if t < 0 or pos >= on_secs:
            samples.append(0)
            continue
        edge = min(pos, on_secs - pos)
        gain = (0.5 - 0.5 * math.cos(math.pi * edge / FADE_SECS)
                if edge < FADE_SECS else 1.0)
        samples.append(int(gain * amp * math.sin(2 * math.pi * freq * t)))
    return samples


def write_wav(path, srate, samples):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(srate)
        w.writeframes(struct.pack('<%dh' % len(samples), *samples))


def write_raw(path, samples):
    with open(path, 'wb') as f:
        f.write(struct.pack('=%dh' % len(samples), *samples))


def read_wav(path):
    with wave.open(path, 'rb') as w:
        if w.getsampwidth() != 2:
            sys.exit('%s: expected 16-bit samples' % path)
        ch = w.getnchannels()
        srate = w.getframerate()
        data = w.readframes(w.getnframes())
    samples = struct.unpack('<%dh' % (len(data) // 2), data)
    return srate, list(samples[::ch])


def read_raw(path, srate):
    with open(path, 'rb') as f:
        data = f.read()
    n = len(data) // 2
    return srate, list(struct.unpack('=%dh' % n, data[:n * 2]))


def goertzel_dbfs(frame, srate, freq):
    """Level of freq in frame, in dBFS of a full scale sine.

    Hann windowed: without it the other tone leaks in at about -30 dB.
    """
    n = len(frame)
    k = 2 * math.cos(2 * math.pi * freq / srate)
    s1 = s2 = 0.0
    for i, x in enumerate(frame):
        w = 0.5 - 0.5 * math.cos(2 * math.pi * i / n)
        s0 = x * w + k * s1 - s2
        s2 = s1
        s1 = s0
    power = s1 * s1 + s2 * s2 - k * s1 * s2
    amp = 4 * math.sqrt(max(power, 0.0)) / n  # Hann gain is 1/2
    return 20 * math.log10(amp / 32767) if amp > 0 else -120.0


def levels(srate, samples, freq):
    n = srate * FRAME_MS // 1000
    return [goertzel_dbfs(samples[i:i + n], srate, freq)
            for i in range(0, len(samples) - n + 1, n)]


def summary(lv):
    """Peak level and the fraction of frames with the tone present."""
    if not lv:
        return -120.0, 0.0
    peak = sorted(lv)[int(len(lv) * 0.99)]
    return peak, sum(1 for x in lv if x > DETECT_DBFS) / len(lv)


def echo_delay_ms(ref, out):
    """Lag (ms) of the best match of the tone on/off pattern."""
    a = [1 if x > DETECT_DBFS else 0 for x in ref]
    b = [1 if x > DETECT_DBFS else 0 for x in out]
    best, best_lag = -1, 0
    for lag in range(0, min(len(b), 2000 // FRAME_MS)):
        score = sum(x * y for x, y in zip(a, b[lag:]))
        if score > best:
            best, best_lag = score, lag
    return best_lag * FRAME_MS


BARESIP_CONFIG = """\
poll_method             kqueue
sip_listen              {addr}:{port}
module_path             {module_path}
audio_player            aufile,{play}
audio_source            aufile,{src}
audio_channels          1
ausrc_format            s16
auplay_format           s16
jitter_buffer_delay     5-10
rtcp_enable             yes
net_af                  ipv4
module                  g711.so
module                  aufile.so
module_app              account.so
module_app              menu.so
"""


def run(args, workdir):
    pstn_in = os.path.join(workdir, 'pstn_in.wav')
    pstn_out = os.path.join(workdir, 'pstn_out.wav')
    wire_play = os.path.join(workdir, 'wire_play.pcm')
    wire_rec = os.path.join(workdir, 'wire_rec.pcm')

    # The PSTN file ends the baresip call, so it is as long as the test;
    # the wire file loops
    write_wav(pstn_in, PSTN_SRATE,
              tone(PSTN_SRATE, PSTN_FREQ, args.duration,
                   0.5, 0.5, 1.0, TONE_DBFS))
    write_raw(wire_play,
              tone(WIRE_SRATE, WIRE_FREQ, 1.0, 0.3, 0.7, 0.0, TONE_DBFS))

    for p in (pstn_out, wire_rec):
        if os.path.exists(p):
            os.remove(p)

    confdir = os.path.join(workdir, 'baresip')
    os.makedirs(confdir, exist_ok=True)
    with open(os.path.join(confdir, 'config'), 'w') as f:
        f.write(BARESIP_CONFIG.format(
            addr=args.addr, port=args.baresip_port,
            module_path=args.module_path,
            play=os.path.basename(pstn_out),
            src=os.path.basename(pstn_in)))
    with open(os.path.join(confdir, 'accounts'), 'w') as f:
        f.write('<sip:pstn@%s:%d>;regint=0;audio_codecs=PCMU\n'
                % (args.addr, args.baresip_port))

    siptest_log = open(os.path.join(workdir, 'siptest.log'), 'w')
    baresip_log = open(os.path.join(workdir, 'baresip.log'), 'w')

    siptest_cmd = [args.siptest, '-f', args.sipconf,
                   '-p', wire_play, '-r', wire_rec,
                   '-d', str(int(args.duration) + 3)]
    print('siptest: %s' % ' '.join(siptest_cmd))
    siptest = subprocess.Popen(siptest_cmd, stdout=siptest_log,
                               stderr=subprocess.STDOUT)
    baresip = None
    try:
        # Let the wire client call the pstn client before dialing in
        time.sleep(args.setup_delay)
        if siptest.poll() is not None:
            sys.exit('siptest exited early, see %s' % siptest_log.name)

        baresip_cmd = [args.baresip, '-f', confdir, '-n', args.addr,
                       '-e', '/dial sip:wire@%s:%d' % (args.addr,
                                                     args.sip_port),
                       '-t', str(int(args.duration) + 5)]
        print('baresip: %s' % ' '.join(baresip_cmd))
        # Run in workdir: baresip truncates long audio device names,
        # so the config uses the file names only
        baresip = subprocess.Popen(baresip_cmd, cwd=workdir,
                                   stdout=baresip_log,
                                   stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)

        baresip.wait(timeout=args.duration + 15)
        siptest.wait(timeout=args.duration + 15)
    except subprocess.TimeoutExpired:
        print('timeout, stopping')
    finally:
        for p in (baresip, siptest):
            if p and p.poll() is None:
                p.send_signal(signal.SIGINT)
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()

    return pstn_in, pstn_out, wire_rec


def analyze(pstn_in, pstn_out, wire_rec, max_echo_db):
    for p in (pstn_out, wire_rec):
        if not os.path.exists(p) or os.path.getsize(p) == 0:
            print('FAIL: no audio recorded in %s' % p)
            return False

    in_srate, in_s = read_wav(pstn_in)
    out_srate, out_s = read_wav(pstn_out)
    rec_srate, rec_s = read_raw(wire_rec, WIRE_SRATE)

    in_lv = levels(in_srate, in_s, PSTN_FREQ)
    in_peak, _ = summary(in_lv)

    rec_peak, rec_act = summary(levels(rec_srate, rec_s, PSTN_FREQ))
    wire_lv = levels(out_srate, out_s, WIRE_FREQ)
    wire_peak, wire_act = summary(wire_lv)

    # Echo is only measured while the wire tone is silent: G.711
    # quantization noise of the wire tone reaches 1 kHz at about -40 dB
    echo_lv = [e if w <= DETECT_DBFS else -120.0
               for e, w in zip(levels(out_srate, out_s, PSTN_FREQ),
                               wire_lv)]
    echo_peak, echo_act = summary(echo_lv)

    print()
    print('pstn_out: %.1fs at %d Hz, wire_rec: %.1fs at %d Hz'
          % (len(out_s) / out_srate, out_srate,
             len(rec_s) / rec_srate, rec_srate))
    print('%-28s %8s %8s' % ('', 'peak dB', 'active'))
    print('%-28s %8.1f' % ('pstn sent (1 kHz)', in_peak))
    print('%-28s %8.1f %7.0f%%' % ('wire heard pstn (1 kHz)',
                                   rec_peak, rec_act * 100))
    print('%-28s %8.1f %7.0f%%' % ('pstn heard wire (600 Hz)',
                                   wire_peak, wire_act * 100))
    print('%-28s %8.1f %7.0f%%' % ('pstn heard itself (1 kHz)',
                                   echo_peak, echo_act * 100))
    print()

    ok = True
    if rec_peak < DETECT_DBFS:
        print('FAIL: pstn -> wire: no PSTN audio at the wire client')
        ok = False
    if wire_peak < DETECT_DBFS:
        print('FAIL: wire -> pstn: no wire audio at the PSTN caller')
        ok = False

    erl = in_peak - echo_peak
    if erl < max_echo_db:
        print('FAIL: echo: PSTN caller hears itself %.1f dB below the '
              'original, delay ~%d ms'
              % (erl, echo_delay_ms(in_lv, echo_lv)))
        ok = False
    else:
        print('echo: %.1f dB below the original (limit %.0f dB)'
              % (erl, max_echo_db))

    print('PASS' if ok else 'FAIL')
    return ok


def local_addr():
    """Address of the default route, like baresip picks for siptest."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('192.0.2.1', 9))  # no packet is sent
        return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'
    finally:
        s.close()


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(__file__),
                                         '..', '..'))
    ap = argparse.ArgumentParser(
        description='Local PSTN audio and echo test with siptest, '
        'see tools/siptest/README.md')
    ap.add_argument('--siptest', default=os.path.join(root, 'siptest'))
    ap.add_argument('--sipconf', default=os.path.join(root, 'sipconf'),
                    help='siptest baresip config dir')
    ap.add_argument('--sip-port', type=int, default=5060,
                    help='siptest SIP port (sip_listen in --sipconf)')
    ap.add_argument('--baresip', default=shutil.which('baresip'),
                    help='baresip CLI with aufile and g711 modules')
    ap.add_argument('--module-path',
                    help='baresip module dir (default: next to --baresip)')
    ap.add_argument('--baresip-port', type=int, default=5062)
    ap.add_argument('--addr', default=local_addr(),
                    help='local address siptest\'s SIP UA binds to '
                    '(it does not listen on loopback)')
    ap.add_argument('--duration', type=float, default=15.0,
                    help='PSTN call length in seconds')
    ap.add_argument('--setup-delay', type=float, default=5.0,
                    help='seconds between starting siptest and dialing')
    ap.add_argument('--max-echo-db', type=float, default=40.0,
                    help='fail if the PSTN echo is less than this many '
                    'dB below the original')
    ap.add_argument('--workdir',
                    help='keep files here (default: a temp dir)')
    ap.add_argument('--analyze-only', action='store_true',
                    help='only analyze the files in --workdir')
    args = ap.parse_args()

    workdir = os.path.abspath(
        args.workdir or tempfile.mkdtemp(prefix='pstn_audio_test.'))
    os.makedirs(workdir, exist_ok=True)

    if args.analyze_only:
        files = (os.path.join(workdir, 'pstn_in.wav'),
                 os.path.join(workdir, 'pstn_out.wav'),
                 os.path.join(workdir, 'wire_rec.pcm'))
    else:
        if not args.baresip:
            sys.exit('baresip not found, use --baresip')
        if not args.module_path:
            args.module_path = os.path.dirname(
                os.path.abspath(args.baresip))
        files = run(args, workdir)

    print('files in %s' % workdir)
    sys.exit(0 if analyze(*files, args.max_echo_db) else 1)


if __name__ == '__main__':
    main()
