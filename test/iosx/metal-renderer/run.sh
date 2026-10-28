#!/bin/bash
# Wire
# Copyright (C) 2026 Wire Swiss GmbH
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

test_dir="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$test_dir/../../.." && pwd)"
source_path="${1:-$repo/iosx/src/flowmgr/AVSVideoView_Metal.mm}"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/avs-metal-renderer.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT

# Compile the production implementation, including its actual frame definition
# and shader source. Only UIKit/display-link and Metal execution are substituted.
xcrun --sdk macosx clang++ -std=c++17 -fobjc-arc -fblocks \
    -Wno-objc-property-implementation \
    -I"$test_dir/include" -I"$repo/include" -I"$repo/iosx/src/flowmgr" \
    -framework Foundation -framework Metal -framework MetalKit -framework QuartzCore \
    "$source_path" "$test_dir/main.mm" -o "$build_dir/metal-renderer-test"
"$build_dir/metal-renderer-test"
