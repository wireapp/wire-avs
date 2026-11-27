# sftloader

`sftloader` runs many simulated Wire clients in one process. Each client
joins one conference call on one or more SFTs (sftd). Use it for:

- load testing an sftd
- checking that every client receives the right video from every other
  client (`-V`)
- reproducing SFT bugs: federation, tile switching

All clients share one process. They exchange call messages in-process, so
no Wire backend is needed, only the SFT(s). Clients talk to the SFTs
directly. They do not use TURN: no `ice_servers` are sent.

## Build

```
make -j8 sftloader        # binary: ./sftloader
```

If the build fails after a branch switch with "No rule to make target
...h", delete the stale `.d` files under `build/<arch>/obj` that name the
missing header.

## Options

| Option | Meaning |
|---|---|
| `-h` | Show help |
| `-u <n>` | Number of users: `usr00000`, `usr00001`, ... |
| `-c <n>` | Client id number for all users (default 0, so client id `0000`) |
| `-s <url>` | SFT url, `url[\|user\|cred]` |
| `-S <urls>` | Comma separated list of all SFTs (`sft_servers_all`) |
| `-f` | Clients are federating (`is_federating`) |
| `-C <file>` | Per-user config JSON, replaces `-u` (see below) |
| `-i <convid>` | Conversation id (default: random uuid) |
| `-n <n>` | Calls per user. Default is endless: a user re-answers after each call ends |
| `-d <sec>` | Call duration. Each user leaves after `sec`. With `-n 1`, sftloader exits when all users are done |
| `-t <ms>` | Parsed but currently unused |
| `-v` | Send video. All users send the same generated picture |
| `-V` | Stamp and verify video, implies `-v` (see below) |
| `-P head\|octo` | Video pattern: talking head (default) or the old moving octagon |
| `-W <n>` | Request at most `n` video streams per client (default 9) |
| `-R <ms>` | Tile switching: rotate the requested streams every `ms` |
| `-Y <ms>` | Start every user's video at the same time, `ms` after sftloader starts |
| `-l <file>` | Write the AVS/wcall log to a file instead of stdout |

The first user starts the call. The others answer after `answer_ms` (from
`-C`), or after a random 2-12 s.

Without `-Y`, each user turns its video on 2-7 s after its call is
established.

## Per-user config (`-C users.json`)

```json
[
  {"userid": "usera", "sft_url": "http://127.0.0.1:8585",
   "sfts_all": ["http://127.0.0.1:8585"]},
  {"userid": "userb", "clientid": "0000",
   "sft_url": "http://127.0.0.1:8595|user|cred",
   "sfts_all": "http://127.0.0.1:8595",
   "is_federating": true, "answer_ms": 1000}
]
```

- Only `userid` is required. Every other field defaults to the global
  `-c/-s/-S/-f` value.
- Userids must be lowercase. AVS lowercases ids in the epoch client list,
  and with mixed case no video is rendered.
- `sfts_all` is either an array or a comma separated string. Clients
  join an SFT that is already in the call only if it is in their
  `sfts_all`. Otherwise, if `is_federating` is set, they join their own
  `sft_url`, and that SFT federates to the host. So to force federation,
  give each client only its own SFT in `sfts_all`.
- The first entry starts the call, so put the host SFT's client first.

## Video verification (`-V`)

Each user stamps its own index into every frame it sends. For every
decoded frame, the receiver reads the stamp and compares it with the
user/client that AVS says the frame came from.

Results go to stdout. A progress line per pair prints every 5 s, and a
final one at exit:

```
VIDEO_RX[final] recv=userb.0000 from=usera.0000 frames=94 ok=94 wrong=0 unreadable=0 wrong_from=-
VIDEO_VERIFY: PASS pairs=12 failed=0
```

| Counter | Meaning |
|---|---|
| `ok` | The stamp matches the sender AVS attributed the frame to |
| `wrong` | Another user's stamp. The receiver shows the wrong person's video. `wrong_from` is the user it actually showed |
| `unreadable` | No valid stamp, usually a corrupted (mis-decoded) frame. Does not fail the run on its own |

A pair fails if it got no `ok` frames or any `wrong` frame.

Exit codes:

- `0`: all pairs passed, or `-V` is not used
- `1`: at least one pair failed
- `2`: setup or config error, for example a bad `-C` file

## Tile switching (`-W`, `-R`, `-Y`)

A client that requests fewer streams than there are video senders gets a
"tile" per stream. `-R` rotates which senders it requests, so the SFT
keeps giving the same stream (same ssrc, same decoder on the receiver)
to a different source. The order is sorted, and each client starts at a
different offset. Each request is printed:

```
TILES user=usera.0000 streams=[userc]
```

The SFT must not forward the new source's delta frames before that
source's keyframe. Otherwise the receiver decodes them on top of the old
source's frames and shows the old user's video under the new user's
name. With `-V`, this shows up as `wrong` and `unreadable` frames.

**Use `-Y` to reproduce this.** libwebrtc numbers each sender's frames
from 0. The receiver decodes a delta frame once it has decoded the frame
id that the delta refers to, from whichever source. With random video
start times, senders' frame ids are 30-100 apart, so the new source's
frames just wait for a keyframe and the bug stays hidden. Starting all
video at the same moment keeps the ids close, like real clients that
turned on video at about the same time.

```
sftloader -u 4 -s http://127.0.0.1:8585 -V -W 1 -R 2500 -Y 6000 -d 30 -n 1
```

Results from 2026-11-27: an sftd without the keyframe-switch fix gets
`wrong>0` in every run. A fixed sftd passes every run.

## Examples

Local smoke test against one sftd:

```
sftd -I 127.0.0.1 -p 8585 -A 127.0.0.1 -u http://127.0.0.1:8585 -q &
sftloader -u 3 -s http://127.0.0.1:8585 -V -d 35 -n 1 -l sl.log
grep 'VIDEO_RX\[final\]\|VIDEO_VERIFY' <stdout>
```

macOS has no `timeout`. To guard against hangs, wrap the command:
`perl -e 'alarm 50; exec @ARGV' sftloader ...`

Plain load test, with endless calls and video until killed:

```
sftloader -u 50 -s https://sft.example.com -v
```

Federation and tile-switching end-to-end tests with coturn are in
wire-avs-service, `test/federation/run.py` (`--case star|control|tiles`).
That script writes the `-C` file and runs sftloader for you.

## Notes

- The video frame rate is low locally (about 11-14 fps). Check
  `ok`/`wrong`, not fps.
- If your Mac sleeps during a long run, the call stalls. That is not a
  sftloader hang.
