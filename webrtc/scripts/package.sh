#! /bin/bash

SCRIPT_DIR=${BASH_SOURCE%/*}
. "$SCRIPT_DIR/version.sh"

if [ "$BUILD_NUMBER" == "" ]; then
	export BUILD_NUMBER=local;
fi

AVS_OS="osx ios linux android"

WEBRTC_RB="$WEBRTC_RELEASE.$BUILD_NUMBER"
DEST_DIR="../contrib/webrtc"

HOST_OS=$(uname)

echo "Packaging header files"
rm -r $WEBRTC_RB 2> /dev/null
mkdir -p $WEBRTC_RB/include
echo WEBRTC_RELEASE=$WEBRTC_RELEASE > $WEBRTC_RB/version.txt
echo WEBRTC_COMMIT=$WEBRTC_COMMIT >> $WEBRTC_RB/version.txt
find webrtc_checkout/src -type f -iname "*.h" -exec scripts/cpheader.sh {} $WEBRTC_RB \;
zip -9r webrtc_${WEBRTC_RB}_headers.zip $WEBRTC_RB version.txt

for OS in $AVS_OS; do
	echo "Packaging $OS files"
	rm -r $WEBRTC_RB 2> /dev/null

	for p in webrtc_checkout/src/out/${OS}*; do
		dst=$WEBRTC_RB/lib/${p/webrtc_checkout\/src\/out\//}
		if [ -e $p/obj/libwebrtc.a ]; then
			mkdir -p $dst
			cp $p/obj/libwebrtc.a $dst/
		fi

		jar=$p/obj/modules/audio_device/audio_device_java__process_prebuilt-desugar.jar
		if [ -e $jar ]; then
			mkdir -p $WEBRTC_RB/java
			cp $jar $WEBRTC_RB/java/audiodev.jar
		fi

		jar=$p/obj/rtc_base/base_java__process_prebuilt-desugar.jar
		if [ -e $jar ]; then
			mkdir -p $WEBRTC_RB/java
			cp $jar $WEBRTC_RB/java/base.jar
		fi

		if [ "$HOST_OS" == "Darwin" ] && [ "$OS" == "ios" ]; then
			mkdir -p $WEBRTC_RB/ios
			cp webrtc_checkout/src/sdk/objc/components/audio/RTCAudioSession+Configuration.mm $WEBRTC_RB/ios
			cp webrtc_checkout/src/sdk/objc/helpers/UIDevice+RTCDevice.mm $WEBRTC_RB/ios
		fi
	done


	if [ -e $WEBRTC_RB ]; then
		zip -9r webrtc_${WEBRTC_RB}_${OS}.zip $WEBRTC_RB
	fi
done

echo "Copying zips to $DEST_DIR"
mkdir -p $DEST_DIR
cp *.zip $DEST_DIR
