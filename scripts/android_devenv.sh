#! /bin/bash
# Downloads version of NDK and SDK required for this version of AVS.
# Sets environment variables needed for this version of AVS, run using source:
#
# source scripts/android_devenv.sh
#
# This will place the SDKs in $WORKSPACE/devtools, set the AVS_DEVTOOLS_ROOT env variable
# to override
#

if [ -z "$AVS_DEVTOOLS_ROOT" ]; then
	AVS_DEVTOOLS_ROOT=$(git rev-parse --show-toplevel)/devtools
fi

PLATFORM=$(uname -s | awk '{ print tolower($1) }')
MACHINE=$(uname -m)

TOOLS_VER=8092744_latest
BUILDTOOLS_VER=32.0.0
ANDROID_NDK_VER=21.0.6113669
ANDROID_SDK_VER=android-21

if [[ "$PLATFORM" == "darwin" ]]; then
	ANDROID_SDK_PLAT=mac
else
	ANDROID_SDK_PLAT=$PLATFORM
fi

TOOLS_PATH=$AVS_DEVTOOLS_ROOT/cmdline-tools/latest
TOOLS_ZIP=commandlinetools-$ANDROID_SDK_PLAT-$TOOLS_VER.zip

if [ ! -e $AVS_DEVTOOLS_ROOT ]; then
	mkdir -p $AVS_DEVTOOLS_ROOT
fi

pushd $AVS_DEVTOOLS_ROOT > /dev/null

if [ ! -e $TOOLS_PATH ]; then
	if [ ! -e $TOOLS_ZIP ]; then
		echo "Downloading commandline tools"
		URL=https://dl.google.com/android/repository/$TOOLS_ZIP
		echo $URL
		curl https://dl.google.com/android/repository/$TOOLS_ZIP > $TOOLS_ZIP
	fi
	echo "Unpacking commandline tools"
	unzip $TOOLS_ZIP > /dev/null
	mv cmdline-tools latest
	mkdir cmdline-tools
	mv latest cmdline-tools

fi

export PATH=$PATH:$PWD/cmdline-tools/latest/bin
yes | sdkmanager "build-tools;$BUILDTOOLS_VER" "ndk;$ANDROID_NDK_VER" "platforms;$ANDROID_SDK_VER"

popd > /dev/null

echo "Setting environment variables"
echo "NDK ($ANDROID_NDK_VER)"
echo "SDK ($ANDROID_SDK_VER)"

export ANDROID_HOME=$AVS_DEVTOOLS_ROOT
export ANDROID_SDK_ROOT=$AVS_DEVTOOLS_ROOT
export ANDROID_NDK_ROOT=$AVS_DEVTOOLS_ROOT/ndk/$ANDROID_NDK_VER

