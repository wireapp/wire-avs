# Compile Excecutable
$ANDROID_NDK_ROOT/ndk-build clean
$ANDROID_NDK_ROOT/ndk-build v=0

# Copy excecutable to device
adb push libs/armeabi-v7a/ztest /data/local/tmp/

# Copy test files to device
adb push ../data/chunk0 /sdcard/
adb push ../data/chunk1 /sdcard/
adb push ../data/chunk2 /sdcard/
adb push ../data/chunk3 /sdcard/
adb push ../data/chunk4 /sdcard/
adb push ../data/chunk_total /sdcard/

adb push ../audio_test/files/testfile32kHz.pcm /sdcard/

# Run excecutable
adb shell /data/local/tmp/ztest

# Clean up
adb shell rm  /data/local/tmp/ztest
adb shell rm  /sdcard/chunk0
adb shell rm  /sdcard/chunk1
adb shell rm  /sdcard/chunk2
adb shell rm  /sdcard/chunk3
adb shell rm  /sdcard/chunk4
adb shell rm  /sdcard/chunk_total

rm libs/armeabi-v7a/ztest
