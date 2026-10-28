# iOS Metal renderer ordering regression

On macOS with Xcode command-line tools:

```sh
bash test/iosx/metal-renderer/run.sh
```

The test compiles the production `AVSVideoView_Metal.mm` unchanged under ARC,
with its real `avs_vidframe.h` and shader source. Foundation locks, ownership,
and Metal descriptors are real. UIKit, the display link, and GPU command
execution are replaced with controlled host implementations; no AVS or WebRTC
build is required. The macOS Jenkins stage runs this test separately because
the normal macOS wrapper builds `AVSVideoViewOSX.m` instead.

The command backend holds GPU work incomplete while another frame arrives. An
encoder hook also admits a decoder resolution change between binding Y and U/V.
Checks cover:

- No CPU texture or vertex-buffer writes during an in-flight GPU command.
- No duplicate submission of an in-flight frame.
- A coherent Y/U/V texture set when a decoder changes resolution during binding.
- Accepting and rendering the next frame after GPU completion.
- Reusing textures for unchanged scaling and layout-only changes.
- Recovery after a missing drawable, command buffer, or encoder.

To run the same tests against the unfixed implementation without changing the
checkout:

```sh
baseline="$(mktemp -d)"
git show 222a3f94479ee302c3ec58b3c4c7ca2ab3a0a02b:iosx/src/flowmgr/AVSVideoView_Metal.mm > "$baseline/renderer.mm"
bash test/iosx/metal-renderer/run.sh "$baseline/renderer.mm"
rm -rf "$baseline"
```

At the PR's base revision, the original implementation fails 6 of 12 checks;
the suggested fix passes all 12. The test-only commit also demonstrates the
failures before the fix is applied.

The fake command backend tracks ownership of the exact textures and vertex
buffer bound to each command. A mutex probe and semaphore handshake select the
resolution-change interleaving without relying on an arbitrary scheduling
delay; all frame uploads and bindings still execute the production methods.

These tests reproduce resource-ordering defects, not the production SIGSEGV
reported in wireapp/wire-prism#1342. They do not execute GPU commands or exercise
UIKit. Real-device Metal validation, sanitizer stress, and performance checks
remain necessary before claiming the crash is resolved.
