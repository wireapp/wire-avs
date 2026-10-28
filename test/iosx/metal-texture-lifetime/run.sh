#!/bin/bash
#
# Texture lifetime test for the iOS Metal video renderer (macOS host).
#
# Usage: run.sh [path/to/AVSVideoView_Metal.mm]
#
# Defaults to the renderer in this checkout. Pass another copy, e.g. from
# `git show <rev>:iosx/src/flowmgr/AVSVideoView_Metal.mm`, to test it instead.

set -euo pipefail

test_dir="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$test_dir/../../.." && pwd)"
renderer="${1:-$repo/iosx/src/flowmgr/AVSVideoView_Metal.mm}"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/avs-metal-texture-lifetime.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT

xcrun --sdk macosx clang++ -std=c++17 -fobjc-arc -fblocks \
	-Wno-objc-property-implementation \
	-I"$test_dir/include" -I"$repo/include" -I"$repo/iosx/src/flowmgr" \
	-framework Foundation -framework Metal -framework QuartzCore \
	"$renderer" "$test_dir/main.mm" -o "$build_dir/metal-texture-lifetime"

"$build_dir/metal-texture-lifetime"
