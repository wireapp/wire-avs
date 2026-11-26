#!/usr/bin/env python3
#
# pstn_audio_test.py -- local PSTN audio and echo test with siptest
#
# One or more baresip CLIs play PSTN callers: each dials siptest's SIP UA
# with a WAV file of tone bursts as microphone and records what it hears.
# The siptest wire client sends tone bursts from a file and records what
# it hears, so there is no audio device and no acoustic echo path. Each
# party has its own tone.
#
# Checks, for every party:
#   it hears every other party, at most --max-loss-db below the original
#   it does not hear itself back, at least --max-echo-db below the
#   original (the wireaudio mix-minus, or any loop through WebRTC)
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
TONE_DBFS = -12.0
FRAME_MS = 20
FADE_SECS = 0.02

# Tone and burst pattern (on, period) of each party. The tones are
# spread over the telephone band (300-3400 Hz) in both directions, apart
# from each other's G.711 harmonics. The periods differ so that each
# party's tone is regularly on while the others are silent.
WIRE_TONE = (2400.0, 0.3, 1.0)
PSTN_TONES = [(1000.0, 0.4, 1.3), (1800.0, 0.4, 1.7),
              (700.0, 0.4, 1.9), (2900.0, 0.4, 2.3)]

# The tone is considered present in a frame above this level
DETECT_DBFS = -50.0

# Own tone measured in fewer frames than this is inconclusive
MIN_GATED = 0.05


def tone(srate, freq, secs, on_secs, period, lead_secs, dbfs):
    """Bursts of a sine tone, starting after lead_secs of silence.

    The bursts fade in and out: hard edges splatter into the other
    tone's frequency and read as echo.
    """
    amp = 32767 * 10 ** (dbfs / 20)
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


class Party:
    """A wire client or PSTN caller: the tone it sends, what it hears."""

    def __init__(self, name, freq, on_secs, period, sent, heard):
        self.name = name
        self.freq = freq
        self.on_secs = on_secs
        self.period = period
        self.sent = sent    # file with what it sends
        self.heard = heard  # file with what it hears


def parties(workdir, ncallers):
    wire = Party('wire', *WIRE_TONE,
                 os.path.join(workdir, 'wire_play.pcm'),
                 os.path.join(workdir, 'wire_rec.pcm'))
    pstn = []
    for i, t in enumerate(PSTN_TONES[:ncallers]):
        d = os.path.join(workdir, 'pstn%d' % (i + 1))
        pstn.append(Party('pstn%d' % (i + 1), *t,
                          os.path.join(d, 'pstn_in.wav'),
                          os.path.join(d, 'pstn_out.wav')))
    return [wire] + pstn


def read_audio(path):
    if path.endswith('.wav'):
        return read_wav(path)
    return read_raw(path, WIRE_SRATE)


def stop(procs):
    for p in procs:
        if p and p.poll() is None:
            p.send_signal(signal.SIGINT)
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()


def run(args, workdir):
    wire, *callers = parties(workdir, args.callers)

    # The wire file loops; each PSTN file ends its baresip call, so it
    # is as long as the call
    write_raw(wire.sent, tone(WIRE_SRATE, wire.freq, wire.period,
                              wire.on_secs, wire.period, 0.0, TONE_DBFS))
    for p in [wire] + callers:
        if os.path.exists(p.heard):
            os.remove(p.heard)

    for i, c in enumerate(callers):
        confdir = os.path.dirname(c.sent)
        os.makedirs(confdir, exist_ok=True)
        write_wav(c.sent, PSTN_SRATE,
                  tone(PSTN_SRATE, c.freq, args.duration,
                       c.on_secs, c.period, 1.0, TONE_DBFS))
        port = args.baresip_port + 2 * i
        with open(os.path.join(confdir, 'config'), 'w') as f:
            f.write(BARESIP_CONFIG.format(
                addr=args.addr, port=port,
                module_path=args.module_path,
                play=os.path.basename(c.heard),
                src=os.path.basename(c.sent)))
        with open(os.path.join(confdir, 'accounts'), 'w') as f:
            f.write('<sip:%s@%s:%d>;regint=0;audio_codecs=PCMU\n'
                    % (c.name, args.addr, port))

    # The SIP calls are hung up this long after the last one is answered
    hangup = int(args.duration) + 3

    siptest_log = open(os.path.join(workdir, 'siptest.log'), 'w')
    siptest_cmd = [args.siptest, '-f', args.sipconf,
                   '-p', wire.sent, '-r', wire.heard, '-d', str(hangup)]
    print('siptest: %s' % ' '.join(siptest_cmd))
    siptest = subprocess.Popen(siptest_cmd, stdout=siptest_log,
                               stderr=subprocess.STDOUT)
    baresips = []
    timeout = (args.setup_delay + args.stagger * len(callers)
               + args.duration + 15)
    try:
        # Let the wire client call the pstn client before dialing in
        time.sleep(args.setup_delay)

        for i, c in enumerate(callers):
            if i:
                time.sleep(args.stagger)
            if siptest.poll() is not None:
                sys.exit('siptest exited early, see %s' % siptest_log.name)

            confdir = os.path.dirname(c.sent)
            cmd = [args.baresip, '-f', confdir, '-n', args.addr,
                   '-e', '/dial sip:wire@%s:%d' % (args.addr, args.sip_port),
                   '-t', str(int(args.duration) + 5)]
            print('%s: %s' % (c.name, ' '.join(cmd)))
            # Run in the caller's dir: baresip truncates long audio
            # device names, so the config uses the file names only
            log = open(os.path.join(confdir, 'baresip.log'), 'w')
            baresips.append(subprocess.Popen(cmd, cwd=confdir, stdout=log,
                                             stderr=subprocess.STDOUT,
                                             stdin=subprocess.DEVNULL))

        deadline = time.time() + timeout
        for p in baresips + [siptest]:
            p.wait(timeout=max(deadline - time.time(), 1))
    except subprocess.TimeoutExpired:
        print('timeout, stopping')
    finally:
        stop(baresips + [siptest])


