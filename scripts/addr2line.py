#! /usr/bin/env python

import re
import subprocess
import sys

addr2line = 'build/toolchains/android-armv7/bin/arm-linux-androideabi-addr2line'
libavs = 'android/obj/local/armeabi-v7a/libavs.so'

if len(sys.argv) != 2:
	print('usage: python {} <logcat file>'.format(sys.argv[0]))
	exit()

lines = open(sys.argv[1]).readlines()

for l in lines:
	m = re.search('(.*?) #(\d+) pc (\w+) .*libavs.so', l)
	if m:
		ts = m.group(1)
		frame = m.group(2)
		addr = m.group(3)

		s = subprocess.check_output([addr2line, '-f', '-e', libavs, addr])
		if int(frame) == 1:
			print('\n********************************\n')
		print('{} #{} pc {} {}'.format(ts, frame, addr, s))
	#else:
	#	m = re.search('(.*?) #(\d+) pc (\w+) .*', l)
	#	if m:
	#		print(l.rstrip())

