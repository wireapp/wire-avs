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

`pstn_audio_test.py` runs an automated local test of the audio path
between a SIP caller and a wire client. It checks for the caller hearing
its own audio back.

- A baresip CLI plays the PSTN caller. It dials siptest, sends 1 kHz
  tone bursts from a WAV file and records what it hears.
- The siptest wire user sends 600 Hz tone bursts (`-p`) and records what
  it hears (`-r`).

Each recording is measured per 20 ms frame for both tones. The test fails
if:

- the wire user doesn't hear the 1 kHz tone (pstn -> wire),
- the PSTN caller doesn't hear the 600 Hz tone (wire -> pstn), or
- the PSTN caller hears its own 1 kHz tone back less than 40 dB below the
  original (echo). The wireaudio mix-minus should keep it out entirely.

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

A run takes about 25 seconds. The exit status is 0 on PASS and 1 on
FAIL. Example output:

```
pstn_out: 18.0s at 8000 Hz, wire_rec: 23.8s at 16000 Hz
                              peak dB   active
pstn sent (1 kHz)               -12.0
wire heard pstn (1 kHz)         -13.8      29%
pstn heard wire (600 Hz)        -10.5      28%
pstn heard itself (1 kHz)       -94.4       0%

echo: 82.4 dB below the original (limit 40 dB)
PASS
```

`peak dB` is the tone's level in dBFS (99th percentile of the frames),
and `active` is the share of frames where the tone is present. Both
tones are on in about 30% of the frames. On echo the test also prints the
delay:

```
FAIL: echo: PSTN caller hears itself 30.0 dB below the original, delay ~200 ms
```

### Options

| option | default | |
|---|---|---|
| `--baresip` | `baresip` in `PATH` | baresip CLI |
| `--module-path` | next to `--baresip` | baresip module dir |
| `--siptest` | `./siptest` | siptest binary |
| `--sipconf` | `./sipconf` | siptest's baresip config dir |
| `--sip-port` | 5060 | siptest's SIP port (`sip_listen` in `--sipconf`) |
| `--addr` | address of the default route | address siptest's SIP UA binds to |
| `--baresip-port` | 5062 | baresip CLI's SIP port |
| `--duration` | 15 | PSTN call length in seconds |
| `--setup-delay` | 5 | seconds between starting siptest and dialing |
| `--max-echo-db` | 40 | fail if the echo is less than this many dB below the original |
| `--workdir` | a new temp dir | where files are written and kept |
| `--analyze-only` | | only analyze the files in `--workdir` |

### Files

The work dir is printed at the end of each run. It contains:

| file | |
|---|---|
| `pstn_in.wav` | what the PSTN caller sends (8 kHz WAV) |
| `pstn_out.wav` | what the PSTN caller hears (8 kHz WAV) |
| `wire_play.pcm` | what the wire user sends (raw, 16 kHz) |
| `wire_rec.pcm` | what the wire user hears (raw, 16 kHz) |
| `siptest.log`, `baresip.log` | logs of both sides |
| `baresip/` | the generated baresip CLI config |

To listen to the raw files, convert them with ffmpeg:

```
ffmpeg -f s16le -ar 16000 -ac 1 -i wire_rec.pcm wire_rec.wav
```

To re-check recordings, for example after changing the analysis, run:

```
tools/siptest/pstn_audio_test.py --analyze-only --workdir <dir>
```

### Notes

- The tone bursts fade in and out, and echo is only measured while the
  wire tone is silent. Hard burst edges, and the G.711 quantization noise
  of the 600 Hz tone, reach 1 kHz at about -30 to -40 dB and would read
  as echo.
- If baresip logs `no laddr`, `--addr` doesn't match the address
  siptest's SIP UA binds to. Check with
  `lsof -nP -a -c siptest -i :5060`.
- baresip truncates long audio device names, so the script runs baresip
  in the work dir and uses only the file names.
- Only one SIP caller is tested. Echo between two PSTN callers in the
  same conversation, and line echo from a real phone network, are not
  covered.
