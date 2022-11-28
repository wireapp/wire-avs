#! /bin/bash
# Downloads version of Emscripten SDK required for this version of AVS.
# Sets environment variables needed for this version of AVS, run using source:
#
# source scripts/wasm_devenv.sh
#
# This will place the SDKs in ../devtools, set the AVS_DEVTOOLS_ROOT env variable
# to override
#

if [ -z "$AVS_DEVTOOLS_ROOT" ]; then
	AVS_DEVTOOLS_ROOT=$(git rev-parse --show-toplevel)/../devtools
fi

PLATFORM=$(uname -s | awk '{ print tolower($1) }')
MACHINE=$(uname -m)

EMSDK_VER=3.1.26
EMSDK_VER_FULL=sdk-$EMSDK_VER-64bit

if [ ! -e $AVS_DEVTOOLS_ROOT ]; then
	mkdir -p $AVS_DEVTOOLS_ROOT
fi

pushd $AVS_DEVTOOLS_ROOT > /dev/null

if [ ! -e emsdk ]; then
	git clone https://github.com/emscripten-core/emsdk.git
fi

cd emsdk

if [ ! -e emscripten/$EMSDK_VER ]; then
	echo "Cleaning and pulling"
	git checkout -- .
	git pull

	echo "Installing EMSDK $EMSDK_VER"
	./emsdk install $EMSDK_VER_FULL > /dev/null
fi

echo "Setting EMSDK $EMSDK_VER"
./emsdk activate $EMSDK_VER_FULL > /dev/null
popd > /dev/null

echo "Setting environment variables"
export WASM_PATH=$AVS_DEVTOOLS_ROOT/emsdk/emscripten/$EMSDK_VER/

