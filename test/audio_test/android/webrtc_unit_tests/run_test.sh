# Compile Excecutable
$ANDROID_NDK_ROOT/ndk-build clean
$ANDROID_NDK_ROOT/ndk-build v=0

# Copy excecutable to device
adb push libs/armeabi-v7a/webrtc_unit_test /data/local/tmp/

# Copy test files to device
#adb push ../audio_test/files/testfile32kHz.pcm /sdcard/

# Run excecutable
adb shell /data/local/tmp/webrtc_unit_test

# Clean up
adb shell rm  /data/local/tmp/webrtc_unit_test

rm libs/armeabi-v7a/webrtc_unit_test