def analyze(workdir, ncallers, max_echo_db, max_loss_db):
    all_parties = parties(workdir, ncallers)
    for p in all_parties:
        if not os.path.exists(p.heard) or os.path.getsize(p.heard) == 0:
            print('FAIL: no audio recorded in %s' % p.heard)
            return False

    # Level of each party's tone in what it sent and what each heard
    sent = {}
    for p in all_parties:
        srate, samples = read_audio(p.sent)
        sent[p.name] = levels(srate, samples, p.freq)

    heard = {}
    for h in all_parties:
        srate, samples = read_audio(h.heard)
        heard[h.name] = {p.name: levels(srate, samples, p.freq)
                         for p in all_parties}

    print()
    print('Tone peak in dBFS (sent at %.0f, max loss %.0f dB), * own '
          'tone, measured only while the others are silent'
          % (TONE_DBFS, max_loss_db))
    print('%-16s' % 'heard by' + ''.join(
        '%14s' % ('%s %d Hz' % (p.name, p.freq)) for p in all_parties))

    errors = []
    for h in all_parties:
        row = '%-16s' % h.name
        for p in all_parties:
            lv = heard[h.name][p.name]
            if p is not h:
                peak, _ = summary(lv)
                row += '%14.1f' % peak
                loss = summary(sent[p.name])[0] - peak
                if loss > max_loss_db:
                    errors.append('%s hears %s (%d Hz) %.1f dB below the '
                                  'original' % (h.name, p.name, p.freq, loss))
                continue

            # Own tone: other tones and their G.711 noise reach the
            # own tone's frequency at about -40 dB, so only frames where
            # all other tones are silent count
            others = [heard[h.name][o.name] for o in all_parties
                      if o is not h]
            gated = [x for i, x in enumerate(lv)
                     if all(o[i] <= DETECT_DBFS for o in others)]
            if len(gated) < MIN_GATED * len(lv):
                row += '%13s*' % 'n/a'
                errors.append('%s: own tone inconclusive, others silent '
                              'in only %d frames' % (h.name, len(gated)))
                continue

            echo_lv = [x if all(o[i] <= DETECT_DBFS for o in others)
                       else -120.0 for i, x in enumerate(lv)]
            peak, _ = summary(gated)
            row += '%13.1f*' % peak
            erl = summary(sent[h.name])[0] - peak
            if erl < max_echo_db:
                errors.append('%s hears itself %.1f dB below the original, '
                              'delay ~%d ms'
                              % (h.name, erl,
                                 echo_delay_ms(sent[h.name], echo_lv)))
        print(row)
    print()

    for e in errors:
        print('FAIL: %s' % e)
    ok = not errors
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
    ap.add_argument('--baresip-port', type=int, default=5062,
                    help='SIP port of the first PSTN caller, the next '
                    'ones use +2, +4, ...')
    ap.add_argument('--callers', type=int, default=2,
                    choices=range(1, len(PSTN_TONES) + 1),
                    help='number of PSTN callers')
    ap.add_argument('--stagger', type=float, default=2.0,
                    help='seconds between the PSTN callers dialing')
    ap.add_argument('--addr', default=local_addr(),
                    help='local address siptest\'s SIP UA binds to '
                    '(it does not listen on loopback)')
    ap.add_argument('--duration', type=float, default=15.0,
                    help='PSTN call length in seconds')
    ap.add_argument('--setup-delay', type=float, default=5.0,
                    help='seconds between starting siptest and dialing')
    ap.add_argument('--max-echo-db', type=float, default=40.0,
                    help='fail if a party hears itself less than this '
                    'many dB below the original')
    ap.add_argument('--max-loss-db', type=float, default=6.0,
                    help='fail if a party hears another more than this '
                    'many dB below the original')
    ap.add_argument('--workdir',
                    help='keep files here (default: a temp dir)')
    ap.add_argument('--analyze-only', action='store_true',
                    help='only analyze the files in --workdir')
    args = ap.parse_args()

    workdir = os.path.abspath(
        args.workdir or tempfile.mkdtemp(prefix='pstn_audio_test.'))
    os.makedirs(workdir, exist_ok=True)

    if args.analyze_only:
        args.callers = len([d for d in os.listdir(workdir)
                            if d.startswith('pstn') and d[4:].isdigit()])
    else:
        if not args.baresip:
            sys.exit('baresip not found, use --baresip')
        if not args.module_path:
            args.module_path = os.path.dirname(
                os.path.abspath(args.baresip))
        run(args, workdir)

    print('files in %s' % workdir)
    sys.exit(0 if analyze(workdir, args.callers, args.max_echo_db,
                        args.max_loss_db) else 1)


if __name__ == '__main__':
    main()
