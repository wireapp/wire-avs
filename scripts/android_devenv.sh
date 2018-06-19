#! /bin/bash
# Downloads version of NDK and SDK required for this version of AVS.
# Sets environment variables needed for this version of AVS, run using source:
#
# source scripts/android_devenv.sh
#
# This will place the SDKs in ../android_dev, set the ANDROID_DEV_ROOT env variable
# to override
#

if [ -z "$ANDROID_DEV_ROOT" ]; then
	ANDROID_DEV_ROOT=$(git rev-parse --show-toplevel)/../android_dev
fi

PLATFORM=$(uname -s | awk '{ print tolower($1) }')
MACHINE=$(uname -m)

ANDROID_NDK_VER=android-ndk-r14b
ANDROID_NDK_PLAT=$PLATFORM-$MACHINE

ANDROID_SDK_VER=android-sdk_r24.4.1
if [ "$PLATFORM" == "darwin" ]; then
	ANDROID_SDK_PLAT=macosx
else
	ANDROID_SDK_PLAT=$PLATFORM
fi

ANDROID_SDK_PKG=zip
ANDROID_SDK_TARGET=android-sdk-$ANDROID_SDK_PLAT
ANDROID_SDK_PKG_FILE=$ANDROID_SDK_VER-$ANDROID_SDK_PLAT.$ANDROID_SDK_PKG

if [ ! -e $ANDROID_DEV_ROOT ]; then
	mkdir -p $ANDROID_DEV_ROOT
fi

pushd $ANDROID_DEV_ROOT > /dev/null

if [ ! -e $ANDROID_NDK_VER ]; then
	if [ ! -e $ANDROID_NDK_VER-$ANDROID_NDK_PLAT.zip ]; then
		echo "Downloading NDK ($ANDROID_NDK_VER)"
		curl http://dl.google.com/android/repository/$ANDROID_NDK_VER-$ANDROID_NDK_PLAT.zip > $ANDROID_NDK_VER-$ANDROID_NDK_PLAT.zip
	fi
	echo "Unpacking NDK ($ANDROID_NDK_VER)"
	unzip $ANDROID_NDK_VER-$ANDROID_NDK_PLAT.zip > /dev/null
fi

if [ ! -e $ANDROID_SDK_TARGET ]; then
	if [ ! -e $ANDROID_SDK_PKG_FILE ]; then
		echo "Downloading SDK ($ANDROID_SDK_VER)"
		curl https://dl.google.com/android/$ANDROID_SDK_PKG_FILE > $ANDROID_SDK_PKG_FILE
	fi
	echo "Unpacking SDK ($ANDROID_SDK_VER)"
	if [ "$ANDROID_SDK_PKG" == "zip" ]; then
		unzip $ANDROID_SDK_PKG_FILE > /dev/null
	elif [ "$ANDROID_SDK_PKG" == "tgz" ]; then
		tar zxf $ANDROID_SDK_PKG_FILE
	fi

	cd $ANDROID_DEV_ROOT/$ANDROID_SDK_TARGET
	tools/android update sdk --no-ui
fi

popd > /dev/null

echo "Setting environment variables"
echo "NDK ($ANDROID_NDK_VER)"
echo "SDK ($ANDROID_SDK_VER)"

export ANDROID_HOME=$ANDROID_DEV_ROOT/$ANDROID_SDK_TARGET
export ANDROID_SDK_ROOT=$ANDROID_DEV_ROOT/$ANDROID_SDK_TARGET
export ANDROID_NDK_ROOT=$ANDROID_DEV_ROOT/$ANDROID_NDK_VER
export PATH=$PATH:$ANDROID_SDK_ROOT/platform-tools

