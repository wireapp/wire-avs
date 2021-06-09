# Copy excecutable to device
adb push libs/armeabi-v7a/resampler_test /data/local/tmp/

# Copy test files to device
adb push ../../files/far44000.pcm /sdcard/
adb push ../../files/far44100.pcm /sdcard/
adb push ../../files/far48000.pcm /sdcard/

# Run excecutable
adb shell /data/local/tmp/resampler_test -fs_in 44000 -in /sdcard/far44000.pcm -fs_out 32000 -out /sdcard/out32000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 32000 -in /sdcard/out32000.pcm -fs_out 44000 -out /sdcard/out44000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 44100 -in /sdcard/far44100.pcm -fs_out 32000 -out /sdcard/out32000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 32000 -in /sdcard/out32000.pcm -fs_out 44100 -out /sdcard/out44100.pcm

adb shell /data/local/tmp/resampler_test -fs_in 48000 -in /sdcard/far48000.pcm -fs_out 32000 -out /sdcard/out32000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 32000 -in /sdcard/out32000.pcm -fs_out 48000 -out /sdcard/out48000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 44000 -in /sdcard/far44000.pcm -fs_out 16000 -out /sdcard/out16000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 16000 -in /sdcard/out16000.pcm -fs_out 44000 -out /sdcard/out44000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 44100 -in /sdcard/far44100.pcm -fs_out 16000 -out /sdcard/out32000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 16000 -in /sdcard/out16000.pcm -fs_out 44100 -out /sdcard/out44100.pcm

adb shell /data/local/tmp/resampler_test -fs_in 48000 -in /sdcard/far48000.pcm -fs_out 16000 -out /sdcard/out32000.pcm

adb shell /data/local/tmp/resampler_test -fs_in 16000 -in /sdcard/out16000.pcm -fs_out 48000 -out /sdcard/out48000.pcm


# Get output
adb pull /sdcard/out44000.pcm
adb pull /sdcard/out44100.pcm
adb pull /sdcard/out48000.pcm

# Clean up
adb shell rm /data/local/tmp/resampler_test
adb shell rm /sdcard/out44000.pcm
adb shell rm /sdcard/out44100.pcm
adb shell rm /sdcard/out48000.pcm

rm libs/armeabi-v7a/resampler_test