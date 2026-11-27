# siptest

`siptest` runs two calling users in one process and connects a SIP call
to them:

- **pstn**: answers 1:1 calls and bridges them to SIP. It uses the `pstn`
  audio backend, and baresip's `wireaudio` module with the mixer.
- **wire**: calls the pstn user. It uses the normal audio device, or
  audio files (see `-p`/`-r` below).

The wire user calls the pstn user, and the pstn user answers. A SIP
call to the UA, `sip:wire@<host>` by default, is then answered and
bridged into that conversation.

```
make siptest
./siptest -f sipconf [options]
```

| option | |
|---|---|
| `-f <dir>` | baresip config dir for the SIP UA (required), e.g. `sipconf` |
| `-s <aor>` | SIP account (default `sip:wire@127.0.0.1;regint=0`) |
| `-t <sec>` | delay before the wire user calls the pstn user |
| `-a <sec>` | delay before the pstn user answers (default 1) |
| `-d <sec>` | hang up the SIP call this long after answering, then exit |
| `-p <file>` | wire user sends this file instead of the microphone |
| `-r <file>` | wire user writes what it hears to this file instead of the speaker |

`-p` and `-r` files are raw 16-bit native-endian mono PCM at 16 kHz, and
the `-p` file loops. With either option the wire user uses no audio
device.

The SIP UA listens on port 5060 of the host's LAN address, not on
loopback, even with `sip_listen 0.0.0.0:5060`.

## PSTN audio and echo test

`pstn_audio_test.py` runs an automated local test of the audio between
SIP callers and a wire client in one conversation, including callers
hearing their own audio back.

- One or more baresip CLIs (`--callers`, default 2) play PSTN callers.
  Each dials siptest a few seconds after the previous one, sends tone
  bursts from a WAV file and records what it hears.
- The siptest wire user sends tone bursts (`-p`) and records what it
  hears (`-r`).

Each party has its own tone:

| party | tone | burst |
|---|---|---|
| wire | 2400 Hz | 0.3 s every 1.0 s |
| pstn1 | 1000 Hz | 0.4 s every 1.3 s |
| pstn2 | 1800 Hz | 0.4 s every 1.7 s |
| pstn3 | 700 Hz | 0.4 s every 1.9 s |
| pstn4 | 2900 Hz | 0.4 s every 2.3 s |

The tones cover the telephone band (300-3400 Hz) in both directions. All
tones are sent at -12 dBFS, and each recording is measured per 20 ms
frame for every tone. The test fails if a party:

- hears another party more than 6 dB below the original
  (`--max-loss-db`), or
- hears itself back less than 40 dB below the original
  (`--max-echo-db`). The wireaudio mix-minus should keep it out
  entirely.

No audio devices are used, so there is no acoustic echo, and any echo
found comes from the software.

### Requirements

- `siptest` built in the wire-avs root (`make siptest`)
- a baresip CLI built with the `g711`, `aufile`, `account` and `menu`
  modules. This is a separate baresip (tested with v4.6), not
  `contrib/baresip`:
  ```
  git clone https://github.com/baresip/baresip && cd baresip
  cmake -B build && cmake --build build -j
  ```
- python 3, standard library only

### Running

From the wire-avs root:

```
make siptest
tools/siptest/pstn_audio_test.py --baresip ~/projects/baresip/build/baresip
```

A run takes about 30 seconds. The exit status is 0 on PASS and 1 on
FAIL. Example output with 4 callers:

```
Tone peak in dBFS (sent at -12, max loss 6 dB), * own tone, measured only while the others are silent
heard by          wire 2400 Hz pstn1 1000 Hz pstn2 1800 Hz  pstn3 700 Hz pstn4 2900 Hz
wire                    -96.5*         -11.8         -11.8         -11.8         -11.9
pstn1                    -11.9        -98.0*         -11.9         -11.9         -11.9
pstn2                    -11.9         -11.9       -100.1*         -11.9         -11.9
pstn3                    -11.9         -11.9         -11.9        -96.3*         -11.9
pstn4                    -11.9         -11.9         -11.9         -11.9        -93.8*

PASS
```

Each row is what one party hears, and each column is one party's tone,
as its peak in dBFS (99th percentile of the frames). `*` marks a party's
own tone, which should be silent.

Failures name the parties, the tone and the loss. For an echo they also
give the delay:

```
FAIL: wire hears pstn2 (1800 Hz) 42.7 dB below the original
FAIL: pstn1 hears itself 30.0 dB below the original, delay ~200 ms
```

The first line is from wireaudio resampling with rem's auresamp, which
cut the wire side above about 1.2 kHz in both directions.

### Options

| option | default | |
|---|---|---|
| `--baresip` | `baresip` in `PATH` | baresip CLI |
| `--module-path` | next to `--baresip` | baresip module dir |
| `--siptest` | `./siptest` | siptest binary |
| `--sipconf` | `./sipconf` | siptest's baresip config dir |
| `--sip-port` | 5060 | siptest's SIP port (`sip_listen` in `--sipconf`) |
| `--addr` | address of the default route | address siptest's SIP UA binds to |
| `--callers` | 2 | number of PSTN callers, 1-4 |
| `--stagger` | 2 | seconds between the callers dialing |
| `--baresip-port` | 5062 | first caller's SIP port; later callers use +2, +4, ... |
| `--duration` | 15 | length of each PSTN call in seconds |
| `--setup-delay` | 5 | seconds between starting siptest and dialing |
| `--max-loss-db` | 6 | fail if a party hears another more than this many dB below the original |
| `--max-echo-db` | 40 | fail if a party hears itself less than this many dB below the original |
| `--workdir` | a new temp dir | where files are written and kept |
| `--analyze-only` | | only analyze the files in `--workdir` |

### Files

The work dir is printed at the end of each run. It contains:

| file | |
|---|---|
| `wire_play.pcm` | what the wire user sends (raw, 16 kHz) |
| `wire_rec.pcm` | what the wire user hears (raw, 16 kHz) |
| `siptest.log` | siptest log |
| `pstnN/pstn_in.wav` | what caller N sends (8 kHz WAV) |
| `pstnN/pstn_out.wav` | what caller N hears (8 kHz WAV) |
| `pstnN/baresip.log` | caller N's baresip log |
| `pstnN/config`, `pstnN/accounts` | caller N's generated baresip config |

To listen to the raw files, convert them with ffmpeg:

```
ffmpeg -f s16le -ar 16000 -ac 1 -i wire_rec.pcm wire_rec.wav
```

To re-check recordings, for example after changing the analysis, run
the command below. It finds the callers from the `pstnN` directories:

```
tools/siptest/pstn_audio_test.py --analyze-only --workdir <dir>
```

### Notes

- The tone bursts fade in and out, and a party's own tone is only
  measured while all the other tones are silent. Hard burst edges, and
  the G.711 quantization noise of the other tones, reach the own tone's
  frequency at about -30 to -40 dB and would read as echo.
- If baresip logs `no laddr`, `--addr` doesn't match the address
  siptest's SIP UA binds to. Check with
  `lsof -nP -a -c siptest -i :5060`.
- baresip truncates long audio device names, so the script runs each
  baresip in its caller's dir and uses only the file names.
- Line echo from a real phone network is not simulated.
