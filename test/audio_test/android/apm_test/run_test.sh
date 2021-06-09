# Copy excecutable to device
adb push libs/armeabi-v7a/apm_test /data/local/tmp/

# Copy test files
adb push ../../files/far16.pcm /sdcard/
adb push ../../files/near16.pcm /sdcard/

# Run excecutable
adb shell /data/local/tmp/apm_test -near /sdcard/near16.pcm -far /sdcard/far16.pcm -out /sdcard/

# Get output
adb pull /sdcard/near16_out_1.pcm
adb pull /sdcard/near16_out_2.pcm
adb pull /sdcard/near16_out_3.pcm
adb pull /sdcard/near16_out_4.pcm
adb pull /sdcard/near16_out_5.pcm
adb pull /sdcard/near16_out_6.pcm
adb pull /sdcard/near16_out_7.pcm

# Clean Up
adb shell rm /data/local/tmp/apm_test
adb shell rm /sdcard/near16.pcm
adb shell rm /sdcard/far16.pcm
adb shell rm /sdcard/near16_out_1.pcm
adb shell rm /sdcard/near16_out_2.pcm
adb shell rm /sdcard/near16_out_3.pcm
adb shell rm /sdcard/near16_out_4.pcm
adb shell rm /sdcard/near16_out_5.pcm
adb shell rm /sdcard/near16_out_6.pcm
adb shell rm /sdcard/near16_out_7.pcm

#rm libs/armeabi-v7a/apm_test