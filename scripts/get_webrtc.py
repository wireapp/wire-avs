#! /usr/bin/env python3

import json
import os
import re
import subprocess
import sys

github_base = 'https://api.github.com/repos'
repo = 'wireapp/prebuilt-webrtc-binaries'
dest_dir = 'contrib/webrtc'

if len(sys.argv) != 2:
	print('usage: {} <release>'.format(sys.argv[0]))
	exit()

release = sys.argv[1]

print('Repo: {}'.format(repo))
print('Release: {}'.format(release))

cmd = ['curl', '-X', 'GET',
	'{}/{}/releases'.format(github_base, repo)]
releases = json.loads(subprocess.check_output(cmd))

if not isinstance(releases, list):
	print('Error {}'.format(releases))
	exit(-1)

if len(releases) < 1:
	print('Couldnt find release {}'.format(release))
	exit(-1)

found = None
for r in releases:
	if r['tag_name'] == release:
		found = r
		break

if not found:
	print('Couldnt find release {}'.format(release))
	exit(-1)

print('Getting assets for {}'.format(release))
for a in found['assets']:
	print(a['name'])
	cmd = ['curl', '-L',
		'-H', 'Accept: application/octet-stream', a['url'], '-o', '{}/{}'.format(dest_dir, a['name'])]
	subprocess.check_output(cmd)
