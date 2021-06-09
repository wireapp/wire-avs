$NDK/ndk-build clean
$NDK/ndk-build
cd libs/armeabi-v7a/
cp ../../../../../../build/android-armv7/lib/libavs.so .
cd ../../
mkdir -p src/org/webrtc
cd src/org/webrtc
ln -s ../../../../../../../android/java/org/webrtc/voiceengine
ln -s ../../../../../../../android/java/org/webrtc/Logging.java
ln -s ../../../../../../../android/java/org/webrtc/ThreadUtils.java
cd ../../../
mkdir -p src/com/waz
cd src/com/waz
ln -s ../../../../../../../android/java/com/waz/call
ln -s ../../../../../../../android/java/com/waz/media
ln -s ../../../../../../../android/java/com/waz/log
ln -s ../../../../../../../android/java/com/waz/avs
ln -s ../../../../../../../android/java/com/waz/audioeffect
cd ../../../
ant clean debug install
rm -r src/com/waz
