# Copy excecutable to device
adb push libs/armeabi-v7a/acm_test /data/local/tmp/

# Copy test files to device
adb push ../../files/far16.pcm /sdcard/
adb push ../../files/far32.pcm /sdcard/
adb push ../../files/Jitter20Ms.dat /sdcard/

# Run excecutable
adb shell /data/local/tmp/acm_test -codec opus -fs 16000 -in /sdcard/far16.pcm -rtp /sdcard/rtp.dat -out /sdcard/acm_out1.pcm

adb shell /data/local/tmp/acm_test -codec opus  -fs 16000 -in /sdcard/far16.pcm -rtp /sdcard/rtp.dat -out /sdcard/acm_out2.pcm -nw /sdcard/Jitter20Ms.dat

adb shell /data/local/tmp/acm_test -codec opus  -fs 32000 -in /sdcard/far32.pcm -rtp /sdcard/rtp.dat -out /sdcard/acm_out3.pcm

adb shell /data/local/tmp/acm_test -codec opus  -fs 32000 -in /sdcard/far32.pcm -rtp /sdcard/rtp.dat -out /sdcard/acm_out4.pcm -nw /sdcard/Jitter20Ms.dat

# Get output
adb pull /sdcard/acm_out1.pcm
adb pull /sdcard/acm_out2.pcm
adb pull /sdcard/acm_out3.pcm
adb pull /sdcard/acm_out4.pcm

# Clean up
adb shell rm /data/local/tmp/acm_test
adb shell rm /sdcard/acm_out1.pcm
adb shell rm /sdcard/acm_out2.pcm
adb shell rm /sdcard/acm_out3.pcm
adb shell rm /sdcard/acm_out4.pcm

rm libs/armeabi-v7a/acm_test