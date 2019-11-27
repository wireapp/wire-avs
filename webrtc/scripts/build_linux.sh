#! /bin/bash

SCRIPT_DIR=${BASH_SOURCE%/*}
. "$SCRIPT_DIR/version.sh"

if [ -e depot_tools ]; then
	pushd depot_tools
	git pull
	popd
else
	git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi

export PATH=$PATH:$PWD/depot_tools

if [ -e webrtc_checkout ]; then
	pushd webrtc_checkout/
else
	mkdir webrtc_checkout
	pushd webrtc_checkout/
	fetch --nohooks webrtc_android
fi

pushd src > /dev/null

git checkout remotes/branch-heads/$WEBRTC_RELEASE
git checkout -b release_$WEBRTC_RELEASE
yes | gclient sync

sed s/sudo/echo\ sudo/g build/install-build-deps-android.sh > build/install-build-deps-android-nosudo.sh
. build/install-build-deps-android-nosudo.sh --quick-check
. build/android/envsetup.sh 

for PATCH in ../../patch/*.patch; do 
  patch -p1 < $PATCH
done

gn gen out/linux-x86_64 -args='target_os="linux" target_cpu="x64" is_debug=false rtc_include_tests=false'
ninja -C out/linux-x86_64

gn gen out/android-i386 -args='target_os="android" target_cpu="x86" is_debug=false rtc_include_tests=false'
ninja -C out/android-i386

gn gen out/android-x86_64 -args='target_os="android" target_cpu="x64" is_debug=false rtc_include_tests=false'
ninja -C out/android-x86_64

gn gen out/android-armv7 -args='target_os="android" target_cpu="arm" is_debug=false rtc_include_tests=false'
ninja -C out/android-armv7

gn gen out/android-arm64 -args='target_os="android" target_cpu="arm64" is_debug=false rtc_include_tests=false'
ninja -C out/android-arm64

popd > /dev/null
popd > /dev/null

