# Copy excecutable to device
adb push libs/armeabi-v7a/opus_demo /data/local/tmp/

# Copy test files to device
adb push ../../files/far16.pcm /sdcard/
adb push ../../files/far48.pcm /sdcard/

# Run excecutable
#adb shell /data/local/tmp/opus_demo voip 16000 1 16000 -complexity 10 /sdcard/far16.pcm /sdcard/opus_c10_16.pcm
adb shell /data/local/tmp/opus_demo voip 16000 1 32000 -complexity 0 /sdcard/far16.pcm /sdcard/opus_c0_16.pcm

#adb shell /data/local/tmp/opus_demo voip 48000 1 48000 -complexity 10 /sdcard/far48.pcm /sdcard/opus_c10_48.pcm
adb shell /data/local/tmp/opus_demo voip 48000 1 32000 -complexity 0 /sdcard/far48.pcm /sdcard/opus_c0_48.pcm

#adb shell /data/local/tmp/opus_demo voip 48000 1 48000 -complexity 10 -bandwidth WB /sdcard/far48.pcm /sdcard/opus_c10_48.pcm
adb shell /data/local/tmp/opus_demo voip 48000 1 32000 -complexity 0 -bandwidth WB /sdcard/far48.pcm /sdcard/opus_c0_48.pcm

adb shell /data/local/tmp/opus_demo voip 48000 1 32000 -complexity 0 /sdcard/far48.pcm /sdcard/opus_c0_48_16.pcm

# Get output
adb pull /sdcard/opus_c10_16.pcm
adb pull /sdcard/opus_c0_16.pcm
adb pull /sdcard/opus_c0_48.pcm
adb pull /sdcard/opus_c0_48_16.pcm

# Clean up
#adb shell rm /data/local/tmp/opus_demo
adb shell rm /sdcard/opus_c10_16.pcm
adb shell rm /sdcard/opus_c0_16.pcm

rm libs/armeabi-v7a/opus_demo