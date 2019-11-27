#! /bin/bash

SCRIPT_DIR=${BASH_SOURCE%/*}
. "$SCRIPT_DIR/version.sh"

if [ -e depot_tools ]; then
	pushd depot_tools > /dev/null
	git pull
	popd > /dev/null
else
	git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi

export PATH=$PATH:$PWD/depot_tools

if [ -e webrtc_checkout ]; then
	pushd webrtc_checkout/ > /dev/null
else
	mkdir webrtc_checkout
	pushd webrtc_checkout/ > /dev/null
	fetch --nohooks webrtc_ios
fi

pushd src > /dev/null
git checkout remotes/branch-heads/$WEBRTC_RELEASE

gclient sync

for PATCH in ../../patch/*.patch; do 
  patch -p1 < $PATCH
done

export ARGS="is_debug=false rtc_include_tests=false rtc_build_examples=false rtc_build_tools=false"
gn gen out/osx-x86_64 -args="target_os=\"mac\" target_cpu=\"x64\" $ARGS"
ninja -C out/osx-x86_64

gn gen out/ios-x86_64 -args="target_os=\"ios\" target_cpu=\"x64\" $ARGS"
ninja -C out/ios-x86_64

gn gen out/ios-armv7 -args="target_os=\"ios\" target_cpu=\"arm\" $ARGS"
ninja -C out/ios-armv7

gn gen out/ios-arm64 -args="target_os=\"ios\" target_cpu=\"arm64\" $ARGS"
ninja -C out/ios-arm64

popd > /dev/null
popd > /dev/null

